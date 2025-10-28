#ifndef NET_H
#define NET_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>


#define MAX_BUFFER_SIZE 1024

int create_server_socket(int port);
int accept_client_connection(int server_fd);
int create_client_socket(const char *server_ip, int port);
int send_line(int socket_fd, const char *line);
int receive_line(int socket_fd, char *buffer, int buffer_size);
void close_socket(int socket_fd);

#endif
