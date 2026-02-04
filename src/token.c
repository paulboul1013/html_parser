#include "token.h"

#include <stdlib.h>
#include <string.h>

void token_init(token *t) {
    if (!t) return;
    t->type = TOKEN_EOF;
    t->name = NULL;
    t->data = NULL;
    t->attrs = NULL;
    t->attr_count = 0;
    t->self_closing = 0;
    t->force_quirks = 0;
}

void token_free(token *t) {
    size_t i;
    if (!t) return;
    free(t->name);
    free(t->data);
    for (i = 0; i < t->attr_count; ++i) {
        free(t->attrs[i].name);
        free(t->attrs[i].value);
    }
    free(t->attrs);
    token_init(t);
}
