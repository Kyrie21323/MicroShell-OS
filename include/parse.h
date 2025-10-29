#ifndef PARSE_H
#define PARSE_H

// Return codes for parse_command
#define PARSE_SUCCESS 0
#define PARSE_ERR_SYNTAX 1
#define PARSE_ERR_TOO_MANY_ARGS 2
#define PARSE_ERR_NO_INPUT_FILE 3
#define PARSE_ERR_NO_OUTPUT_FILE 4
#define PARSE_ERR_NO_ERROR_FILE 5
#define PARSE_ERR_EMPTY_CMD_REDIR 6

// Return codes for validate_pipeline
#define VALIDATE_SUCCESS 0
#define VALIDATE_ERR_STARTS_PIPE 1
#define VALIDATE_ERR_EMPTY_CMD 2
#define VALIDATE_ERR_ENDS_PIPE 3

int parse_command(char *cmd, char *args[], char **inputFile, char **outputFile, char **errorFile, int isPipeline);
int validate_pipeline(char *cmd);

#endif
