#ifndef NET_H
#define NET_H

#include <sys/socket.h>
#include <netinet/in.h> // <-- Added for sockaddr_in
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>


#define MAX_BUFFER_SIZE 1024

int create_server_socket(int port);
// Modified to accept a pointer to store client address info
int accept_client_connection(int server_fd, struct sockaddr_in *client_addr);
int create_client_socket(const char *server_ip, int port);
int send_line(int socket_fd, const char *line);
int receive_line(int socket_fd, char *buffer, int buffer_size);
void close_socket(int socket_fd);

#endif
