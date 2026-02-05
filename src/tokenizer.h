#ifndef HTML_PARSER_TOKENIZER_H
#define HTML_PARSER_TOKENIZER_H

#include <stddef.h>
#include "token.h"

typedef enum {
    TOKENIZE_DATA = 0,
    TOKENIZE_RCDATA,
    TOKENIZE_RAWTEXT,
    TOKENIZE_SCRIPT_DATA,
    TOKENIZE_SCRIPT_DATA_ESCAPED,
    TOKENIZE_SCRIPT_DATA_DOUBLE_ESCAPED
} tokenizer_state;

typedef struct {
    const char *input;
    size_t pos;
    size_t len;
    size_t line;
    size_t col;
    tokenizer_state state;
    char raw_tag[16];
} tokenizer;

void tokenizer_init(tokenizer *tz, const char *input);
void tokenizer_init_with_context(tokenizer *tz, const char *input, const char *context_tag);
void tokenizer_next(tokenizer *tz, token *out);

#endif
