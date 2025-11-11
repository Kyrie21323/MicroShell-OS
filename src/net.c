#include "net.h"

//creates and binds a server socket to the specified port, returns socket file descriptor on success, -1 on failure
int create_server_socket(int port){
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    //create socket file descriptor
    if((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0){
        perror("socket failed");
        return -1;
    }

    //set socket options to allow address reuse
    if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))){
        perror("setsockopt failed");
        close(server_fd);
        return -1;
    }

    //configure address structure
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    //bind socket to address
    if(bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0){
        perror("bind failed");
        close(server_fd);
        return -1;
    }

    //start listening for connections
    if(listen(server_fd, 3) < 0){
        perror("listen failed");
        close(server_fd);
        return -1;
    }

    return server_fd;
}

//accepts a client connection on the server socket, blocks until a client connects, returns client socket file descriptor on success, -1 on failure
//Returns -1 on error (including EINTR), caller should check errno
// Modified to fill client_addr struct
int accept_client_connection(int server_fd, struct sockaddr_in *client_addr){
    int addrlen = sizeof(struct sockaddr_in);
    int client_fd;

    // Use the passed client_addr struct to store address info
    if((client_fd = accept(server_fd, (struct sockaddr *)client_addr, (socklen_t*)&addrlen)) < 0){
        // Don't print error for EINTR - it's handled by the caller
        if(errno != EINTR){
            perror("accept failed");
        }
        return -1;
    }
    
    return client_fd;
}

//creates and connects a client socket to the specified server, establishes connection to the server, returns socket file descriptor on success, -1 on failure
int create_client_socket(const char *server_ip, int port){
    int client_fd;
    struct sockaddr_in serv_addr;

    //create socket file descriptor
    if((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("socket creation failed");
        return -1;
    }

    //configure server address structure
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    //convert IP address from string to binary format
    if(inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0){
        perror("invalid address/address not supported");
        close(client_fd);
        return -1;
    }

    //connect to server
    if(connect(client_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
        perror("connection failed");
        close(client_fd);
        return -1;
    }

    printf("[INFO] Connected to server %s:%d\n", server_ip, port);
    return client_fd;
}

//sends a line of text over the socket, prefixed by its length.
int send_line(int socket_fd, const char *line){
    int len = strlen(line);
    
    // Send the line length first (as 4-byte network order integer)
    int32_t net_len = htonl(len);
    if(send(socket_fd, &net_len, sizeof(net_len), 0) != sizeof(net_len)){
        perror("send length failed");
        return -1;
    }

    // Send the actual line data
    if(len > 0) { // Only send if there is data
        if(send(socket_fd, line, len, 0) != len){
            perror("send data failed");
            return -1;
        }
    }

    return len;
}

//receives a line of text from the socket, reading the length prefix first.
//Returns the number of bytes received, or 0/negative on error/EOF.
//Handles empty messages (line_len == 0) correctly.
int receive_line(int socket_fd, char *buffer, int buffer_size){
    int32_t net_len;
    
    // Receive the line length
    ssize_t len_bytes = recv(socket_fd, &net_len, sizeof(net_len), MSG_WAITALL);
    if(len_bytes <= 0){
        return len_bytes; // Error or connection closed
    }

    int line_len = ntohl(net_len);
    
    if(line_len >= buffer_size){
        fprintf(stderr, "Received line too long (%d bytes) for buffer of size %d\n", line_len, buffer_size);
        // Still need to read the data off the socket to not corrupt the stream
        char discard_buffer[1024];
        int to_read = line_len;
        while(to_read > 0){
            int reading = (to_read > 1024) ? 1024 : to_read;
            if(recv(socket_fd, discard_buffer, reading, MSG_WAITALL) <= 0) break;
            to_read -= reading;
        }
        buffer[0] = '\0'; // Return empty string
        return -1; // Indicate error
    }

    // Receive the actual line data (if length > 0)
    if(line_len > 0){
        ssize_t data_bytes = recv(socket_fd, buffer, line_len, MSG_WAITALL);
        if(data_bytes != line_len){
            // This indicates a problem, as MSG_WAITALL should have returned the full amount
            buffer[0] = '\0';
            return -1;
        }
    }
    
    buffer[line_len] = '\0'; // Null terminate the received string (even for empty messages)
    return line_len;
}

//closes a socket connection
void close_socket(int socket_fd){
    if(socket_fd >= 0){
        close(socket_fd);
    }
}
