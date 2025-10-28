#include "exec.h"
#include "parse.h"
#include "redir.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_CMD_LENGTH 1024 
#define MAX_ARGS 64         
#define MAX_PIPES 10
#define OUTPUT_BUFFER_SIZE 4096

typedef struct {
    char *args[MAX_ARGS];
    char *inputFile;
    char *outputFile;
    char *errorFile;
} Stage;

static char* skip_whitespace(char *str){
    while(*str == ' ' || *str == '\t' || *str == '\n'){
        str++;
    }
    return str;
}

/**
 * @brief Reads all data from a file descriptor into a dynamically allocated string.
 * * @param fd The file descriptor to read from.
 * @return A dynamically allocated string containing the data. Caller must free it.
 */
static char* read_output_from_fd(int fd) {
    size_t capacity = OUTPUT_BUFFER_SIZE;
    size_t total_read = 0;
    char *buffer = malloc(capacity);
    if (!buffer) {
        perror("malloc");
        return NULL;
    }

    ssize_t bytes_read;
    while ((bytes_read = read(fd, buffer + total_read, capacity - total_read - 1)) > 0) {
        total_read += bytes_read;
        if (total_read >= capacity - 1) {
            capacity *= 2;
            char *new_buffer = realloc(buffer, capacity);
            if (!new_buffer) {
                perror("realloc");
                free(buffer);
                return NULL;
            }
            buffer = new_buffer;
        }
    }
    buffer[total_read] = '\0';
    return buffer;
}


/*
 * Executes a single command, capturing its stdout and stderr.
 * This function forks a child process to run the command. The parent process
 * sets up a pipe to capture the child's output and returns it as a string.
 */
char* execute_command(char *args[], char *inputFile, char *outputFile, char *errorFile) {
    int output_pipe[2];
    if (pipe(output_pipe) < 0) {
        perror("pipe failed");
        return xstrdup("");
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        close(output_pipe[0]);
        close(output_pipe[1]);
        return xstrdup("");
    }
    
    if (pid == 0) { // Child process
        close(output_pipe[0]); // Close unused read end

        // Redirect stdout and stderr to the pipe, unless file redirection is specified.
        if (!outputFile) {
            dup2(output_pipe[1], STDOUT_FILENO);
        }
        if (!errorFile) {
            dup2(output_pipe[1], STDERR_FILENO);
        }
        
        // Handle file redirections which take precedence
        if (inputFile && setup_redirection(inputFile, O_RDONLY, STDIN_FILENO) < 0) {
            exit(EXIT_FAILURE);
        }
        if (outputFile && setup_redirection(outputFile, O_WRONLY | O_CREAT | O_TRUNC, STDOUT_FILENO) < 0) {
            exit(EXIT_FAILURE);
        }
        if (errorFile && setup_redirection(errorFile, O_WRONLY | O_CREAT | O_TRUNC, STDERR_FILENO) < 0) {
            exit(EXIT_FAILURE);
        }
        
        close(output_pipe[1]); // Close the write end after dup2
        
        if (execvp(args[0], args) < 0) {
            fprintf(stderr, "Command not found: %s\n", args[0]);
            exit(EXIT_FAILURE);
        }
    } else { // Parent process
        close(output_pipe[1]); // Close unused write end
        
        char *output = read_output_from_fd(output_pipe[0]);
        
        close(output_pipe[0]);
        wait(NULL); // Wait for child to complete
        
        return output;
    }
    return xstrdup(""); // Should not be reached
}

/*
 * Executes a pipeline of commands, capturing the final stdout and stderr from all commands.
 * It creates a series of pipes to connect the commands and an additional pipe
 * to capture all output and errors destined for the client.
 */
char* execute_pipeline(char *cmd) {
    if (validate_pipeline(cmd) != 0) {
        return xstrdup("Invalid pipeline syntax.\n");
    }
    
    Stage stages[MAX_PIPES];
    int numStages = 0;
    
    char *saveptr;
    char *stage_cmd = strtok_r(cmd, "|", &saveptr);
    
    while (stage_cmd != NULL && numStages < MAX_PIPES) {
        stage_cmd = skip_whitespace(stage_cmd);
        if (parse_command(stage_cmd, stages[numStages].args, &stages[numStages].inputFile, &stages[numStages].outputFile, &stages[numStages].errorFile, 1) != 0) {
             // Free any allocated args from previous successful parses
            for (int i = 0; i < numStages; i++) {
                for (int j = 0; stages[i].args[j] != NULL; j++) free(stages[i].args[j]);
                if(stages[i].inputFile) free(stages[i].inputFile);
                if(stages[i].outputFile) free(stages[i].outputFile);
                if(stages[i].errorFile) free(stages[i].errorFile);
            }
            return xstrdup("Command parsing failed in pipeline.\n");
        }
        numStages++;
        stage_cmd = strtok_r(NULL, "|", &saveptr);
    }
    
    if (numStages == 0) return xstrdup("");

    int capture_pipe[2]; // Pipe to capture final output and all errors
    if (pipe(capture_pipe) < 0) {
        perror("capture pipe failed");
        return xstrdup("");
    }

    int pipes[numStages - 1][2];
    for (int i = 0; i < numStages - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe failed");
            return xstrdup("");
        }
    }
    
    pid_t pids[numStages];
    for (int i = 0; i < numStages; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork failed");
            return xstrdup("");
        } else if (pids[i] == 0) { // Child process for stage i
            close(capture_pipe[0]); // Close read end of capture pipe

            // Redirect stderr to capture pipe unless a file is specified
            if (!stages[i].errorFile) {
                dup2(capture_pipe[1], STDERR_FILENO);
            }

            // Connect input from previous stage, unless a file is specified
            if (i > 0 && !stages[i].inputFile) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }
            
            // Connect output to next stage or capture pipe, unless a file is specified
            if (i < numStages - 1 && !stages[i].outputFile) {
                dup2(pipes[i][1], STDOUT_FILENO);
            } else if (i == numStages - 1 && !stages[i].outputFile) {
                // Last command's output goes to capture pipe
                dup2(capture_pipe[1], STDOUT_FILENO);
            }

            // Handle file redirections, which override pipes
            if(stages[i].inputFile && setup_redirection(stages[i].inputFile, O_RDONLY, STDIN_FILENO) < 0) exit(EXIT_FAILURE);
            if(stages[i].outputFile && setup_redirection(stages[i].outputFile, O_WRONLY|O_CREAT|O_TRUNC, STDOUT_FILENO) < 0) exit(EXIT_FAILURE);
            if(stages[i].errorFile && setup_redirection(stages[i].errorFile, O_WRONLY|O_CREAT|O_TRUNC, STDERR_FILENO) < 0) exit(EXIT_FAILURE);
            
            // Close all pipe fds in the child
            for (int j = 0; j < numStages - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            close(capture_pipe[1]);
            
            if (execvp(stages[i].args[0], stages[i].args) < 0) {
                fprintf(stderr, "Command not found: %s\n", stages[i].args[0]);
                exit(EXIT_FAILURE);
            }
        }
    }
    
    // Parent process
    close(capture_pipe[1]); // Close write end of capture pipe
    for (int i = 0; i < numStages - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    
    // Wait for all children to finish
    for (int i = 0; i < numStages; i++) {
        waitpid(pids[i], NULL, 0);
    }

    char* output = read_output_from_fd(capture_pipe[0]);
    close(capture_pipe[0]);

    // Free memory
    for (int i = 0; i < numStages; i++) {
        for (int j = 0; stages[i].args[j] != NULL; j++) free(stages[i].args[j]);
        if(stages[i].inputFile) free(stages[i].inputFile);
        if(stages[i].outputFile) free(stages[i].outputFile);
        if(stages[i].errorFile) free(stages[i].errorFile);
    }

    return output;
}
