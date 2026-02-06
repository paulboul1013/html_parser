#ifndef HTML_PARSER_TREE_H
#define HTML_PARSER_TREE_H

#include <stddef.h>

typedef enum {
    NODE_DOCUMENT = 1,
    NODE_DOCTYPE,
    NODE_ELEMENT,
    NODE_TEXT,
    NODE_COMMENT
} node_type;

typedef struct {
    char *name;
    char *value;
} node_attr;

typedef struct node {
    node_type type;
    char *name;              /* element/doctype name */
    char *data;              /* text/comment data */
    node_attr *attrs;        /* element attributes (NULL if none) */
    size_t attr_count;
    struct node *parent;
    struct node *first_child;
    struct node *last_child;
    struct node *next_sibling;
} node;

node *node_create(node_type type, const char *name, const char *data);
void node_append_child(node *parent, node *child);
void node_insert_before(node *parent, node *child, node *ref);
void node_remove_child(node *parent, node *child);
void node_reparent_children(node *src, node *dst);
void node_free_shallow(node *n);
void node_free(node *n);

void tree_dump_ascii(const node *root, const char *title);

/* Serialize tree to HTML string (caller must free) */
char *tree_serialize_html(const node *root);

#endif
