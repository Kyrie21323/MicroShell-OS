#include "net.h"
#include "parse.h"
#include "exec.h"
#include "util.h" // <--- ADDED THIS LINE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#define MAX_CMD_LENGTH 1024 
#define MAX_ARGS 64         

static int server_fd = -1;
static int client_fd = -1;

void signal_handler(int sig){
    (void)sig; // <--- ADDED THIS LINE to fix the unused parameter warning
    printf("\n[INFO] Shutting down server...\n");
    if(client_fd >= 0){
        close_socket(client_fd);
    }
    if(server_fd >= 0){
        close_socket(server_fd);
    }
    exit(0);
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

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    server_fd = create_server_socket(port);
    if(server_fd < 0){
        fprintf(stderr, "Error: Failed to create server socket\n");
        exit(1);
    }

    printf("[INFO] Server started on port %d, waiting for client connections...\n", port);

    while (1){
        client_fd = accept_client_connection(server_fd);
        if(client_fd < 0){
            fprintf(stderr, "Error: Failed to accept client connection\n");
            continue;
        }

        printf("[INFO] Client connected.\n");

        while(1){
            int bytes_received = receive_line(client_fd, cmd_buffer, sizeof(cmd_buffer));
            
            if(bytes_received <= 0){
                if(bytes_received == 0){
                    printf("[INFO] Client disconnected\n");
                } else {
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
                continue;
            }
            
            printf("[EXECUTING] Executing command: \"%s\"\n", cmd_buffer);

            char *output_str = NULL;
            // Use a copy for parsing/execution because strtok_r in execute_pipeline modifies it
            char cmd_copy[MAX_CMD_LENGTH];
            strncpy(cmd_copy, cmd_buffer, MAX_CMD_LENGTH);

            if(strchr(cmd_copy, '|') != NULL){
                output_str = execute_pipeline(cmd_copy);
            } else {
                char *args[MAX_ARGS];
                char *inputFile, *outputFile, *errorFile;
                if(parse_command(cmd_copy, args, &inputFile, &outputFile, &errorFile, 0) == 0){
                    output_str = execute_command(args, inputFile, outputFile, errorFile);
                    
                    for(int i = 0; args[i] != NULL; i++) free(args[i]);
                    if(inputFile) free(inputFile);
                    if(outputFile) free(outputFile);
                    if(errorFile) free(errorFile);
                } else {
                    output_str = xstrdup("Error: Command parsing failed.\n");
                }
            }
            
            // Log output before sending to client, as per project requirements
            if (output_str && strlen(output_str) > 0) {
                 // Check if the last character is a newline, if not, the formatting could be messy
                int len = strlen(output_str);
                if (output_str[len - 1] == '\n') {
                    printf("[OUTPUT] Sending output to client:\n%s", output_str);
                } else {
                    printf("[OUTPUT] Sending output to client:\n%s\n", output_str);
                }
            } else {
                 printf("[OUTPUT] Sending empty output to client.\n");
            }


            // Send the result back to the client
            if (send_line(client_fd, output_str ? output_str : "") < 0) {
                perror("Error sending output to client");
            }

            if (output_str) {
                free(output_str);
            }
        }

        close_socket(client_fd);
        client_fd = -1;
        printf("[INFO] Client session ended\n");
    }

    close_socket(server_fd);
    return 0;
}
