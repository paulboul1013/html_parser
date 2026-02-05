#include "tree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup_string(const char *s) {
    size_t len;
    char *out;
    if (!s) return NULL;
    len = strlen(s);
    out = (char *)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len + 1);
    return out;
}

node *node_create(node_type type, const char *name, const char *data) {
    node *n = (node *)calloc(1, sizeof(node));
    if (!n) return NULL;
    n->type = type;
    n->name = dup_string(name);
    n->data = dup_string(data);
    return n;
}

void node_append_child(node *parent, node *child) {
    if (!parent || !child) return;
    child->parent = parent;
    if (!parent->first_child) {
        parent->first_child = child;
        parent->last_child = child;
        return;
    }
    parent->last_child->next_sibling = child;
    parent->last_child = child;
}

void node_insert_before(node *parent, node *child, node *ref) {
    if (!parent || !child) return;
    child->parent = parent;
    if (!ref || !parent->first_child) {
        node_append_child(parent, child);
        return;
    }
    if (parent->first_child == ref) {
        child->next_sibling = ref;
        parent->first_child = child;
        return;
    }
    node *prev = parent->first_child;
    while (prev && prev->next_sibling && prev->next_sibling != ref) {
        prev = prev->next_sibling;
    }
    if (!prev || !prev->next_sibling) {
        node_append_child(parent, child);
        return;
    }
    child->next_sibling = ref;
    prev->next_sibling = child;
}

void node_free_shallow(node *n) {
    if (!n) return;
    free(n->name);
    free(n->data);
    free(n);
}

void node_free(node *n) {
    node *child;
    node *next;
    if (!n) return;
    child = n->first_child;
    while (child) {
        next = child->next_sibling;
        node_free(child);
        child = next;
    }
    free(n->name);
    free(n->data);
    free(n);
}

static const char *node_type_name(node_type t) {
    switch (t) {
        case NODE_DOCUMENT: return "DOCUMENT";
        case NODE_DOCTYPE: return "DOCTYPE";
        case NODE_ELEMENT: return "ELEMENT";
        case NODE_TEXT: return "TEXT";
        case NODE_COMMENT: return "COMMENT";
        default: return "UNKNOWN";
    }
}

static void dump_node(const node *n, const char *prefix, int is_last) {
    char next_prefix[512];
    const char *branch = is_last ? "\\-- " : "|-- ";

    printf("%s%s%s", prefix, branch, node_type_name(n->type));
    if (n->name) printf(" name=\"%s\"", n->name);
    if (n->data) printf(" data=\"%s\"", n->data);
    printf("\n");

    snprintf(next_prefix, sizeof(next_prefix), "%s%s", prefix, is_last ? "    " : "|   ");
    for (const node *child = n->first_child; child; child = child->next_sibling) {
        int last = (child->next_sibling == NULL);
        dump_node(child, next_prefix, last);
    }
}

void tree_dump_ascii(const node *root, const char *title) {
    if (!root) return;
    if (title && title[0]) {
        printf("%s\n", title);
    }
    printf("%s\n", node_type_name(root->type));
    for (const node *child = root->first_child; child; child = child->next_sibling) {
        int last = (child->next_sibling == NULL);
        dump_node(child, "", last);
    }
}
