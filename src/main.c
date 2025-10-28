#include "parse.h"
#include "exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//maximum length for command input buffer
#define MAX_CMD_LENGTH 1024 
//maximum number of arguments a command can have
#define MAX_ARGS 64         

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
            char* output = execute_pipeline(cmd);
            printf("%s", output);
            free(output);
        }else if(parse_command(cmd, args, &inputFile, &outputFile, &errorFile, 0) == 0){
            //single command - parse and execute if parsing succeeded
            char* output = execute_command(args, inputFile, outputFile, errorFile);
            printf("%s", output);
            free(output);

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
        }
    }
    
    return 0;
}
