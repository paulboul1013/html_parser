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

static void free_node_attrs(node_attr *attrs, size_t count) {
    if (!attrs) return;
    for (size_t i = 0; i < count; ++i) {
        free(attrs[i].name);
        free(attrs[i].value);
    }
    free(attrs);
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

void node_remove_child(node *parent, node *child) {
    if (!parent || !child) return;
    if (parent->first_child == child) {
        parent->first_child = child->next_sibling;
        if (parent->last_child == child) {
            parent->last_child = NULL;
        }
    } else {
        node *prev = parent->first_child;
        while (prev && prev->next_sibling != child) {
            prev = prev->next_sibling;
        }
        if (!prev) return;
        prev->next_sibling = child->next_sibling;
        if (parent->last_child == child) {
            parent->last_child = prev;
        }
    }
    child->parent = NULL;
    child->next_sibling = NULL;
}

void node_reparent_children(node *src, node *dst) {
    if (!src || !dst || !src->first_child) return;
    node *child = src->first_child;
    while (child) {
        child->parent = dst;
        child = child->next_sibling;
    }
    if (dst->last_child) {
        dst->last_child->next_sibling = src->first_child;
    } else {
        dst->first_child = src->first_child;
    }
    dst->last_child = src->last_child;
    src->first_child = NULL;
    src->last_child = NULL;
}

void node_free_shallow(node *n) {
    if (!n) return;
    free(n->name);
    free(n->data);
    free_node_attrs(n->attrs, n->attr_count);
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
    free_node_attrs(n->attrs, n->attr_count);
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
    if (n->attr_count > 0) {
        printf(" [");
        for (size_t i = 0; i < n->attr_count; ++i) {
            if (i) printf(" ");
            printf("%s=\"%s\"",
                   n->attrs[i].name  ? n->attrs[i].name  : "",
                   n->attrs[i].value ? n->attrs[i].value : "");
        }
        printf("]");
    }
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

/* ============================================================================
 * HTML Serialization (tree → HTML string)
 * ============================================================================ */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} string_buffer;

static void sb_init(string_buffer *sb) {
    sb->cap = 256;
    sb->data = (char *)malloc(sb->cap);
    sb->len = 0;
    if (sb->data) sb->data[0] = '\0';
}

static void sb_append(string_buffer *sb, const char *str) {
    if (!sb->data || !str) return;
    size_t slen = strlen(str);
    if (sb->len + slen + 1 > sb->cap) {
        size_t new_cap = sb->cap * 2;
        while (new_cap < sb->len + slen + 1) new_cap *= 2;
        char *new_data = (char *)realloc(sb->data, new_cap);
        if (!new_data) return;
        sb->data = new_data;
        sb->cap = new_cap;
    }
    memcpy(sb->data + sb->len, str, slen);
    sb->len += slen;
    sb->data[sb->len] = '\0';
}

static void sb_append_char(string_buffer *sb, char c) {
    char buf[2] = {c, '\0'};
    sb_append(sb, buf);
}

static char *sb_to_string(string_buffer *sb) {
    return sb->data;
}

static int is_void_element_serializer(const char *name) {
    if (!name) return 0;
    return strcmp(name, "area") == 0 ||
           strcmp(name, "base") == 0 ||
           strcmp(name, "br") == 0 ||
           strcmp(name, "col") == 0 ||
           strcmp(name, "embed") == 0 ||
           strcmp(name, "hr") == 0 ||
           strcmp(name, "img") == 0 ||
           strcmp(name, "input") == 0 ||
           strcmp(name, "link") == 0 ||
           strcmp(name, "meta") == 0 ||
           strcmp(name, "param") == 0 ||
           strcmp(name, "source") == 0 ||
           strcmp(name, "track") == 0 ||
           strcmp(name, "wbr") == 0;
}

static int is_raw_text_element(const char *name) {
    if (!name) return 0;
    return strcmp(name, "script") == 0 ||
           strcmp(name, "style") == 0;
}

static int is_rcdata_element(const char *name) {
    if (!name) return 0;
    return strcmp(name, "textarea") == 0 ||
           strcmp(name, "title") == 0;
}

static char *escape_html_text(const char *text) {
    if (!text) return NULL;
    string_buffer sb;
    sb_init(&sb);
    for (const char *p = text; *p; ++p) {
        if (*p == '&') {
            sb_append(&sb, "&amp;");
        } else if (*p == '<') {
            sb_append(&sb, "&lt;");
        } else if (*p == '>') {
            sb_append(&sb, "&gt;");
        } else {
            sb_append_char(&sb, *p);
        }
    }
    return sb_to_string(&sb);
}

static char *escape_attr_value(const char *value) {
    if (!value) return NULL;
    string_buffer sb;
    sb_init(&sb);
    for (const char *p = value; *p; ++p) {
        if (*p == '&') {
            sb_append(&sb, "&amp;");
        } else if (*p == '"') {
            sb_append(&sb, "&quot;");
        } else {
            sb_append_char(&sb, *p);
        }
    }
    return sb_to_string(&sb);
}

static void serialize_node(const node *n, string_buffer *sb, const char *parent_name) {
    if (!n) return;

    switch (n->type) {
        case NODE_DOCUMENT:
            /* Document node: serialize children only */
            for (const node *child = n->first_child; child; child = child->next_sibling) {
                serialize_node(child, sb, NULL);
            }
            break;

        case NODE_DOCTYPE:
            sb_append(sb, "<!DOCTYPE ");
            if (n->name && n->name[0]) {
                sb_append(sb, n->name);
            } else {
                sb_append(sb, "html");
            }
            sb_append(sb, ">");
            break;

        case NODE_ELEMENT: {
            /* Opening tag */
            sb_append(sb, "<");
            sb_append(sb, n->name ? n->name : "");

            /* Attributes */
            for (size_t i = 0; i < n->attr_count; ++i) {
                sb_append(sb, " ");
                sb_append(sb, n->attrs[i].name ? n->attrs[i].name : "");
                sb_append(sb, "=\"");
                char *escaped = escape_attr_value(n->attrs[i].value);
                if (escaped) {
                    sb_append(sb, escaped);
                    free(escaped);
                }
                sb_append(sb, "\"");
            }

            sb_append(sb, ">");

            /* Children (with context-aware escaping) */
            int is_raw = is_raw_text_element(n->name);
            int is_rcdata = is_rcdata_element(n->name);

            for (const node *child = n->first_child; child; child = child->next_sibling) {
                if (child->type == NODE_TEXT && (is_raw || is_rcdata)) {
                    /* Raw text: no escaping for script/style */
                    /* RCDATA: escape for textarea/title */
                    if (is_raw) {
                        sb_append(sb, child->data ? child->data : "");
                    } else {
                        char *escaped = escape_html_text(child->data);
                        if (escaped) {
                            sb_append(sb, escaped);
                            free(escaped);
                        }
                    }
                } else {
                    serialize_node(child, sb, n->name);
                }
            }

            /* Closing tag (skip for void elements) */
            if (!is_void_element_serializer(n->name)) {
                sb_append(sb, "</");
                sb_append(sb, n->name ? n->name : "");
                sb_append(sb, ">");
            }
            break;
        }

        case NODE_TEXT: {
            /* Normal text: escape unless parent is raw text element */
            int parent_is_raw = parent_name && is_raw_text_element(parent_name);
            if (parent_is_raw) {
                sb_append(sb, n->data ? n->data : "");
            } else {
                char *escaped = escape_html_text(n->data);
                if (escaped) {
                    sb_append(sb, escaped);
                    free(escaped);
                }
            }
            break;
        }

        case NODE_COMMENT:
            sb_append(sb, "<!--");
            sb_append(sb, n->data ? n->data : "");
            sb_append(sb, "-->");
            break;
    }
}

char *tree_serialize_html(const node *root) {
    if (!root) return NULL;
    string_buffer sb;
    sb_init(&sb);
    serialize_node(root, &sb, NULL);
    return sb_to_string(&sb);
}
