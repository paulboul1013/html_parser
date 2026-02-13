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
    TOKENIZE_SCRIPT_DATA_DOUBLE_ESCAPED,
    TOKENIZE_PLAINTEXT
} tokenizer_state;

typedef struct {
    const char *input;
    size_t pos;
    size_t len;
    size_t line;
    size_t col;
    tokenizer_state state;
    char raw_tag[16];
    int allow_cdata;        /* set by tree builder when in foreign content */
} tokenizer;

void tokenizer_init(tokenizer *tz, const char *input);
void tokenizer_init_with_context(tokenizer *tz, const char *input, const char *context_tag);
void tokenizer_next(tokenizer *tz, token *out);

/* Pre-process raw input bytes: replace U+0000 NULL with U+FFFD REPLACEMENT CHARACTER.
 * raw: input buffer (may contain embedded NULLs).
 * raw_len: exact byte length (from fread, not strlen).
 * Returns: newly allocated null-terminated string. Caller must free(). */
char *tokenizer_replace_nulls(const char *raw, size_t raw_len);

#endif
