#ifndef TOKENIZE_H
#define TOKENIZE_H
#include <stdbool.h>
typedef struct { char *val; bool was_quoted; } QTok;
int qtokenize(const char *line, QTok **out, int *count);
void free_qtokens(QTok *arr, int n);
void apply_globbing(char **argv, bool *was_quoted, int *argc);
#endif
