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
Job *current_running_job = NULL;  // Track currently running job for preemption checks
int last_job_id = -1;  // Track last job ID to enforce "no same job twice consecutively" rule
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

// Timeline tracking for final summary
typedef struct TimelineEntry {
    int client_id;
    int elapsed_time;  // Changed from remaining_time to elapsed_time for global time tracking
    struct TimelineEntry *next;
} TimelineEntry;
static TimelineEntry *timeline_head = NULL;
static TimelineEntry *timeline_tail = NULL;
static int had_preemption = 0;  // Track if any preemption occurred (only print summary if true)
static int global_time = 0;  // Track global elapsed time for timeline summary

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

// SRJF Selection Algorithm with "no same job twice consecutively" rule
// If exclude is non-NULL, excludes that job from selection (for preemption checks)
// If last_job_id >= 0, avoids selecting that job ID unless it's the only option
Job *select_job(int remove, Job *exclude, int last_job_id) {
    if (!job_queue_head) return NULL;

    // First pass: Find shortest remaining time among non-excluded demo jobs
    int shortest_time = -1;
    Job *curr = job_queue_head;
    while (curr) {
        if (curr != exclude && curr->initial_burst != -1) {
            if (shortest_time == -1 || curr->remaining_time < shortest_time) {
                shortest_time = curr->remaining_time;
            }
        }
        curr = curr->next;
    }
    
    // Second pass: Select best job according to priority rules
    curr = job_queue_head;
    Job *curr_prev = NULL;
    Job *best = NULL;
    Job *best_prev = NULL;
    Job *alternate_best = NULL;  // Alternative if best matches last_job_id
    Job *alternate_best_prev = NULL;
    
    while (curr) {
        if (curr == exclude) {
            curr_prev = curr;
            curr = curr->next;
            continue;
        }
        
        // Priority 1: Shell Commands (burst == -1) always selected first
        if (curr->initial_burst == -1) {
            if (!best || best->initial_burst != -1) {
                best = curr;
                best_prev = curr_prev;
            }
        }
        // Priority 2: Jobs with shortest remaining time (SJRF)
        else if (shortest_time >= 0 && curr->remaining_time == shortest_time) {
            // Check if this is a candidate for selection
            if (!best || best->initial_burst == -1 || 
                (best->initial_burst != -1 && best->remaining_time > shortest_time)) {
                // This job is better than current best
                if (last_job_id >= 0 && curr->id == last_job_id) {
                    // This matches last job - save as alternate, but look for different one
                    if (!alternate_best) {
                        alternate_best = curr;
                        alternate_best_prev = curr_prev;
                    }
                } else {
                    // This is different from last job - prefer it
                    best = curr;
                    best_prev = curr_prev;
                }
            } else if (best && best->initial_burst != -1 && 
                       best->remaining_time == shortest_time &&
                       last_job_id >= 0 && best->id == last_job_id &&
                       curr->id != last_job_id) {
                // Current best matches last_job_id, but this one doesn't - prefer this
                best = curr;
                best_prev = curr_prev;
            }
        }
        
        curr_prev = curr;
        curr = curr->next;
    }
    
    // If best matches last_job_id, check if we have alternate options
    if (best && last_job_id >= 0 && best->id == last_job_id) {
        // Check if there are other jobs with same shortest time
        curr = job_queue_head;
        int other_options = 0;
        while (curr) {
            if (curr != exclude && curr != best &&
                curr->initial_burst != -1 && 
                curr->remaining_time == shortest_time &&
                curr->id != last_job_id) {
                other_options = 1;
                break;
            }
            curr = curr->next;
        }
        
        // If other options exist, we should have found them above
        // But if we only have alternate_best, use it only if no other options
        if (!other_options && alternate_best && alternate_best->id == last_job_id) {
            // Only option is the same job - must select it (rule exception)
            best = alternate_best;
            best_prev = alternate_best_prev;
        } else if (other_options) {
            // There are other options - we should have selected one above
            // This shouldn't happen, but if it does, keep current best
        }
    }

    // Fallback: If best is still NULL, we need to pick ANY runnable job
    // This prevents returning NULL when jobs exist in the queue
    if (!best) {
        // Try to find any job that doesn't match last_job_id
        curr = job_queue_head;
        curr_prev = NULL;
        while (curr) {
            if (curr != exclude && (last_job_id < 0 || curr->id != last_job_id)) {
                best = curr;
                best_prev = curr_prev;
                break;
            }
            curr_prev = curr;
            curr = curr->next;
        }
        
        // If still NULL, just pick the first non-excluded job (even if it matches last_job_id)
        if (!best) {
            curr = job_queue_head;
            curr_prev = NULL;
            while (curr) {
                if (curr != exclude) {
                    best = curr;
                    best_prev = curr_prev;
                    break;
                }
                curr_prev = curr;
                curr = curr->next;
            }
        }
    }

    if (!best) return NULL;  // No valid job found (should never happen if queue has jobs)

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

// Safe send that handles client disconnects gracefully
static int safe_send_line(int client_fd, const char *line) {
    if (client_fd < 0) return -1;
    int result = send_line(client_fd, line);
    // If send fails, client likely disconnected - don't crash
    if (result < 0) {
        // Client disconnected, but continue execution for logging
    }
    return result;
}

void run_shell_job(Job *job) {
    safe_log("(%d) started (-1)\n", job->client_id);
    
    char *output = execute_pipeline(job->command, job->client_fd);
    
    // Track bytes sent for shell output
    if(output && strlen(output) > 0) {
        job->bytes_sent += strlen(output);
    }
    
    safe_send_line(job->client_fd, output ? output : "");
    if(output) free(output);
    
    // Log bytes summary before ended
    if(job->bytes_sent > 0) {
        safe_log("[%d]<<< %d bytes sent\n", job->client_id, job->bytes_sent);
    }
    safe_log("(%d) ended (-1)\n", job->client_id);
}

void run_demo_job(Job *job) {
    int quantum = (job->rounds_run == 0) ? SCHED_QUANTUM_1 : SCHED_QUANTUM_REST; 
    int time_slice = 0;
    int was_preempted_by_shell = 0;  // Only track shell preemption now

    // Logging: first run prints "started", subsequent runs print "running"
    if (job->rounds_run == 0) {
        safe_log("(%d) started (%d)\n", job->client_id, job->remaining_time);
    } else {
        safe_log("(%d) running (%d)\n", job->client_id, job->remaining_time);
    }

    while (time_slice < quantum && job->remaining_time > 0) {
        sleep(1); // Simulate work
        
        int current_progress = job->initial_burst - job->remaining_time + 1;
        char buf[64];
        snprintf(buf, sizeof(buf), "Demo %d/%d", current_progress, job->initial_burst);
        safe_send_line(job->client_fd, buf);
        
        // Track bytes sent (each Demo line)
        job->bytes_sent += strlen(buf);

        job->remaining_time--;
        time_slice++;

        // Preemption Check: Only shell commands preempt demo jobs mid-quantum
        // Demo jobs do NOT preempt each other mid-quantum (removed remaining_time comparison)
        pthread_mutex_lock(&queue_mutex);
        
        Job *potential_better = select_job(0, job, last_job_id);
        
        // Only shell commands can preempt demo jobs
        if (potential_better && potential_better->initial_burst == -1) {
            // Shell command always preempts demo jobs
            was_preempted_by_shell = 1;
            had_preemption = 1;  // Mark that preemption occurred
            pthread_mutex_unlock(&queue_mutex);
            break; // Preempt
        }
        
        pthread_mutex_unlock(&queue_mutex);
    }

    job->rounds_run++;

    // Logging: use "preempted" only for shell preemption, "waiting" for quantum end
    // Note: "ended" logging moved to scheduler_loop to print after bytes summary
    if (was_preempted_by_shell) {
        safe_log("(%d) preempted (%d)\n", job->client_id, job->remaining_time);
    } else if (job->remaining_time > 0) {
        safe_log("(%d) waiting (%d)\n", job->client_id, job->remaining_time);
    }
    // If remaining_time == 0, we don't log here; scheduler_loop will log bytes + ended
}

// Add entry to timeline for final summary
static void add_timeline_entry(int client_id, int elapsed_time) {
    TimelineEntry *entry = malloc(sizeof(TimelineEntry));
    if (!entry) return;
    entry->client_id = client_id;
    entry->elapsed_time = elapsed_time;
    entry->next = NULL;
    
    if (!timeline_head) {
        timeline_head = timeline_tail = entry;
    } else {
        timeline_tail->next = entry;
        timeline_tail = entry;
    }
}

// Clear timeline entries without printing (for non-preemptive scenarios)
static void clear_timeline(void) {
    TimelineEntry *curr = timeline_head;
    while (curr) {
        TimelineEntry *next = curr->next;
        free(curr);
        curr = next;
    }
    timeline_head = timeline_tail = NULL;
}

// Print final timeline summary when queue becomes empty
static void print_timeline_summary(void) {
    if (!timeline_head) return;
    
    TimelineEntry *curr = timeline_head;
    printf("\n");
    while (curr) {
        printf("P%d-(%d)", curr->client_id, curr->elapsed_time);
        if (curr->next) printf("-");
        curr = curr->next;
    }
    printf("\n");
    
    // Free timeline entries
    clear_timeline();
}

void *scheduler_loop(void *arg) {
    (void)arg;
    while (!g_stop) {
        pthread_mutex_lock(&queue_mutex);
        while (job_queue_head == NULL && !g_stop) {
            // When queue becomes empty, print timeline summary if we have any timeline data
            if (timeline_head != NULL) {
                print_timeline_summary();
            }
            pthread_cond_wait(&queue_cond, &queue_mutex);
        }
        if (g_stop) { 
            // On shutdown, print timeline summary if we have any timeline data
            if (timeline_head != NULL) {
                print_timeline_summary();
            }
            pthread_mutex_unlock(&queue_mutex); 
            break; 
        }
        pthread_mutex_unlock(&queue_mutex);

        pthread_mutex_lock(&queue_mutex);
        // FIXED: Pass last_job_id to enforce "no same job twice consecutively" rule
        Job *job = select_job(1, NULL, last_job_id);
        // FIXED: Track currently running job for preemption checks
        current_running_job = job;
        if (job) {
            // FIXED: Update last_job_id to track which job just ran
            last_job_id = job->id;
        }
        pthread_mutex_unlock(&queue_mutex);

        if (!job) continue;

        if (job->type == JOB_CMD) {
            run_shell_job(job);
            safe_send_line(job->client_fd, "<<EOF>>"); 
            // Shell jobs execute instantly for timeline purposes (assume negligible time)
            // We don't increment global_time for shell commands in this model
            // But we still add a timeline entry if needed
            free(job->command);
            free(job);
        } else {
            int remaining_before = job->remaining_time;
            run_demo_job(job);
            int remaining_after = job->remaining_time;
            
            // Calculate time spent in this quantum and update global time
            int time_spent = remaining_before - remaining_after;
            global_time += time_spent;
            
            // Record timeline entry with global elapsed time
            add_timeline_entry(job->client_id, global_time);
            
            if (job->remaining_time > 0) {
                pthread_mutex_lock(&queue_mutex);
                job->next = job_queue_head;
                job_queue_head = job;
                pthread_mutex_unlock(&queue_mutex);
            } else {
                // Job completed - log bytes summary and ended
                if (job->bytes_sent > 0) {
                    safe_log("[%d]<<< %d bytes sent\n", job->client_id, job->bytes_sent);
                }
                safe_log("(%d) ended (0)\n", job->client_id, job->remaining_time);
                
                safe_send_line(job->client_fd, "<<EOF>>");
                free(job->command);
                free(job);
            }
        }
        
        // FIXED: Clear running job after execution completes (whether finished or preempted)
        pthread_mutex_lock(&queue_mutex);
        current_running_job = NULL;
        pthread_mutex_unlock(&queue_mutex);
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
        job->bytes_sent = 0;  // Initialize bytes counter
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
    
    // FIXED: Wait for scheduler thread to exit cleanly
    pthread_join(sched_tid, NULL);
    
    return 0;
}
