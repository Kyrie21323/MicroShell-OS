#include "net.h"
#include "parse.h"
#include "exec.h"
#include "util.h"
#include "errors.h"
#include "job.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h> 
#include <stdarg.h> // Required for va_list

#define MAX_CMD_LENGTH 1024 
#define SCHED_QUANTUM_1 3
#define SCHED_QUANTUM_REST 7

// Global State
static int server_fd = -1;
static volatile sig_atomic_t g_stop = 0;
static int client_id_counter = 0;
static int job_id_counter = 0;

// Scheduler Queue
Job *job_queue_head = NULL;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

// Logs
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
void safe_log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    pthread_mutex_lock(&log_mutex);
    vprintf(fmt, args);
    fflush(stdout);
    pthread_mutex_unlock(&log_mutex);
    va_end(args);
}

// Signal Handler (Fixed: Standard C function instead of C++ lambda)
void handle_sigint(int sig) {
    (void)sig;
    g_stop = 1;
    if(server_fd >= 0) close(server_fd);
    // Wake up scheduler so it can exit
    pthread_mutex_lock(&queue_mutex);
    pthread_cond_broadcast(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);
}

// --- Queue Helpers ---

void add_job(Job *new_job) {
    pthread_mutex_lock(&queue_mutex);
    
    if (!job_queue_head) {
        job_queue_head = new_job;
    } else {
        Job *curr = job_queue_head;
        while (curr->next) curr = curr->next;
        curr->next = new_job;
    }
    
    safe_log("(%d) created (%d)\n", new_job->client_id, new_job->initial_burst);
    
    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);
}

// SRJF Selection Algorithm [cite: 13, 48]
Job *select_job(int remove) {
    if (!job_queue_head) return NULL;

    Job *best = job_queue_head;
    Job *prev = NULL;
    Job *best_prev = NULL;

    Job *curr = job_queue_head;
    Job *curr_prev = NULL;

    while (curr) {
        // Priority 1: Shell Commands (burst == -1) [cite: 50]
        if (curr->initial_burst == -1) {
            if (best->initial_burst != -1) {
                best = curr;
                best_prev = curr_prev;
            }
        }
        // Priority 2: Shortest Remaining Job (if neither is shell)
        else if (best->initial_burst != -1) {
            if (curr->remaining_time < best->remaining_time) {
                best = curr;
                best_prev = curr_prev;
            }
        }
        
        curr_prev = curr;
        curr = curr->next;
    }

    if (remove && best) {
        if (best_prev) {
            best_prev->next = best->next;
        } else {
            job_queue_head = best->next;
        }
        best->next = NULL;
    }
    return best;
}

// --- Execution Logic ---

void run_shell_job(Job *job) {
    safe_log("(%d) started (-1)\n", job->client_id);
    
    char *output = execute_pipeline(job->command, job->client_fd);
    
    if(output && strlen(output) > 0) {
        safe_log("[%d]<<< %lu bytes sent\n", job->client_id, strlen(output));
    }
    
    send_line(job->client_fd, output ? output : "");
    if(output) free(output);
    
    safe_log("(%d) ended (-1)\n", job->client_id);
}

void run_demo_job(Job *job) {
    int quantum = (job->rounds_run == 0) ? SCHED_QUANTUM_1 : SCHED_QUANTUM_REST; 
    int time_slice = 0;

    safe_log("(%d) started (%d)\n", job->client_id, job->remaining_time);
    if(job->rounds_run > 0) {
         safe_log("(%d) running (%d)\n", job->client_id, job->remaining_time);
    }

    while (time_slice < quantum && job->remaining_time > 0) {
        sleep(1); // Simulate work [cite: 17]
        
        int current_progress = job->initial_burst - job->remaining_time + 1;
        char buf[64];
        snprintf(buf, sizeof(buf), "Demo %d/%d", current_progress, job->initial_burst);
        send_line(job->client_fd, buf);

        job->remaining_time--;
        time_slice++;

        // Preemption Check 
        pthread_mutex_lock(&queue_mutex);
        Job *potential_better = select_job(0);
        pthread_mutex_unlock(&queue_mutex);

        if (potential_better && potential_better != job) {
            if (potential_better->initial_burst == -1 || 
                potential_better->remaining_time < job->remaining_time) {
                break; // Preempt
            }
        }
    }

    job->rounds_run++;

    if (job->remaining_time == 0) {
        safe_log("(%d) ended (0)\n", job->client_id);
    } else {
        safe_log("(%d) waiting (%d)\n", job->client_id, job->remaining_time);
    }
}

void *scheduler_loop(void *arg) {
    (void)arg;
    while (!g_stop) {
        pthread_mutex_lock(&queue_mutex);
        while (job_queue_head == NULL && !g_stop) {
            pthread_cond_wait(&queue_cond, &queue_mutex);
        }
        if (g_stop) { pthread_mutex_unlock(&queue_mutex); break; }
        pthread_mutex_unlock(&queue_mutex);

        pthread_mutex_lock(&queue_mutex);
        Job *job = select_job(1); 
        pthread_mutex_unlock(&queue_mutex);

        if (!job) continue;

        if (job->type == JOB_CMD) {
            run_shell_job(job);
            send_line(job->client_fd, "<<EOF>>"); 
            free(job->command);
            free(job);
        } else {
            run_demo_job(job);
            
            if (job->remaining_time > 0) {
                pthread_mutex_lock(&queue_mutex);
                job->next = job_queue_head;
                job_queue_head = job;
                pthread_mutex_unlock(&queue_mutex);
            } else {
                send_line(job->client_fd, "<<EOF>>");
                free(job->command);
                free(job);
            }
        }
    }
    return NULL;
}

typedef struct { int fd; int id; } client_t;

void *handle_client_input(void *arg) {
    client_t *info = (client_t*)arg;
    int client_fd = info->fd;
    int client_id = info->id;
    free(info);

    char buffer[MAX_CMD_LENGTH];

    while (!g_stop) {
        int bytes = receive_line(client_fd, buffer, sizeof(buffer));
        if (bytes <= 0) break; 
        if (strcmp(buffer, "exit") == 0) break;
        if (strlen(buffer) == 0) continue;

        safe_log("[%d] >>> %s\n", client_id, buffer);

        Job *job = malloc(sizeof(Job));
        job->id = ++job_id_counter;
        job->client_id = client_id;
        job->client_fd = client_fd;
        job->command = xstrdup(buffer);
        job->rounds_run = 0;
        job->next = NULL;

        // Parse demo command
        if (strncmp(buffer, "demo", 4) == 0 || strncmp(buffer, "./demo", 6) == 0 || strncmp(buffer, "/demo", 5) == 0) {
             job->type = JOB_DEMO;
             char *space = strchr(buffer, ' ');
             if (space) job->initial_burst = atoi(space + 1);
             else job->initial_burst = 5; 
             job->remaining_time = job->initial_burst;
        } else {
            job->type = JOB_CMD;
            job->initial_burst = -1; 
            job->remaining_time = 0; 
        }

        add_job(job);
    }
    
    close_socket(client_fd);
    safe_log("[%d] <<< client disconnected\n", client_id);
    return NULL;
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    
    // FIXED: Use standard function pointer, not lambda
    signal(SIGINT, handle_sigint);
    signal(SIGPIPE, SIG_IGN);

    server_fd = create_server_socket(8080);
    if(server_fd < 0) exit(1);

    printf("-------------------------\n");
    printf("| Hello, Server Started |\n");
    printf("-------------------------\n");

    pthread_t sched_tid;
    pthread_create(&sched_tid, NULL, scheduler_loop, NULL);

    while(!g_stop) {
        struct sockaddr_in addr;
        int cf = accept_client_connection(server_fd, &addr);
        if (cf < 0) continue;

        if (g_stop) { close(cf); break; }

        int cid = ++client_id_counter;
        safe_log("[%d] <<< client connected\n", cid);

        client_t *c = malloc(sizeof(client_t));
        c->fd = cf; c->id = cid;
        
        pthread_t tid;
        pthread_create(&tid, NULL, handle_client_input, c);
        pthread_detach(tid);
    }
    
    return 0;
}
