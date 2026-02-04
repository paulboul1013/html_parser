#ifndef HTML_PARSER_TREE_BUILDER_H
#define HTML_PARSER_TREE_BUILDER_H

#include "token.h"
#include "tree.h"

node *build_tree_from_tokens(const token *tokens, size_t count);
node *build_tree_from_input(const char *input);

#endif
