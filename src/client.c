#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#define MAX_CMD_LENGTH 1024 
#define MAX_RESPONSE_LENGTH 65536

static int client_fd = -1;

void signal_handler(int sig){
    (void)sig; 
    printf("\n[INFO] Shutting down client...\n");
    if(client_fd >= 0) close_socket(client_fd);
    exit(0);
}

int main(int argc, char *argv[]){
    char *server_ip = "127.0.0.1";
    int port = 8080;
    char cmd_buffer[MAX_CMD_LENGTH];
    char response_buffer[MAX_RESPONSE_LENGTH];
    (void)argc; (void)argv;

    signal(SIGINT, signal_handler);

    client_fd = create_client_socket(server_ip, port);
    if(client_fd < 0){
        fprintf(stderr, "Error: Failed to connect\n");
        exit(1);
    }

    while(1){
        printf("$ ");
        fflush(stdout);

        if(fgets(cmd_buffer, sizeof(cmd_buffer), stdin) == NULL) break;
        cmd_buffer[strcspn(cmd_buffer, "\n")] = '\0';
        if(strlen(cmd_buffer) == 0) continue;

        if(send_line(client_fd, cmd_buffer) < 0) break;
        if(strcmp(cmd_buffer, "exit") == 0) break;
        
        // Loop to receive multi-line output until server says "CMD_DONE"
        while(1) {
            int bytes = receive_line(client_fd, response_buffer, sizeof(response_buffer));
            if(bytes <= 0) break; // Error or disconnect
            
            // Check for our custom End-Of-Transmission marker
            if(strcmp(response_buffer, "<<EOF>>") == 0) {
                break;
            }
            
            printf("%s\n", response_buffer); // Print output line
        }
    }

    close_socket(client_fd);
    return 0;
}
