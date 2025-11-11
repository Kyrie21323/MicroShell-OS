#include "net.h"
#include "parse.h"
#include "exec.h"
#include "util.h"
#include "errors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdbool.h>
#include <pthread.h>      // For threads
#include <arpa/inet.h>    // For inet_ntoa
#include <netinet/in.h>   // For sockaddr_in

#define MAX_CMD_LENGTH 1024 
#define MAX_ARGS 64         
#define LOG_BUFFER_SIZE 2048 // For building log prefixes
#define INET_ADDRSTRLEN 16   // For IPv4 address string

static int server_fd = -1;

// Global flag to signal shutdown
static volatile sig_atomic_t g_stop = 0;

// Mutex to protect client_count
static pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
static int client_count = 0;

/**
 * @brief Struct to pass data to a new client thread
 */
typedef struct {
    int client_fd;                // Client's socket file descriptor
    int client_id;                // Unique ID for the client
    char client_ip[INET_ADDRSTRLEN]; // Client's IP address
    int client_port;              // Client's port
} client_data_t;

/**
 * @brief Signal handler for SIGINT (Ctrl+C)
 */
void sigint_handler(int sig){
    (void)sig;
    printf("\n[INFO] SIGINT received, stopping...\n");
    g_stop = 1;
    if(server_fd >= 0){
        // This helps unblock the accept() call
        shutdown(server_fd, SHUT_RDWR);
    }
}

/**
 * @brief Signal handler for SIGCHLD (reaps zombie processes)
 */
void sigchld_handler(int sig){
    (void)sig;
    int status;
    while(waitpid(-1, &status, WNOHANG) > 0){
        // Reap all zombie children
    }
}

/**
 * @brief Checks if an output string is a known error message.
 * @param output The string to check.
 * @return 1 if it's an error, 0 otherwise.
 */
static bool is_error(const char *output_str) {
    if (!output_str) return false;
    if (strstr(output_str, "not found") || strstr(output_str, "not specified") ||
        strstr(output_str, "missing") || strstr(output_str, "Empty command") ||
        strstr(output_str, "Unclosed quotes") || strstr(output_str, "Invalid") ||
        strstr(output_str, "failed") || strstr(output_str, "Error:") ||
        strcmp(output_str, ERR_PIPE_CMD) == 0) {
        return true;
    }
    return false;
}

/**
 * @brief The main function for each client thread.
 * * This function handles all communication and command execution
 * for a single connected client[cite: 9].
 * * @param arg A void pointer to a client_data_t struct.
 * @return NULL
 */
void *handle_client(void *arg) {
    client_data_t *data = (client_data_t *)arg;
    
    // Copy data from the struct, then free it
    int client_fd = data->client_fd;
    int client_id = data->client_id;
    char client_ip[INET_ADDRSTRLEN];
    strncpy(client_ip, data->client_ip, INET_ADDRSTRLEN);
    int client_port = data->client_port;
    
    // We've copied the data, so we can free the struct passed from main
    free(data);

    char cmd_buffer[MAX_CMD_LENGTH];
    char log_prefix[LOG_BUFFER_SIZE];
    
    // Create the client-specific log prefix for all messages
    // Format: [Client #X - IP:PORT]
    snprintf(log_prefix, sizeof(log_prefix), "[Client #%d - %s:%d]", client_id, client_ip, client_port);

    while(!g_stop){
        int bytes_received = receive_line(client_fd, cmd_buffer, sizeof(cmd_buffer));
        
        if(bytes_received <= 0){
            if(bytes_received == 0){
                printf("[INFO] %s Client disconnected (EOF).\n", log_prefix);
            } else if(errno != EINTR) { // Don't log error on signal interrupt
                perror("Error receiving command");
            }
            break;
        }

        // Log received command 
        printf("[RECEIVED] %s Received command: \"%s\"\n", log_prefix, cmd_buffer);

        if(strcmp(cmd_buffer, "exit") == 0){
            // Log exit command 
            printf("[INFO] %s Client requested disconnect. Closing connection.\n", log_prefix);
            break;
        }
        if(strlen(cmd_buffer) == 0){
            // Send back an empty response for an empty command
            send_line(client_fd, "");
            continue;
        }
        
        // Log execution 
        printf("[EXECUTING] %s Executing command: \"%s\"\n", log_prefix, cmd_buffer);

        char *output_str = NULL;
        char cmd_copy[MAX_CMD_LENGTH];
        strncpy(cmd_copy, cmd_buffer, MAX_CMD_LENGTH);
        cmd_copy[MAX_CMD_LENGTH - 1] = '\0'; // Ensure null-termination

        // --- Execute Command ---
        if(strchr(cmd_copy, '|') != NULL){
            output_str = execute_pipeline(cmd_copy, client_fd);
        } else {
            char *args[MAX_ARGS];
            char *inputFile, *outputFile, *errorFile;
            int parse_res = parse_command(cmd_copy, args, &inputFile, &outputFile, &errorFile, 0);
            
            if(parse_res == PARSE_SUCCESS){
                output_str = execute_command(args, inputFile, outputFile, errorFile);
                
                for(int i = 0; args[i] != NULL; i++) free(args[i]);
                if(inputFile) free(inputFile);
                if(outputFile) free(outputFile);
                if(errorFile) free(errorFile);
            } else {
                // Handle parsing errors
                 switch (parse_res) {
                    case PARSE_ERR_SYNTAX:
                    case PARSE_ERR_EMPTY_CMD_REDIR:
                        output_str = xstrdup("Error: Invalid command syntax.\n"); break;
                    case PARSE_ERR_TOO_MANY_ARGS:
                        output_str = xstrdup("Error: Too many arguments.\n"); break;
                    case PARSE_ERR_NO_INPUT_FILE:
                        output_str = xstrdup(ERR_INPUT_NOT_SPECIFIED); break;
                    case PARSE_ERR_NO_OUTPUT_FILE:
                        output_str = xstrdup(ERR_OUTPUT_NOT_SPECIFIED); break;
                    case PARSE_ERR_NO_OUTPUT_FILE_AFTER:
                        output_str = xstrdup(ERR_OUT_AFTER); break;
                    case PARSE_ERR_NO_ERROR_FILE:
                        output_str = xstrdup(ERR_ERROR_NOT_SPECIFIED); break;
                    default:
                        output_str = xstrdup("Error: Command parsing failed.\n");
                }
            }
        }
        // --- End Execution ---
        
        // Ensure output is not NULL
        if (!output_str) {
            output_str = xstrdup("");
        }

        // Check if the result was an error
        if (is_error(output_str)) {
            // Log error message 
            // Remove trailing newline for cleaner single-line log
            char log_msg[MAX_CMD_LENGTH];
            strncpy(log_msg, output_str, sizeof(log_msg) - 1);
            log_msg[sizeof(log_msg) - 1] = '\0';
            log_msg[strcspn(log_msg, "\n")] = 0; // Remove trailing newline
            
            printf("[ERROR] %s %s\n", log_prefix, log_msg);
            // Log sending error message [cite: 10]
            printf("[OUTPUT] %s Sending error message to client: \"%s\"\n", log_prefix, log_msg);
        
        } else if (strlen(output_str) > 0) {
            // Log sending standard output [cite: 10]
            printf("[OUTPUT] %s Sending output to client:\n%s", log_prefix, output_str);
            // Add a newline to log if output didn't have one
            if(output_str[strlen(output_str)-1] != '\n') {
                printf("\n");
            }
        } else {
            // Don't log anything for empty, non-error output (e.g., successful touch)
            // But log that we are sending the (empty) response
            printf("[OUTPUT] %s Sending output to client: \n", log_prefix);
        }

        // Send the result (error or success)
        if (send_line(client_fd, output_str) < 0) {
            perror("Error sending output to client");
        }

        free(output_str);
    }

    close_socket(client_fd);
    // Log final disconnect message [cite: 10]
    printf("[INFO] Client #%d disconnected.\n", client_id);
    return NULL;
}

/**
 * @brief Main server function
 */
int main(int argc, char *argv[]) {
    int port;

    if(argc != 2){
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    port = atoi(argv[1]);
    if(port <= 0 || port > 65535){
        fprintf(stderr, "Error: Invalid port number\n");
        exit(1);
    }

    signal(SIGINT, sigint_handler);
    signal(SIGCHLD, sigchld_handler);
    signal(SIGPIPE, SIG_IGN);  // Ignore SIGPIPE

    server_fd = create_server_socket(port);
    if(server_fd < 0){
        fprintf(stderr, "Error: Failed to create server socket\n");
        exit(1);
    }

    printf("[INFO] Server started, waiting for client connections...\n");

    while (!g_stop){
        struct sockaddr_in client_addr; // To store client's IP and port
        int client_fd = accept_client_connection(server_fd, &client_addr);
        
        if(client_fd < 0){
            if(g_stop){ break; } // Normal shutdown
            if(errno == EINTR){ continue; } // Interrupted by signal
            fprintf(stderr, "Error: Failed to accept client connection\n");
            continue;
        }

        // --- New Client Connected ---
        
        // Malloc data struct to pass to thread
        client_data_t *client_data = malloc(sizeof(client_data_t));
        if(!client_data){
            perror("malloc failed");
            close_socket(client_fd);
            continue;
        }

        // Get client info
        client_data->client_fd = client_fd;
        client_data->client_port = ntohs(client_addr.sin_port);
        strncpy(client_data->client_ip, inet_ntoa(client_addr.sin_addr), INET_ADDRSTRLEN);

        // Safely assign a new client ID
        pthread_mutex_lock(&client_mutex);
        client_count++;
        client_data->client_id = client_count;
        pthread_mutex_unlock(&client_mutex);

        // Log the new connection as required [cite: 10]
        printf("[INFO] Client #%d connected from %s:%d. Assigned to Thread-%d.\n",
               client_data->client_id,
               client_data->client_ip,
               client_data->client_port,
               client_data->client_id);

        // Create the new thread
        pthread_t thread_id;
        if(pthread_create(&thread_id, NULL, handle_client, (void *)client_data) != 0){
            perror("pthread_create failed");
            free(client_data);
            close_socket(client_fd);
        }

        // Detach the thread so its resources are freed on exit
        pthread_detach(thread_id);
    }

    if(server_fd >= 0){
        close_socket(server_fd);
    }
    
    printf("[INFO] Server socket closed. Bye.\n");
    return 0;
}
