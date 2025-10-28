#ifndef EXEC_H
#define EXEC_H

// Executes a single command, captures its output/error, and returns it as a string.
char* execute_command(char *args[], char *inputFile, char *outputFile, char *errorFile);

// Executes a pipeline, captures the final output/error, and returns it as a string.
char* execute_pipeline(char *cmd);

#endif
