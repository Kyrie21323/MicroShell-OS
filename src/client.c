#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#define MAX_CMD_LENGTH 1024 
#define MAX_RESPONSE_LENGTH 65536 // 64KB buffer for server response

static int client_fd = -1;

void signal_handler(int sig){
    (void)sig; // Unused parameter
    printf("\n[INFO] Shutting down client...\n");
    if(client_fd >= 0){
        close_socket(client_fd);
    }
    exit(0);
}

int main(int argc, char *argv[]){
    char *server_ip;
    int port;
    char cmd_buffer[MAX_CMD_LENGTH];
    char response_buffer[MAX_RESPONSE_LENGTH];

    if(argc != 3){
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        exit(1);
    }

    server_ip = argv[1];
    port = atoi(argv[2]);
    
    if(port <= 0 || port > 65535){
        fprintf(stderr, "Error: Invalid port number\n");
        exit(1);
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    client_fd = create_client_socket(server_ip, port);
    if(client_fd < 0){
        fprintf(stderr, "Error: Failed to connect to server\n");
        exit(1);
    }

    // This message is now printed by create_client_socket
    // printf("[INFO] Connected to server successfully\n");

    while(1){
        printf("$ ");
        fflush(stdout);

        if(fgets(cmd_buffer, sizeof(cmd_buffer), stdin) == NULL){
            printf("\n[INFO] End of input, sending exit command...\n");
            strcpy(cmd_buffer, "exit"); // Send exit command on Ctrl+D
        }

        cmd_buffer[strcspn(cmd_buffer, "\n")] = '\0';

        if (strlen(cmd_buffer) == 0 && feof(stdin)) {
            // Handle case where Ctrl+D was on an empty line
            break; 
        }

        if(strlen(cmd_buffer) == 0){
            continue;
        }

        if(send_line(client_fd, cmd_buffer) < 0){
            perror("Error sending command. Server may have disconnected");
            break;
        }

        if(strcmp(cmd_buffer, "exit") == 0){
            printf("[INFO] Exiting client...\n");
            break;
        }
        
        // Wait for and receive the server's response
        int bytes_received = receive_line(client_fd, response_buffer, sizeof(response_buffer));
        if (bytes_received < 0) {
            perror("Error receiving response from server");
            break;
        } else {
            // Print the response from the server (even if empty length 0 message)
            // bytes_received == 0 means empty message, not connection closed
            printf("%s", response_buffer);
            fflush(stdout);
        }
    }

    close_socket(client_fd);
    return 0;
}
