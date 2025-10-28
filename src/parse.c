#include "parse.h"
#include "tokenize.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>


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
        printf("Invalid pipeline: starts with pipe.\n");
        return -1;
    }

    int saw_non_ws_since_pipe = 0;
    for (char *q = p; *q; ++q) {
        if (*q == '|') {
            if (!saw_non_ws_since_pipe) {
                printf("Invalid pipeline: empty command between pipes.\n");
                return -1;
            }
            saw_non_ws_since_pipe = 0;
        } else if (*q != ' ' && *q != '\t' && *q != '\n') {
            saw_non_ws_since_pipe = 1;
        }
    }

    if (!saw_non_ws_since_pipe) {
        printf("Invalid pipeline: ends with pipe.\n");
        return -1;
    }

    return 0;
}

int parse_command(char *cmd, char *args[], char **inputFile, char **outputFile, char **errorFile, int isPipeline){
    *inputFile = *outputFile = *errorFile = NULL;

    QTok *toks=NULL; int nt=0;
    if(qtokenize(cmd, &toks, &nt)!=0){
        // No need to print, qtokenize doesn't exist
        args[0]=NULL; return -1;
    }
    if(nt==0){ args[0]=NULL; free_qtokens(toks,nt); return -1; }

    bool quoted[MAX_ARGS]; int ac=0;
    for(int i=0;i<nt;i++){
        if(ac>=MAX_ARGS-1){ free_qtokens(toks,nt); args[0]=NULL; return -1; }
        args[ac] = toks[i].val;
        quoted[ac] = toks[i].was_quoted;
        ac++;
    }
    free(toks);

    for(int i=0;i<ac;i++){
        if(!quoted[i] && (strcmp(args[i],"<")==0 || strcmp(args[i],">")==0 || strcmp(args[i],"2>")==0)){
            if(i+1>=ac){ 
                if(strcmp(args[i],"<")==0) printf("Input file not specified.\n");
                else if(strcmp(args[i],">")==0) printf(isPipeline ? "Output file not specified after redirection.\n" : "Output file not specified.\n");
                else printf("Error output file not specified.\n");
                for(int k=0;k<ac;k++) free(args[k]);
                args[0]=NULL; return -1;
            }
        }
    }

    char *argv2[MAX_ARGS]; bool quoted2[MAX_ARGS]; int m=0;
    for(int i=0;i<ac;i++){
        if(!quoted[i] && strcmp(args[i],"<")==0){
            *inputFile = strip_outer_quotes(args[i+1]); i++; continue;
        }else if(!quoted[i] && strcmp(args[i],">")==0){
            *outputFile = strip_outer_quotes(args[i+1]); i++; continue;
        }else if(!quoted[i] && strcmp(args[i],"2>")==0){
            *errorFile = strip_outer_quotes(args[i+1]); i++; continue;
        }else{
            argv2[m]=args[i]; quoted2[m]=quoted[i]; m++;
        }
    }
    argv2[m]=NULL;

    if(m==0){
        if(*inputFile){ free(*inputFile); *inputFile=NULL; }
        if(*outputFile){ free(*outputFile); *outputFile=NULL; }
        if(*errorFile){ free(*errorFile); *errorFile=NULL; }
        args[0]=NULL; return -1;
    }

    apply_globbing(argv2, quoted2, &m);

    for(int i=0;i<m;i++) args[i]=argv2[i];
    args[m]=NULL;

    return 0;
}
