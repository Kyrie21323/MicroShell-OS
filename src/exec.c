#include "exec.h"
#include "parse.h"
#include "redir.h"
#include "util.h"
#include "errors.h"
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
        close(output_pipe[0]);

        if (!outputFile) {
            dup2(output_pipe[1], STDOUT_FILENO);
        }
        if (!errorFile) {
            dup2(output_pipe[1], STDERR_FILENO);
        }
        
        if (inputFile && setup_redirection(inputFile, O_RDONLY, STDIN_FILENO) < 0) {
            exit(EXIT_FAILURE);
        }
        if (outputFile && setup_redirection(outputFile, O_WRONLY | O_CREAT | O_TRUNC, STDOUT_FILENO) < 0) {
            exit(EXIT_FAILURE);
        }
        if (errorFile && setup_redirection(errorFile, O_WRONLY | O_CREAT | O_TRUNC, STDERR_FILENO) < 0) {
            exit(EXIT_FAILURE);
        }
        
        close(output_pipe[1]);
        
        if (execvp(args[0], args) < 0) {
            dprintf(STDERR_FILENO, "Command not found: %s\n", args[0]);
            _exit(127);
        }
    } else { // Parent process
        close(output_pipe[1]);
        
        char *output = read_output_from_fd(output_pipe[0]);
        
        close(output_pipe[0]);
        wait(NULL);
        
        return output;
    }
    return xstrdup("");
}

char* execute_pipeline(char *cmd, int client_fd) {
    int validation_err = validate_pipeline(cmd);
    if (validation_err != VALIDATE_SUCCESS) {
        switch (validation_err) {
            case VALIDATE_ERR_STARTS_PIPE:
                return xstrdup(ERR_CMD_MISSING_BEFORE_PIPE);
            case VALIDATE_ERR_EMPTY_CMD:
                return xstrdup(ERR_EMPTY_CMD_BETWEEN_PIPES);
            case VALIDATE_ERR_ENDS_PIPE:
                return xstrdup(ERR_CMD_MISSING_AFTER_PIPE);
            default:
                return xstrdup(ERR_CMD_MISSING_AFTER_PIPE);
        }
    }
    
    Stage stages[MAX_PIPES];
    int numStages = 0;
    
    char *saveptr;
    char *stage_cmd = strtok_r(cmd, "|", &saveptr);
    
    while (stage_cmd != NULL && numStages < MAX_PIPES) {
        stage_cmd = skip_whitespace(stage_cmd);
        int parse_res = parse_command(stage_cmd, stages[numStages].args, &stages[numStages].inputFile, &stages[numStages].outputFile, &stages[numStages].errorFile, 1);
        if (parse_res != PARSE_SUCCESS) {
            for (int i = 0; i < numStages; i++) {
                for (int j = 0; stages[i].args[j] != NULL; j++) free(stages[i].args[j]);
                if(stages[i].inputFile) free(stages[i].inputFile);
                if(stages[i].outputFile) free(stages[i].outputFile);
                if(stages[i].errorFile) free(stages[i].errorFile);
            }
            switch (parse_res) {
                case PARSE_ERR_NO_INPUT_FILE: return xstrdup(ERR_INPUT_NOT_SPECIFIED);
                case PARSE_ERR_NO_OUTPUT_FILE: return xstrdup(ERR_OUTPUT_NOT_SPECIFIED);
                case PARSE_ERR_NO_OUTPUT_FILE_AFTER: return xstrdup(ERR_OUT_AFTER);
                case PARSE_ERR_NO_ERROR_FILE: return xstrdup(ERR_ERROR_NOT_SPECIFIED);
                case PARSE_ERR_UNCLOSED_QUOTES: return xstrdup(ERR_UNCLOSED_QUOTES);
                default: return xstrdup("");
            }
        }
        numStages++;
        stage_cmd = strtok_r(NULL, "|", &saveptr);
    }
    
    if (numStages == 0) return xstrdup("");

    int capture_pipe[2];
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
        } else if (pids[i] == 0) {
            // CHILD PROCESS
            close(capture_pipe[0]);

            // Setup STDIN: first stage gets /dev/null if no explicit input redirection
            if (i == 0) {
                if (!stages[i].inputFile) {
                    // Feed EOF to first stage to prevent blocking on user input
                    int devnull = open("/dev/null", O_RDONLY);
                    if (devnull >= 0) {
                        dup2(devnull, STDIN_FILENO);
                        close(devnull);
                    }
                } else {
                    if(setup_redirection(stages[i].inputFile, O_RDONLY, STDIN_FILENO) < 0) _exit(EXIT_FAILURE);
                }
            } else {
                // Connect to previous stage's pipe
                if (!stages[i].inputFile) {
                    dup2(pipes[i - 1][0], STDIN_FILENO);
                } else {
                    if(setup_redirection(stages[i].inputFile, O_RDONLY, STDIN_FILENO) < 0) _exit(EXIT_FAILURE);
                }
            }
            
            // Setup STDOUT
            if (i < numStages - 1) {
                // Middle stages write to next pipe
                if (!stages[i].outputFile) {
                    dup2(pipes[i][1], STDOUT_FILENO);
                } else {
                    if(setup_redirection(stages[i].outputFile, O_WRONLY|O_CREAT|O_TRUNC, STDOUT_FILENO) < 0) _exit(EXIT_FAILURE);
                }
            } else {
                // Last stage writes to capture pipe
                if (!stages[i].outputFile) {
                    dup2(capture_pipe[1], STDOUT_FILENO);
                } else {
                    if(setup_redirection(stages[i].outputFile, O_WRONLY|O_CREAT|O_TRUNC, STDOUT_FILENO) < 0) _exit(EXIT_FAILURE);
                }
            }

            // Setup STDERR: redirect to capture pipe so errors are collected with stdout
            // We'll handle errors in the parent and send them via the protocol
            if (!stages[i].errorFile) {
                dup2(capture_pipe[1], STDERR_FILENO);
            } else {
                if(setup_redirection(stages[i].errorFile, O_WRONLY|O_CREAT|O_TRUNC, STDERR_FILENO) < 0) _exit(EXIT_FAILURE);
            }
            
            // Close all pipe file descriptors in child (after dup2, we have copies)
            for (int j = 0; j < numStages - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            close(capture_pipe[1]);
            
            // Execute command
            if (execvp(stages[i].args[0], stages[i].args) < 0) {
                // execvp failed - write error to stderr (which goes to capture pipe)
                dprintf(STDERR_FILENO, "Command not found in pipe sequence: %s\n", stages[i].args[0]);
                _exit(127);
            }
        }
        // PARENT: Continue spawning remaining stages regardless of previous results
    }
    
    // Close all pipe file descriptors in parent (children have what they need)
    close(capture_pipe[1]);
    for (int i = 0; i < numStages - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    
    // Wait for all children to complete (handle failures gracefully)
    for (int i = 0; i < numStages; i++) {
        waitpid(pids[i], NULL, 0);
    }

    // Read both stdout and stderr from capture pipe (they're both redirected there)
    // This includes error messages from failed execvp calls
    char* output = read_output_from_fd(capture_pipe[0]);
    close(capture_pipe[0]);

    for (int i = 0; i < numStages; i++) {
        for (int j = 0; stages[i].args[j] != NULL; j++) free(stages[i].args[j]);
        if(stages[i].inputFile) free(stages[i].inputFile);
        if(stages[i].outputFile) free(stages[i].outputFile);
        if(stages[i].errorFile) free(stages[i].errorFile);
    }

    return output;
}
