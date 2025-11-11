#include "parse.h"
#include "tokenize.h"
#include "util.h"
#include "errors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>


#define MAX_CMD_LENGTH 1024 
#define MAX_ARGS 64         

static char* skip_whitespace(char *str){
    while(*str == ' ' || *str == '\t' || *str == '\n'){
        str++;
    }
    return str;
}

int validate_pipeline(char *cmd){
    size_t n = strlen(cmd) + 1;
    char buf[n];
    memcpy(buf, cmd, n);

    char *p = skip_whitespace(buf);

    if (*p == '|') {
        return VALIDATE_ERR_STARTS_PIPE;
    }

    int saw_non_ws_since_pipe = 0;
    for (char *q = p; *q; ++q) {
        if (*q == '|') {
            if (!saw_non_ws_since_pipe) {
                return VALIDATE_ERR_EMPTY_CMD;
            }
            saw_non_ws_since_pipe = 0;
        } else if (*q != ' ' && *q != '\t' && *q != '\n') {
            saw_non_ws_since_pipe = 1;
        }
    }

    if (!saw_non_ws_since_pipe) {
        return VALIDATE_ERR_ENDS_PIPE;
    }

    return VALIDATE_SUCCESS;
}

int parse_command(char *cmd, char *args[], char **inputFile, char **outputFile, char **errorFile, int isPipeline, int *outputAppend){
    (void)isPipeline; // isPipeline is now handled by the caller based on error code
    *inputFile = *outputFile = *errorFile = NULL;
    if(outputAppend) *outputAppend = 0; // Default to truncate mode

    QTok *toks=NULL; int nt=0;
    if(qtokenize(cmd, &toks, &nt)!=0){
        args[0]=NULL; return PARSE_ERR_UNCLOSED_QUOTES;
    }
    if(nt==0){ args[0]=NULL; free_qtokens(toks,nt); return PARSE_ERR_SYNTAX; }

    bool quoted[MAX_ARGS]; int ac=0;
    for(int i=0;i<nt;i++){
        if(ac>=MAX_ARGS-1){ 
            // Free tokens that were already transferred to args
            for(int k=0; k<ac; k++) free(args[k]);
            // Free remaining tokens that were not transferred
            for(int k=i; k<nt; k++) free(toks[k].val);
            free(toks);
            args[0]=NULL; 
            return PARSE_ERR_TOO_MANY_ARGS; 
        }
        args[ac] = toks[i].val;
        quoted[ac] = toks[i].was_quoted;
        ac++;
    }
    free(toks); // The container is freed, but the strings are now owned by 'args'

    for(int i=0;i<ac;i++){
        if(!quoted[i] && (strcmp(args[i],"<")==0 || strcmp(args[i],">")==0 || strcmp(args[i],">>")==0 || strcmp(args[i],"2>")==0)){
            if(i+1>=ac || args[i+1] == NULL || strlen(args[i+1]) == 0){ 
                int err_code = PARSE_ERR_SYNTAX;
                if(strcmp(args[i],"<")==0) err_code = PARSE_ERR_NO_INPUT_FILE;
                else if(strcmp(args[i],">")==0 || strcmp(args[i],">>")==0) err_code = isPipeline ? PARSE_ERR_NO_OUTPUT_FILE_AFTER : PARSE_ERR_NO_OUTPUT_FILE;
                else err_code = PARSE_ERR_NO_ERROR_FILE;

                for(int k=0;k<ac;k++) free(args[k]);
                args[0]=NULL; 
                return err_code;
            }
        }
    }

    char *argv2[MAX_ARGS]; bool quoted2[MAX_ARGS]; int m=0;
    for(int i=0;i<ac;i++){
        if(!quoted[i] && strcmp(args[i],"<")==0){
            char *fname = strip_outer_quotes(args[i+1]);
            if(fname == NULL || strlen(fname) == 0){
                if(fname) free(fname);
                for(int k=0;k<ac;k++) free(args[k]);
                args[0]=NULL; 
                return PARSE_ERR_NO_INPUT_FILE;
            }
            *inputFile = fname;
            free(args[i]); free(args[i+1]);
            i++; continue;
        }else if(!quoted[i] && strcmp(args[i],">>")==0){
            // Check if filename is missing or empty
            if(i+1 >= ac || args[i+1] == NULL || strlen(args[i+1]) == 0){
                for(int k=0;k<ac;k++) free(args[k]);
                args[0]=NULL; 
                // Use "after redirection" message when in pipeline context
                return isPipeline ? PARSE_ERR_NO_OUTPUT_FILE_AFTER : PARSE_ERR_NO_OUTPUT_FILE;
            }
            char *fname = strip_outer_quotes(args[i+1]);
            if(fname == NULL || strlen(fname) == 0){
                if(fname) free(fname);
                for(int k=0;k<ac;k++) free(args[k]);
                args[0]=NULL; 
                // Use "after redirection" message when in pipeline context
                return isPipeline ? PARSE_ERR_NO_OUTPUT_FILE_AFTER : PARSE_ERR_NO_OUTPUT_FILE;
            }
            *outputFile = fname;
            if(outputAppend) *outputAppend = 1; // Set append mode
            free(args[i]); free(args[i+1]);
            i++; continue;
        }else if(!quoted[i] && strcmp(args[i],">")==0){
            // Check if filename is missing or empty
            if(i+1 >= ac || args[i+1] == NULL || strlen(args[i+1]) == 0){
                for(int k=0;k<ac;k++) free(args[k]);
                args[0]=NULL; 
                // Use "after redirection" message when in pipeline context
                return isPipeline ? PARSE_ERR_NO_OUTPUT_FILE_AFTER : PARSE_ERR_NO_OUTPUT_FILE;
            }
            char *fname = strip_outer_quotes(args[i+1]);
            if(fname == NULL || strlen(fname) == 0){
                if(fname) free(fname);
                for(int k=0;k<ac;k++) free(args[k]);
                args[0]=NULL; 
                // Use "after redirection" message when in pipeline context
                return isPipeline ? PARSE_ERR_NO_OUTPUT_FILE_AFTER : PARSE_ERR_NO_OUTPUT_FILE;
            }
            *outputFile = fname;
            if(outputAppend) *outputAppend = 0; // Set truncate mode
            free(args[i]); free(args[i+1]);
            i++; continue;
        }else if(!quoted[i] && strcmp(args[i],"2>")==0){
            char *fname = strip_outer_quotes(args[i+1]);
            if(fname == NULL || strlen(fname) == 0){
                if(fname) free(fname);
                for(int k=0;k<ac;k++) free(args[k]);
                args[0]=NULL; 
                return PARSE_ERR_NO_ERROR_FILE;
            }
            *errorFile = fname;
            free(args[i]); free(args[i+1]);
            i++; continue;
        }else{
            argv2[m]=args[i]; quoted2[m]=quoted[i]; m++;
        }
    }
    argv2[m]=NULL;

    if(m==0){
        if(*inputFile){ free(*inputFile); *inputFile=NULL; }
        if(*outputFile){ free(*outputFile); *outputFile=NULL; }
        if(*errorFile){ free(*errorFile); *errorFile=NULL; }
        args[0]=NULL; return PARSE_ERR_EMPTY_CMD_REDIR;
    }

    apply_globbing(argv2, quoted2, &m);

    for(int i=0;i<m;i++) args[i]=argv2[i];
    args[m]=NULL;

    return PARSE_SUCCESS;
}
