#include "redir.h"
#include "errors.h"
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

int setup_redirection(const char *filename, int flags, int target_fd) {
    int fd = open(filename, flags, 0644);
    if(fd < 0){
        // Only print "File not found." for input files (file doesn't exist)
        // For output/error files with O_CREAT, open should not fail
        if(target_fd == STDIN_FILENO){
            write(STDERR_FILENO, ERR_FILE_NOT_FOUND, strlen(ERR_FILE_NOT_FOUND));
        }
        // For output/error files, we don't print any error (spec says no perror for user errors)
        return -1;
    }

    if(dup2(fd, target_fd) < 0){
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}
