#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

char *xstrdup(const char *s){
    if(!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char*)malloc(n);
    if(!p){ perror("malloc"); _exit(127); }
    memcpy(p, s, n);
    return p;
}

char *strip_outer_quotes(const char *str) {
    if(!str){
        return NULL;
    }
    size_t len = strlen(str);
    if(len < 2){
        return xstrdup(str);
    }
    
    if((str[0] == '\'' && str[len-1] == '\'') || (str[0] == '"' && str[len-1] == '"')){
        char *result = malloc(len - 1);
        if(!result){
            perror("malloc");
            _exit(127);
        }
        memcpy(result, str + 1, len - 2);
        result[len - 2] = '\0';
        return result;
    }
    
    return xstrdup(str);
}
