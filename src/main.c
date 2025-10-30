#include "parse.h"
#include "exec.h"
#include "errors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

//maximum length for command input buffer
#define MAX_CMD_LENGTH 1024 
//maximum number of arguments a command can have
#define MAX_ARGS 64

// Helper function to check if output is an error message
static int is_error_message(const char *output) {
    if(!output) return 0;
    // Check exact matches first
    if (strcmp(output, ERR_CMD_MISSING_BEFORE_PIPE) == 0 ||
        strcmp(output, ERR_CMD_MISSING_AFTER_PIPE) == 0 ||
        strcmp(output, ERR_EMPTY_CMD_BETWEEN_PIPES) == 0 ||
        strcmp(output, ERR_INPUT_NOT_SPECIFIED) == 0 ||
        strcmp(output, ERR_OUTPUT_NOT_SPECIFIED) == 0 ||
        strcmp(output, ERR_OUTPUT_NOT_SPECIFIED_AFTER) == 0 ||
        strcmp(output, ERR_ERROR_NOT_SPECIFIED) == 0 ||
        strcmp(output, ERR_UNCLOSED_QUOTES) == 0 ||
        strcmp(output, ERR_FILE_NOT_FOUND) == 0) {
        return 1;
    }
    // Check for messages that include command name
    if (strstr(output, "Command not found:") != NULL ||
        strstr(output, "Command not found in pipe sequence:") != NULL ||
        strncmp(output, ERR_CMD_NOT_FOUND, strlen(ERR_CMD_NOT_FOUND) - 2) == 0 ||
        strncmp(output, ERR_PIPE_CMD, strlen(ERR_PIPE_CMD) - 2) == 0) {
        return 1;
    }
    return 0;
}         

/*Main function
This function implements the main shell loop that reads commands and executes them
It handles both single commands and pipelines, with proper error handling
*/
int main() {
    //buffer to store user input command
    char cmd[MAX_CMD_LENGTH];
    //array to store parsed command arguments
    char *args[MAX_ARGS];
    //point er to store redirection filenames
    char *inputFile, *outputFile, *errorFile;
    
    while (1) {
        //display shell prompt
        printf("$ ");
        
        //read command from user input
        if (fgets(cmd, MAX_CMD_LENGTH, stdin) == NULL) {
            break;
        }
        
        //remove newline and skip empty commands
        cmd[strcspn(cmd, "\n")] = '\0';
        
        //skip empty commands
        if(strlen(cmd) == 0){
            continue;
        }
        
        //handle exit command
        if(strcmp(cmd, "exit") == 0){
            break;
        }
        
        //execute command (pipeline or single)
        if(strchr(cmd, '|') != NULL){
            //command contains pipe symbol - execute as pipeline
            // For standalone shell, use STDERR_FILENO for error output
            char* output = execute_pipeline(cmd, STDERR_FILENO);
            if(output) {
                // Check if output is an error message
                if(is_error_message(output)) {
                    write(STDERR_FILENO, output, strlen(output));
                } else {
                    printf("%s", output);
                }
            }
            if(output) free(output);
        }else{
            int parse_res = parse_command(cmd, args, &inputFile, &outputFile, &errorFile, 0);
            if(parse_res == PARSE_SUCCESS){
                //single command - parse and execute if parsing succeeded
                char* output = execute_command(args, inputFile, outputFile, errorFile);
                if(output) {
                    // Check if output is an error message
                    if(is_error_message(output)) {
                        write(STDERR_FILENO, output, strlen(output));
                    } else {
                        printf("%s", output);
                    }
                }
                if(output) free(output);

                //free argv strings created by qtokenize/globbing
                for(int i=0; args[i]!=NULL; i++){
                    free(args[i]);
                }
                if(inputFile){
                    free(inputFile);
                }
                if(outputFile){
                    free(outputFile);
                }
                if(errorFile){
                    free(errorFile);
                }
            } else {
                // Handle parse errors for single commands
                switch(parse_res){
                    case PARSE_ERR_UNCLOSED_QUOTES:
                        write(STDERR_FILENO, ERR_UNCLOSED_QUOTES, strlen(ERR_UNCLOSED_QUOTES));
                        break;
                    case PARSE_ERR_NO_INPUT_FILE:
                        write(STDERR_FILENO, ERR_INPUT_NOT_SPECIFIED, strlen(ERR_INPUT_NOT_SPECIFIED));
                        break;
                    case PARSE_ERR_NO_OUTPUT_FILE:
                        write(STDERR_FILENO, ERR_OUTPUT_NOT_SPECIFIED, strlen(ERR_OUTPUT_NOT_SPECIFIED));
                        break;
                    case PARSE_ERR_NO_OUTPUT_FILE_AFTER:
                        write(STDERR_FILENO, ERR_OUTPUT_NOT_SPECIFIED_AFTER, strlen(ERR_OUTPUT_NOT_SPECIFIED_AFTER));
                        break;
                    case PARSE_ERR_NO_ERROR_FILE:
                        write(STDERR_FILENO, ERR_ERROR_NOT_SPECIFIED, strlen(ERR_ERROR_NOT_SPECIFIED));
                        break;
                    default:
                        // For other errors, just do nothing (syntax errors, etc.)
                        break;
                }
                // Clean up any allocated args
                if(args[0] != NULL){
                    for(int i=0; args[i]!=NULL; i++){
                        free(args[i]);
                    }
                }
                if(inputFile) free(inputFile);
                if(outputFile) free(outputFile);
                if(errorFile) free(errorFile);
            }
        }
    }
    
    return 0;
}
