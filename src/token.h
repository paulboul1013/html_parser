#ifndef HTML_PARSER_TOKEN_H
#define HTML_PARSER_TOKEN_H

#include <stddef.h>

typedef enum {
    TOKEN_DOCTYPE = 1,
    TOKEN_START_TAG,
    TOKEN_END_TAG,
    TOKEN_COMMENT,
    TOKEN_CHARACTER,
    TOKEN_EOF
} token_type;

typedef struct {
    char *name;
    char *value;
} token_attr;

typedef struct {
    token_type type;
    char *name;   /* For tag name or doctype name */
    char *data;   /* For comment or character data */
    token_attr *attrs;
    size_t attr_count;
    int self_closing;
    int force_quirks;
} token;

void token_init(token *t);
void token_free(token *t);

#endif
