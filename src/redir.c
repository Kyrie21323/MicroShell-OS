#include "redir.h"
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int setup_redirection(const char *filename, int flags, int target_fd) {
    int fd = open(filename, flags, 0644);
    if(fd < 0){
        if(target_fd == STDIN_FILENO){
            fprintf(stderr, "File not found: %s\n", filename);
        }else{
            perror("open");
        }
        return -1;
    }

    if(dup2(fd, target_fd) < 0){
        perror("dup2 failed");
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}
