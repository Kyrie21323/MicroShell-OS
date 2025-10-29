#include "net.h"
#include "parse.h"
#include "exec.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdbool.h>

#define MAX_CMD_LENGTH 1024 
#define MAX_ARGS 64         

static int server_fd = -1;
static int client_fd = -1;

static volatile sig_atomic_t g_stop = 0;

void sigint_handler(int sig){
    (void)sig;
    printf("\n[INFO] SIGINT received, stopping...\n");
    g_stop = 1;
    if(server_fd >= 0){
        // This helps unblock the accept() call
        shutdown(server_fd, SHUT_RDWR);
    }
}

void sigchld_handler(int sig){
    (void)sig;
    int status;
    while(waitpid(-1, &status, WNOHANG) > 0){
        // Reap all zombie children
    }
}

int main(int argc, char *argv[]) {
    int port;
    char cmd_buffer[MAX_CMD_LENGTH];

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

    server_fd = create_server_socket(port);
    if(server_fd < 0){
        fprintf(stderr, "Error: Failed to create server socket\n");
        exit(1);
    }

    printf("[INFO] Server started on port %d, waiting for client connections...\n", port);

    while (!g_stop){
        client_fd = accept_client_connection(server_fd);
        if(client_fd < 0){
            if(g_stop){
                // Normal shutdown path
                break;
            }
            if(errno == EINTR){
                // Interrupted by signal, loop will check g_stop
                continue;
            }
            fprintf(stderr, "Error: Failed to accept client connection\n");
            continue;
        }

        printf("[INFO] Client connected.\n");

        // The Fix: This loop now checks the g_stop flag
        while(!g_stop){
            
            int bytes_received = receive_line(client_fd, cmd_buffer, sizeof(cmd_buffer));
            
            if(bytes_received <= 0){
                if(bytes_received == 0){
                    printf("[INFO] Client disconnected.\n");
                } else {
                    if(errno == EINTR){
                        // Interrupted by signal, loop will check g_stop
                        continue;
                    }
                    perror("Error receiving command");
                }
                break;
            }

            printf("[RECEIVED] Received command: \"%s\" from client.\n", cmd_buffer);

            if(strcmp(cmd_buffer, "exit") == 0){
                printf("[INFO] Client requested exit\n");
                break;
            }
            if(strlen(cmd_buffer) == 0){
                // Send back an empty response for an empty command
                send_line(client_fd, "");
                continue;
            }
            
            printf("[EXECUTING] Executing command: \"%s\"\n", cmd_buffer);

            char *output_str = NULL;
            char cmd_copy[MAX_CMD_LENGTH];
            strncpy(cmd_copy, cmd_buffer, MAX_CMD_LENGTH);

            if(strchr(cmd_copy, '|') != NULL){
                output_str = execute_pipeline(cmd_copy);
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
                     switch (parse_res) {
                        case PARSE_ERR_SYNTAX:
                        case PARSE_ERR_EMPTY_CMD_REDIR:
                            output_str = xstrdup("Error: Invalid command syntax.\n"); break;
                        case PARSE_ERR_TOO_MANY_ARGS:
                            output_str = xstrdup("Error: Too many arguments.\n"); break;
                        case PARSE_ERR_NO_INPUT_FILE:
                            output_str = xstrdup("Input file not specified.\n"); break;
                        case PARSE_ERR_NO_OUTPUT_FILE:
                            output_str = xstrdup("Output file not specified.\n"); break;
                        case PARSE_ERR_NO_ERROR_FILE:
                            output_str = xstrdup("Error output file not specified.\n"); break;
                        default:
                            output_str = xstrdup("Error: Command parsing failed.\n");
                    }
                }
            }
            
            bool is_error = false;
            if (output_str) {
                // Better error checking
                if (strstr(output_str, "not found") || strstr(output_str, "not specified") ||
                    strstr(output_str, "Invalid") || strstr(output_str, "failed") ||
                    strstr(output_str, "Error:")) {
                    is_error = true;
                }
            }

            if (is_error) {
                char log_msg[MAX_CMD_LENGTH];
                strncpy(log_msg, output_str, sizeof(log_msg) - 1);
                log_msg[sizeof(log_msg) - 1] = '\0';
                // Remove trailing newline for logging
                log_msg[strcspn(log_msg, "\n")] = 0; 
                printf("[ERROR] %s\n", log_msg);
                printf("[OUTPUT] Sending error message to client: \"%s\"\n", log_msg);
            } else if (output_str && strlen(output_str) > 0) {
                 int len = strlen(output_str);
                if (output_str[len - 1] == '\n') {
                    printf("[OUTPUT] Sending output to client:\n%s", output_str);
                } else {
                    printf("[OUTPUT] Sending output to client:\n%s\n", output_str);
                }
            } else {
                 printf("[OUTPUT] Sending empty output to client.\n");
            }

            if (send_line(client_fd, output_str ? output_str : "") < 0) {
                perror("Error sending output to client");
            }

            if (output_str) {
                free(output_str);
            }
        }

        close_socket(client_fd);
        client_fd = -1;
        printf("[INFO] Client session ended.\n");
    }

    if(server_fd >= 0){
        close_socket(server_fd);
    }
    
    printf("[INFO] Server socket closed. Bye.\n");
    return 0;
}
