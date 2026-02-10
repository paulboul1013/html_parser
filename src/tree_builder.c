#define _POSIX_C_SOURCE 200809L
#include "tree_builder.h"
#include "tokenizer.h"
#include "foreign.h"

#include <stdlib.h>
#include <string.h>

static void attach_attrs(node *n, const token_attr *src, size_t count) {
    if (!n || !src || count == 0) return;
    n->attrs = (node_attr *)malloc(count * sizeof(node_attr));
    if (!n->attrs) return;
    n->attr_count = count;
    for (size_t i = 0; i < count; ++i) {
        n->attrs[i].name  = src[i].name  ? strdup(src[i].name)  : NULL;
        n->attrs[i].value = src[i].value ? strdup(src[i].value) : NULL;
    }
}

/* Attach attributes with SVG attribute name adjustment */
static void attach_attrs_svg(node *n, const token_attr *src, size_t count) {
    if (!n || !src || count == 0) return;
    n->attrs = (node_attr *)malloc(count * sizeof(node_attr));
    if (!n->attrs) return;
    n->attr_count = count;
    for (size_t i = 0; i < count; ++i) {
        const char *aname = src[i].name ? svg_adjust_attr_name(src[i].name) : NULL;
        n->attrs[i].name  = aname ? strdup(aname) : NULL;
        n->attrs[i].value = src[i].value ? strdup(src[i].value) : NULL;
    }
}


#define STACK_MAX 256

typedef enum {
    MODE_INITIAL = 0,
    MODE_BEFORE_HTML,
    MODE_IN_HEAD,
    MODE_IN_BODY,
    MODE_IN_TABLE,
    MODE_IN_ROW,
    MODE_IN_CELL,
    MODE_IN_TABLE_BODY,
    MODE_IN_CAPTION,
    MODE_IN_SELECT,
    MODE_IN_SELECT_IN_TABLE,
    MODE_IN_TEMPLATE,
    MODE_AFTER_BODY,
    MODE_AFTER_AFTER_BODY
} insertion_mode;

typedef enum {
    DOC_NO_QUIRKS = 0,
    DOC_LIMITED_QUIRKS,
    DOC_QUIRKS
} doc_mode;

static int is_table_mode(insertion_mode mode) {
    return mode == MODE_IN_TABLE ||
           mode == MODE_IN_TABLE_BODY ||
           mode == MODE_IN_ROW ||
           mode == MODE_IN_CELL;
}

typedef enum {
    FMT_NONE = 0,
    FMT_A,
    FMT_B,
    FMT_BIG,
    FMT_CODE,
    FMT_EM,
    FMT_FONT,
    FMT_I,
    FMT_NOBR,
    FMT_S,
    FMT_SMALL,
    FMT_STRIKE,
    FMT_STRONG,
    FMT_TT,
    FMT_U,
    FMT_MARKER          /* sentinel: pushed at td/th/caption boundaries */
} fmt_tag;

typedef struct {
    fmt_tag tag;
    node *element;
} formatting_entry;

typedef struct {
    formatting_entry items[64];
    size_t count;
} formatting_list;

typedef struct {
    node *items[STACK_MAX];
    size_t size;
} node_stack;

static void stack_pop_until(node_stack *st, const char *name);
static int is_void_element(const char *name);
static int is_table_element(const char *name);
static int is_heading_element(const char *name);
static int stack_has_open_heading(node_stack *st);
static void stack_pop_until_heading(node_stack *st);
static node *clone_element_shallow(node *original);
static int handle_in_template_mode(const token *t, node *doc, node_stack *st,
                                   formatting_list *fmt, insertion_mode *mode,
                                   insertion_mode *template_mode_stack, int *template_mode_top,
                                   int *reprocess);

static void stack_init(node_stack *st) {
    st->size = 0;
}

static void stack_push(node_stack *st, node *n) {
    if (!n) return;
    if (st->size < STACK_MAX) {
        st->items[st->size++] = n;
    }
}

static node *stack_top(node_stack *st) {
    if (st->size == 0) return NULL;
    return st->items[st->size - 1];
}

static int stack_contains(node_stack *st, node *n) {
    for (size_t i = 0; i < st->size; ++i) {
        if (st->items[i] == n) return 1;
    }
    return 0;
}

static node *stack_pop(node_stack *st) {
    if (st->size == 0) return NULL;
    return st->items[--st->size];
}

static int stack_index_of(node_stack *st, node *n) {
    if (!st || !n) return -1;
    for (size_t i = 0; i < st->size; ++i) {
        if (st->items[i] == n) return (int)i;
    }
    return -1;
}

static void stack_remove_at(node_stack *st, size_t index) {
    if (!st || index >= st->size) return;
    for (size_t i = index; i + 1 < st->size; ++i) {
        st->items[i] = st->items[i + 1];
    }
    st->size--;
}

static void stack_insert_at(node_stack *st, size_t index, node *n) {
    if (!st || !n || st->size >= STACK_MAX) return;
    if (index > st->size) index = st->size;
    for (size_t i = st->size; i > index; --i) {
        st->items[i] = st->items[i - 1];
    }
    st->items[index] = n;
    st->size++;
}

static fmt_tag fmt_tag_from_name(const char *name) {
    if (!name) return FMT_NONE;
    if (strcmp(name, "a") == 0) return FMT_A;
    if (strcmp(name, "b") == 0) return FMT_B;
    if (strcmp(name, "big") == 0) return FMT_BIG;
    if (strcmp(name, "code") == 0) return FMT_CODE;
    if (strcmp(name, "i") == 0) return FMT_I;
    if (strcmp(name, "em") == 0) return FMT_EM;
    if (strcmp(name, "font") == 0) return FMT_FONT;
    if (strcmp(name, "nobr") == 0) return FMT_NOBR;
    if (strcmp(name, "s") == 0) return FMT_S;
    if (strcmp(name, "small") == 0) return FMT_SMALL;
    if (strcmp(name, "strike") == 0) return FMT_STRIKE;
    if (strcmp(name, "strong") == 0) return FMT_STRONG;
    if (strcmp(name, "tt") == 0) return FMT_TT;
    if (strcmp(name, "u") == 0) return FMT_U;
    return FMT_NONE;
}

static int stack_has_open_named(node_stack *st, const char *name) {
    if (!name) return 0;
    for (size_t i = st->size; i > 0; --i) {
        node *n = st->items[i - 1];
        if (n && n->name && strcmp(n->name, name) == 0) return 1;
    }
    return 0;
}

static int attrs_equal(const node *a, const node *b) {
    if (!a || !b) return 0;
    if (a->attr_count != b->attr_count) return 0;
    for (size_t i = 0; i < a->attr_count; ++i) {
        const char *an = a->attrs[i].name ? a->attrs[i].name : "";
        const char *av = a->attrs[i].value ? a->attrs[i].value : "";
        const char *bn = b->attrs[i].name ? b->attrs[i].name : "";
        const char *bv = b->attrs[i].value ? b->attrs[i].value : "";
        if (strcmp(an, bn) != 0 || strcmp(av, bv) != 0) return 0;
    }
    return 1;
}

static int in_template_context(node_stack *st) {
    return stack_has_open_named(st, "template");
}

static int stack_has_open_table_section(node_stack *st) {
    return stack_has_open_named(st, "thead") || stack_has_open_named(st, "tbody") || stack_has_open_named(st, "tfoot");
}

/* General scope barriers (WHATWG §13.2.4.2) */
static int is_scoping_element(const char *name) {
    if (!name) return 0;
    return strcmp(name, "applet") == 0 ||
           strcmp(name, "caption") == 0 ||
           strcmp(name, "html") == 0 ||
           strcmp(name, "table") == 0 ||
           strcmp(name, "td") == 0 ||
           strcmp(name, "th") == 0 ||
           strcmp(name, "marquee") == 0 ||
           strcmp(name, "object") == 0 ||
           strcmp(name, "template") == 0;
}

/* List item scope = general scope + ol, ul */
static int is_list_item_scoping_element(const char *name) {
    if (!name) return 0;
    return is_scoping_element(name) ||
           strcmp(name, "ol") == 0 ||
           strcmp(name, "ul") == 0;
}

/* Button scope = general scope + button */
static int is_button_scoping_element(const char *name) {
    if (!name) return 0;
    return is_scoping_element(name) ||
           strcmp(name, "button") == 0;
}

/* Table scope = html, table, template only */
static int is_table_scoping_element(const char *name) {
    if (!name) return 0;
    return strcmp(name, "html") == 0 ||
           strcmp(name, "table") == 0 ||
           strcmp(name, "template") == 0;
}

static int has_element_in_scope(node_stack *st, const char *name) {
    if (!name) return 0;
    for (size_t i = st->size; i > 0; --i) {
        node *n = st->items[i - 1];
        if (!n || !n->name) continue;
        if (n->ns == NS_HTML && strcmp(n->name, name) == 0) return 1;
        if (is_scoping_element_ns(n->name, n->ns)) return 0;
    }
    return 0;
}

static int has_element_in_list_item_scope(node_stack *st, const char *name) {
    if (!name) return 0;
    for (size_t i = st->size; i > 0; --i) {
        node *n = st->items[i - 1];
        if (!n || !n->name) continue;
        if (n->ns == NS_HTML && strcmp(n->name, name) == 0) return 1;
        if (n->ns != NS_HTML && is_scoping_element_ns(n->name, n->ns)) return 0;
        if (is_list_item_scoping_element(n->name)) return 0;
    }
    return 0;
}

static int has_element_in_button_scope(node_stack *st, const char *name) {
    if (!name) return 0;
    for (size_t i = st->size; i > 0; --i) {
        node *n = st->items[i - 1];
        if (!n || !n->name) continue;
        if (n->ns == NS_HTML && strcmp(n->name, name) == 0) return 1;
        if (n->ns != NS_HTML && is_scoping_element_ns(n->name, n->ns)) return 0;
        if (is_button_scoping_element(n->name)) return 0;
    }
    return 0;
}

static int has_element_in_table_scope(node_stack *st, const char *name) {
    if (!name) return 0;
    for (size_t i = st->size; i > 0; --i) {
        node *n = st->items[i - 1];
        if (!n || !n->name) continue;
        if (n->ns == NS_HTML && strcmp(n->name, name) == 0) return 1;
        if (n->ns != NS_HTML && is_scoping_element_ns(n->name, n->ns)) return 0;
        if (is_table_scoping_element(n->name)) return 0;
    }
    return 0;
}

/* WHATWG §13.2.6.3 — implied end tag elements */
static int is_implied_end_tag_element(const char *name) {
    if (!name) return 0;
    return strcmp(name, "dd") == 0 ||
           strcmp(name, "dt") == 0 ||
           strcmp(name, "li") == 0 ||
           strcmp(name, "optgroup") == 0 ||
           strcmp(name, "option") == 0 ||
           strcmp(name, "p") == 0 ||
           strcmp(name, "rb") == 0 ||
           strcmp(name, "rp") == 0 ||
           strcmp(name, "rt") == 0 ||
           strcmp(name, "rtc") == 0;
}

static void generate_implied_end_tags(node_stack *st) {
    while (st->size > 0) {
        node *top = stack_top(st);
        if (!top || !top->name || !is_implied_end_tag_element(top->name))
            break;
        stack_pop(st);
    }
}

static void generate_implied_end_tags_except(node_stack *st, const char *except) {
    while (st->size > 0) {
        node *top = stack_top(st);
        if (!top || !top->name || !is_implied_end_tag_element(top->name))
            break;
        if (except && strcmp(top->name, except) == 0)
            break;
        stack_pop(st);
    }
}

static void formatting_push(formatting_list *fl, fmt_tag tag, node *element) {
    if (!fl || tag == FMT_NONE || !element) return;
    size_t count_same = 0;
    size_t earliest_index = 0;
    for (size_t i = 0; i < fl->count; ++i) {
        if (fl->items[i].tag == tag && attrs_equal(fl->items[i].element, element)) {
            if (count_same == 0) earliest_index = i;
            count_same++;
        }
    }
    if (count_same >= 3) {
        for (size_t j = earliest_index + 1; j < fl->count; ++j) {
            fl->items[j - 1] = fl->items[j];
        }
        fl->count--;
    }
    if (fl->count < sizeof(fl->items) / sizeof(fl->items[0])) {
        fl->items[fl->count].tag = tag;
        fl->items[fl->count].element = element;
        fl->count++;
    }
}

/* Push a marker onto the active-formatting list (at td/th/caption entry). */
static void formatting_push_marker(formatting_list *fl) {
    if (!fl) return;
    if (fl->count < sizeof(fl->items) / sizeof(fl->items[0])) {
        fl->items[fl->count].tag    = FMT_MARKER;
        fl->items[fl->count].element = NULL;
        fl->count++;
    }
}

/* Remove entries from the end of the list until (and including) the last marker.
   If no marker is found the entire list is cleared. */
static void formatting_clear_to_marker(formatting_list *fl) {
    if (!fl) return;
    while (fl->count > 0) {
        fl->count--;
        if (fl->items[fl->count].tag == FMT_MARKER) {
            return;     /* marker itself is also removed */
        }
    }
}

static int is_template_head_element(const char *name) {
    if (!name) return 0;
    return strcmp(name, "base") == 0 ||
           strcmp(name, "basefont") == 0 ||
           strcmp(name, "bgsound") == 0 ||
           strcmp(name, "link") == 0 ||
           strcmp(name, "meta") == 0 ||
           strcmp(name, "noframes") == 0 ||
           strcmp(name, "noscript") == 0 ||
           strcmp(name, "script") == 0 ||
           strcmp(name, "style") == 0 ||
           strcmp(name, "template") == 0 ||
           strcmp(name, "title") == 0;
}

static void template_mode_replace(insertion_mode *stack, int *top, insertion_mode mode) {
    if (!stack || !top) return;
    if (*top > 0) (*top)--;
    if (*top < 64) {
        stack[(*top)++] = mode;
    }
}

static int stack_has_table_element(node_stack *st) {
    if (!st) return 0;
    for (size_t i = st->size; i > 0; --i) {
        node *n = st->items[i - 1];
        if (n && n->name && is_table_element(n->name)) return 1;
    }
    return 0;
}

static insertion_mode reset_insertion_mode_from_stack(node_stack *st) {
    if (!st || st->size == 0) return MODE_IN_BODY;
    int has_table = stack_has_table_element(st);
    for (size_t i = st->size; i > 0; --i) {
        node *n = st->items[i - 1];
        if (!n || !n->name) continue;
        if (strcmp(n->name, "select") == 0) {
            return has_table ? MODE_IN_SELECT_IN_TABLE : MODE_IN_SELECT;
        }
        if (strcmp(n->name, "td") == 0 || strcmp(n->name, "th") == 0) return MODE_IN_CELL;
        if (strcmp(n->name, "tr") == 0) return MODE_IN_ROW;
        if (strcmp(n->name, "tbody") == 0 || strcmp(n->name, "thead") == 0 || strcmp(n->name, "tfoot") == 0) {
            return MODE_IN_TABLE_BODY;
        }
        if (strcmp(n->name, "caption") == 0) return MODE_IN_CAPTION;
        if (strcmp(n->name, "table") == 0) return MODE_IN_TABLE;
        if (strcmp(n->name, "head") == 0) return MODE_IN_HEAD;
        if (strcmp(n->name, "body") == 0) return MODE_IN_BODY;
        if (strcmp(n->name, "html") == 0) return MODE_IN_BODY;
    }
    return MODE_IN_BODY;
}

static node *create_template_element(const token_attr *attrs, size_t attr_count) {
    node *tmpl = node_create(NODE_ELEMENT, "template", NULL);
    if (!tmpl) return NULL;
    attach_attrs(tmpl, attrs, attr_count);
    node *content = node_create(NODE_ELEMENT, "content", NULL);
    if (!content) {
        node_free(tmpl);
        return NULL;
    }
    node_append_child(tmpl, content);
    return tmpl;
}

static void open_template_element(node_stack *st, formatting_list *fl, insertion_mode *mode,
                                  insertion_mode *template_mode_stack, int *template_mode_top,
                                  node *tmpl, int self_closing) {
    if (!st || !tmpl || self_closing) return;
    stack_push(st, tmpl);
    if (tmpl->first_child) {
        stack_push(st, tmpl->first_child);
    }
    if (fl) formatting_push_marker(fl);
    if (mode && template_mode_stack && template_mode_top && *template_mode_top < 64) {
        template_mode_stack[(*template_mode_top)++] = MODE_IN_TEMPLATE;
    }
    if (mode) *mode = MODE_IN_TEMPLATE;
}

static void close_template_element(node_stack *st, formatting_list *fl, insertion_mode *mode,
                                   insertion_mode *template_mode_stack, int *template_mode_top) {
    if (!st) return;
    (void)template_mode_stack;
    stack_pop_until(st, "template");
    if (fl) formatting_clear_to_marker(fl);
    if (template_mode_top && *template_mode_top > 0) {
        (*template_mode_top)--;
    }
    if (mode) {
        *mode = reset_insertion_mode_from_stack(st);
    }
}

static int formatting_find_last(formatting_list *fl, fmt_tag tag) {
    if (!fl || tag == FMT_NONE) return -1;
    for (size_t i = fl->count; i > 0; --i) {
        if (fl->items[i - 1].tag == FMT_MARKER) return -1;
        if (fl->items[i - 1].tag == tag) return (int)(i - 1);
    }
    return -1;
}

static void formatting_remove_at(formatting_list *fl, size_t index) {
    if (!fl || index >= fl->count) return;
    for (size_t i = index; i + 1 < fl->count; ++i) {
        fl->items[i] = fl->items[i + 1];
    }
    fl->count--;
}

static void formatting_insert_at(formatting_list *fl, size_t index, fmt_tag tag, node *element) {
    if (!fl || fl->count >= sizeof(fl->items) / sizeof(fl->items[0])) return;
    if (index > fl->count) index = fl->count;
    for (size_t i = fl->count; i > index; --i) {
        fl->items[i] = fl->items[i - 1];
    }
    fl->items[index].tag = tag;
    fl->items[index].element = element;
    fl->count++;
}

static int formatting_index_of_element(formatting_list *fl, node *element) {
    if (!fl || !element) return -1;
    for (size_t i = 0; i < fl->count; ++i) {
        if (fl->items[i].element == element) return (int)i;
    }
    return -1;
}

static void reconstruct_active_formatting(node_stack *st, formatting_list *fl, node *parent) {
    if (!st || !fl || !parent) return;
    if (fl->count == 0) return;

    /* Per WHATWG: if the last entry is a marker or is already on the open-
       element stack there is nothing to reconstruct. */
    {
        formatting_entry *last = &fl->items[fl->count - 1];
        if (last->tag == FMT_MARKER) return;
        if (last->element && stack_contains(st, last->element)) return;
    }

    /* Walk backwards to find the boundary: stop at a marker or at an entry
       that is still on the stack.  'first' is the index we start from. */
    size_t first = 0;
    for (size_t i = fl->count - 1; i > 0; --i) {
        formatting_entry *e = &fl->items[i - 1];
        if (e->tag == FMT_MARKER ||
            (e->element && stack_contains(st, e->element))) {
            first = i;
            break;
        }
    }

    /* Reconstruct each entry in [first, count).  Markers are skipped. */
    for (size_t i = first; i < fl->count; ++i) {
        if (fl->items[i].tag == FMT_MARKER) continue;
        node *source = fl->items[i].element;
        if (!source) continue;
        node *n = clone_element_shallow(source);
        if (!n) continue;
        node_append_child(parent, n);
        stack_push(st, n);
        fl->items[i].element = n;
        parent = n;
    }
}

static int is_foster_parent_target(const char *name);
static void foster_insert(node_stack *st, node *doc, node *child);

/* Adoption Agency Algorithm (WHATWG §13.2.6.4).
   Returns 1 if the token was consumed (caller should break),
   0 if tag_name is not a formatting element (caller handles normally). */
static int adoption_agency(node_stack *st, formatting_list *fl, node *doc,
                           const char *tag_name) {
    fmt_tag ft = fmt_tag_from_name(tag_name);
    if (ft == FMT_NONE) return 0;

    /* WHATWG step 2: current node is the element and not in active list → simple pop */
    {
        node *cur = stack_top(st);
        if (cur && cur->name && strcmp(cur->name, tag_name) == 0 &&
            formatting_index_of_element(fl, cur) < 0) {
            stack_pop(st);
            return 1;
        }
    }

    /* Outer loop: at most 8 iterations */
    for (int outer = 0; outer < 8; ++outer) {
        /* Step 4c: find formatting element in active list (last before marker) */
        int fmt_idx = formatting_find_last(fl, ft);
        if (fmt_idx < 0) return 0;  /* not in active list → "any other end tag" */

        node *formatting_element = fl->items[fmt_idx].element;

        /* Step 4e: not in stack → remove from list and return */
        int fe_stack_idx = stack_index_of(st, formatting_element);
        if (fe_stack_idx < 0) {
            formatting_remove_at(fl, (size_t)fmt_idx);
            return 1;
        }

        /* Step 4f: not in scope → parse error, return */
        if (!has_element_in_scope(st, tag_name)) {
            return 1;
        }

        /* Step 4h: find furthest block (first special element above fe in stack) */
        node *furthest_block = NULL;
        int fb_stack_idx = -1;
        for (size_t i = (size_t)fe_stack_idx + 1; i < st->size; ++i) {
            if (st->items[i] && st->items[i]->name &&
                is_special_element_ns(st->items[i]->name, st->items[i]->ns)) {
                furthest_block = st->items[i];
                fb_stack_idx = (int)i;
                break;
            }
        }

        /* Step 4i: no furthest block → pop to formatting element and remove */
        if (!furthest_block) {
            while (st->size > 0) {
                node *popped = stack_pop(st);
                if (popped == formatting_element) break;
            }
            formatting_remove_at(fl, (size_t)fmt_idx);
            return 1;
        }

        /* Step 4j: common ancestor */
        node *common_ancestor = (fe_stack_idx > 0) ? st->items[fe_stack_idx - 1] : doc;

        /* Step 4k: bookmark = position in formatting list */
        size_t bookmark = (size_t)fmt_idx;

        /* Step 4l-n: inner loop */
        node *inner_node = furthest_block;
        node *last_node = furthest_block;

        for (int inner = 1; inner <= 8; ++inner) {
            /* Step n.2: move to element above inner_node in stack (toward fe) */
            int node_si = stack_index_of(st, inner_node);
            if (node_si <= 0) break;
            inner_node = st->items[node_si - 1];

            /* Step n.3: if node is formatting element, break */
            if (inner_node == formatting_element) break;

            /* Step n.4-5: check if inner_node is in formatting list */
            int node_fi = formatting_index_of_element(fl, inner_node);
            if (inner > 3 && node_fi >= 0) {
                formatting_remove_at(fl, (size_t)node_fi);
                if ((size_t)node_fi < bookmark) bookmark--;
                /* recalculate fmt_idx if affected */
                fmt_idx = formatting_index_of_element(fl, formatting_element);
                node_fi = -1;
            }

            /* Step n.5 cont: not in formatting list → remove from stack, continue */
            if (node_fi < 0) {
                stack_remove_at(st, (size_t)(node_si - 1));
                /* fb_stack_idx may shift */
                fb_stack_idx = stack_index_of(st, furthest_block);
                continue;
            }

            /* Step n.6: create replacement for inner_node */
            node *replacement = clone_element_shallow(inner_node);
            if (!replacement) break;

            /* Replace in formatting list */
            fl->items[node_fi].element = replacement;

            /* Replace in stack */
            int cur_si = stack_index_of(st, inner_node);
            if (cur_si >= 0) st->items[cur_si] = replacement;

            /* Replace in tree: replacement takes inner_node's position and children */
            node_reparent_children(inner_node, replacement);
            if (inner_node->parent) {
                node_insert_before(inner_node->parent, replacement, inner_node);
                node_remove_child(inner_node->parent, inner_node);
            }
            node_free_shallow(inner_node);
            inner_node = replacement;

            /* Step n.7: if last_node is furthest_block, move bookmark */
            if (last_node == furthest_block) {
                bookmark = (size_t)node_fi + 1;
            }

            /* Step n.8: append last_node to inner_node */
            if (last_node->parent) {
                node_remove_child(last_node->parent, last_node);
            }
            node_append_child(inner_node, last_node);

            /* Step n.9 */
            last_node = inner_node;
        }

        /* Step 4o: insert last_node at appropriate place in common_ancestor */
        if (last_node->parent) {
            node_remove_child(last_node->parent, last_node);
        }
        if (common_ancestor->name && is_foster_parent_target(common_ancestor->name)) {
            foster_insert(st, doc, last_node);
        } else {
            node_append_child(common_ancestor, last_node);
        }

        /* Step 4p: create replacement for formatting element */
        node *new_element = clone_element_shallow(formatting_element);
        if (!new_element) {
            /* fallback: just remove from list and stack */
            formatting_remove_at(fl, (size_t)fmt_idx);
            stack_remove_at(st, (size_t)stack_index_of(st, formatting_element));
            return 1;
        }

        /* Step 4q: move all children of furthest_block to new_element */
        node_reparent_children(furthest_block, new_element);

        /* Step 4r: append new_element to furthest_block */
        node_append_child(furthest_block, new_element);

        /* Step 4s: update formatting list */
        fmt_idx = formatting_index_of_element(fl, formatting_element);
        if (fmt_idx >= 0) {
            formatting_remove_at(fl, (size_t)fmt_idx);
            if ((size_t)fmt_idx < bookmark && bookmark > 0) bookmark--;
        }
        if (bookmark > fl->count) bookmark = fl->count;
        formatting_insert_at(fl, bookmark, ft, new_element);

        /* Step 4t: update stack */
        int fe_si = stack_index_of(st, formatting_element);
        if (fe_si >= 0) stack_remove_at(st, (size_t)fe_si);
        fb_stack_idx = stack_index_of(st, furthest_block);
        if (fb_stack_idx >= 0) {
            stack_insert_at(st, (size_t)fb_stack_idx + 1, new_element);
        }
    }

    return 1;
}

static void stack_pop_until(node_stack *st, const char *name) {
    if (!name) return;
    while (st->size > 0) {
        node *n = stack_top(st);
        if (n && n->name && strcmp(n->name, name) == 0) {
            stack_pop(st);
            return;
        }
        stack_pop(st);
    }
}

static const char *stack_pop_until_any(node_stack *st, const char *a, const char *b) {
    while (st->size > 0) {
        node *n = stack_top(st);
        if (n && n->name) {
            if (a && strcmp(n->name, a) == 0) {
                stack_pop(st);
                return a;
            }
            if (b && strcmp(n->name, b) == 0) {
                stack_pop(st);
                return b;
            }
        }
        stack_pop(st);
    }
    return NULL;
}

static node *current_node(node_stack *st, node *doc) {
    node *top = stack_top(st);
    return top ? top : doc;
}

static node *find_open_table(node_stack *st) {
    for (size_t i = st->size; i > 0; --i) {
        node *n = st->items[i - 1];
        if (n && n->name && strcmp(n->name, "table") == 0) {
            return n;
        }
    }
    return NULL;
}

static node *foster_parent(node_stack *st, node *doc, node **table_out) {
    node *table = find_open_table(st);
    if (table_out) *table_out = table;
    if (table && table->parent) {
        return table->parent;
    }
    return current_node(st, doc);
}

static void foster_insert(node_stack *st, node *doc, node *child) {
    node *table = NULL;
    node *parent = foster_parent(st, doc, &table);
    if (table && parent == table->parent) {
        node_insert_before(parent, child, table);
    } else {
        node_append_child(parent, child);
    }
}

static node *ensure_html(node *doc, node_stack *st, node **html_out);
static node *ensure_body(node *doc, node_stack *st, node **html_out, node **body_out);

static int is_head_element(const char *name) {
    if (!name) return 0;
    return strcmp(name, "base") == 0 ||
           strcmp(name, "link") == 0 ||
           strcmp(name, "meta") == 0 ||
           strcmp(name, "style") == 0 ||
           strcmp(name, "template") == 0 ||
           strcmp(name, "title") == 0 ||
           strcmp(name, "script") == 0;
}

static void body_autoclose_on_start(node_stack *st, const char *tag_name, doc_mode dmode);

static insertion_mode fragment_mode_for_context(const char *name) {
    if (!name) return MODE_IN_BODY;
    if (strcmp(name, "table") == 0) return MODE_IN_TABLE;
    if (strcmp(name, "tbody") == 0 || strcmp(name, "thead") == 0 || strcmp(name, "tfoot") == 0) return MODE_IN_TABLE_BODY;
    if (strcmp(name, "tr") == 0) return MODE_IN_ROW;
    if (strcmp(name, "td") == 0 || strcmp(name, "th") == 0) return MODE_IN_CELL;
    if (strcmp(name, "caption") == 0) return MODE_IN_CAPTION;
    if (strcmp(name, "select") == 0) return MODE_IN_SELECT;
    if (strcmp(name, "head") == 0) return MODE_IN_HEAD;
    return MODE_IN_BODY;
}

static int is_body_ignored_start(const char *name);
static int is_void_element(const char *name);

static void handle_in_body_start_fragment(const char *name, int self_closing, node *doc, node_stack *st,
                                          formatting_list *fmt, insertion_mode *mode,
                                          insertion_mode *template_mode_stack, int *template_mode_top,
                                          doc_mode dmode,
                                          const token_attr *attrs, size_t attr_count) {
    /* Table-related tags are parse errors in IN_BODY; ignore them. */
    if (is_body_ignored_start(name)) {
        return;
    }
    if (is_heading_element(name) && stack_has_open_heading(st)) {
        stack_pop_until_heading(st);
    }
    if (name && (strcmp(name, "applet") == 0 || strcmp(name, "marquee") == 0 || strcmp(name, "object") == 0)) {
        node *parent = current_node(st, doc);
        node *n = node_create(NODE_ELEMENT, name, NULL);
        attach_attrs(n, attrs, attr_count);
        node_append_child(parent, n);
        formatting_push_marker(fmt);
        if (!self_closing) stack_push(st, n);
        return;
    }
    /* <table> closes an open <p> (unless quirks mode), then switches to IN_TABLE. */
    if (name && strcmp(name, "table") == 0) {
        if (dmode != DOC_QUIRKS && has_element_in_button_scope(st, "p")) {
            stack_pop_until(st, "p");
        }
        node *parent = current_node(st, doc);
        node *n = node_create(NODE_ELEMENT, "table", NULL);
        attach_attrs(n, attrs, attr_count);
        node_append_child(parent, n);
        stack_push(st, n);
        *mode = MODE_IN_TABLE;
        return;
    }
    /* <select> in body switches to IN_SELECT. */
    if (name && strcmp(name, "select") == 0) {
        node *parent = current_node(st, doc);
        node *n = node_create(NODE_ELEMENT, "select", NULL);
        attach_attrs(n, attrs, attr_count);
        node_append_child(parent, n);
        stack_push(st, n);
        *mode = MODE_IN_SELECT;
        return;
    }
    /* SVG / MathML enter foreign content */
    if (name && strcmp(name, "svg") == 0) {
        reconstruct_active_formatting(st, fmt, current_node(st, doc));
        node *n = node_create_ns(NODE_ELEMENT, "svg", NULL, NS_SVG);
        attach_attrs_svg(n, attrs, attr_count);
        node_append_child(current_node(st, doc), n);
        if (!self_closing) stack_push(st, n);
        return;
    }
    if (name && strcmp(name, "math") == 0) {
        reconstruct_active_formatting(st, fmt, current_node(st, doc));
        node *n = node_create_ns(NODE_ELEMENT, "math", NULL, NS_MATHML);
        attach_attrs(n, attrs, attr_count);
        node_append_child(current_node(st, doc), n);
        if (!self_closing) stack_push(st, n);
        return;
    }
    if (name && strcmp(name, "template") == 0) {
        node *parent = current_node(st, doc);
        node *tmpl = create_template_element(attrs, attr_count);
        if (!tmpl) return;
        node_append_child(parent, tmpl);
        open_template_element(st, fmt, mode, template_mode_stack, template_mode_top, tmpl, self_closing);
        return;
    }
    fmt_tag ft = fmt_tag_from_name(name);
    if (ft != FMT_NONE) {
        node *parent = current_node(st, doc);
        reconstruct_active_formatting(st, fmt, parent);
    }
    body_autoclose_on_start(st, name, DOC_NO_QUIRKS);
    node *parent = current_node(st, doc);
    node *n = node_create(NODE_ELEMENT, name ? name : "", NULL);
    attach_attrs(n, attrs, attr_count);
    node_append_child(parent, n);
    if (!self_closing && !is_void_element(name)) {
        stack_push(st, n);
        if (ft != FMT_NONE) formatting_push(fmt, ft, n);
    }
}

static int is_table_element(const char *name) {
    if (!name) return 0;
    return strcmp(name, "table") == 0 ||
           strcmp(name, "tbody") == 0 ||
           strcmp(name, "thead") == 0 ||
           strcmp(name, "tfoot") == 0 ||
           strcmp(name, "tr") == 0 ||
           strcmp(name, "td") == 0 ||
           strcmp(name, "th") == 0 ||
           strcmp(name, "caption") == 0 ||
           strcmp(name, "colgroup") == 0 ||
           strcmp(name, "col") == 0;
}

static int is_table_section_element(const char *name) {
    if (!name) return 0;
    return strcmp(name, "tbody") == 0 ||
           strcmp(name, "thead") == 0 ||
           strcmp(name, "tfoot") == 0;
}

static int is_cell_element(const char *name) {
    if (!name) return 0;
    return strcmp(name, "td") == 0 || strcmp(name, "th") == 0;
}

static int is_select_child_element(const char *name) {
    if (!name) return 0;
    return strcmp(name, "option") == 0 || strcmp(name, "optgroup") == 0;
}

static int is_body_ignored_start(const char *name) {
    if (!name) return 0;
    return strcmp(name, "caption") == 0 ||
           strcmp(name, "col") == 0 ||
           strcmp(name, "colgroup") == 0 ||
           strcmp(name, "frame") == 0 ||
           strcmp(name, "head") == 0 ||
           strcmp(name, "tbody") == 0 ||
           strcmp(name, "td") == 0 ||
           strcmp(name, "tfoot") == 0 ||
           strcmp(name, "th") == 0 ||
           strcmp(name, "thead") == 0 ||
           strcmp(name, "tr") == 0;
}

static int is_void_element(const char *name) {
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

static node *clone_element_shallow(node *original) {
    if (!original) return NULL;
    node *n = node_create(NODE_ELEMENT, original->name, NULL);
    if (!n) return NULL;
    n->ns = original->ns;
    if (original->attrs && original->attr_count > 0) {
        n->attrs = (node_attr *)malloc(original->attr_count * sizeof(node_attr));
        if (n->attrs) {
            n->attr_count = original->attr_count;
            for (size_t i = 0; i < original->attr_count; ++i) {
                n->attrs[i].name  = original->attrs[i].name  ? strdup(original->attrs[i].name)  : NULL;
                n->attrs[i].value = original->attrs[i].value ? strdup(original->attrs[i].value) : NULL;
            }
        }
    }
    return n;
}

static int is_foster_parent_target(const char *name) {
    if (!name) return 0;
    return strcmp(name, "table") == 0 ||
           strcmp(name, "tbody") == 0 ||
           strcmp(name, "tfoot") == 0 ||
           strcmp(name, "thead") == 0 ||
           strcmp(name, "tr") == 0;
}

static int is_body_block_like_start(const char *name) {
    if (!name) return 0;
    /* Minimal set: enough to demonstrate auto-closing <p>/<li> cleanly. */
    return strcmp(name, "address") == 0 ||
           strcmp(name, "article") == 0 ||
           strcmp(name, "aside") == 0 ||
           strcmp(name, "blockquote") == 0 ||
           strcmp(name, "div") == 0 ||
           strcmp(name, "dl") == 0 ||
           strcmp(name, "fieldset") == 0 ||
           strcmp(name, "footer") == 0 ||
           strcmp(name, "form") == 0 ||
           strcmp(name, "h1") == 0 || strcmp(name, "h2") == 0 ||
           strcmp(name, "h3") == 0 || strcmp(name, "h4") == 0 ||
           strcmp(name, "h5") == 0 || strcmp(name, "h6") == 0 ||
           strcmp(name, "header") == 0 ||
           strcmp(name, "hr") == 0 ||
           strcmp(name, "main") == 0 ||
           strcmp(name, "nav") == 0 ||
           strcmp(name, "ol") == 0 ||
           strcmp(name, "p") == 0 ||
           strcmp(name, "pre") == 0 ||
           strcmp(name, "section") == 0 ||
           strcmp(name, "table") == 0 ||
           strcmp(name, "ul") == 0;
}

static int is_heading_element(const char *name) {
    if (!name) return 0;
    return strcmp(name, "h1") == 0 || strcmp(name, "h2") == 0 ||
           strcmp(name, "h3") == 0 || strcmp(name, "h4") == 0 ||
           strcmp(name, "h5") == 0 || strcmp(name, "h6") == 0;
}

static int stack_has_open_heading(node_stack *st) {
    if (!st) return 0;
    for (size_t i = st->size; i > 0; --i) {
        node *n = st->items[i - 1];
        if (n && n->name && is_heading_element(n->name)) {
            return 1;
        }
    }
    return 0;
}

static void stack_pop_until_heading(node_stack *st) {
    if (!st) return;
    while (st->size > 0) {
        node *n = stack_top(st);
        if (n && n->name && is_heading_element(n->name)) {
            stack_pop(st);
            return;
        }
        stack_pop(st);
    }
}

static char to_lower_ascii_local(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static int is_ci_equal(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (to_lower_ascii_local(*a) != to_lower_ascii_local(*b)) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int starts_with_ci_str(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    while (*prefix) {
        if (*s == '\0') return 0;
        if (to_lower_ascii_local(*s) != to_lower_ascii_local(*prefix)) return 0;
        s++;
        prefix++;
    }
    return 1;
}

static int is_quirks_public_prefix(const char *public_id) {
    static const char *prefixes[] = {
        "+//silmaril//dtd html pro v0r11 19970101//",
        "-//advasoft ltd//dtd html 3.0 aswedit + extensions//",
        "-//as//dtd html 3.0 aswedit + extensions//",
        "-//ietf//dtd html 2.0 level 1//",
        "-//ietf//dtd html 2.0 level 2//",
        "-//ietf//dtd html 2.0 strict level 1//",
        "-//ietf//dtd html 2.0 strict level 2//",
        "-//ietf//dtd html 2.0 strict//",
        "-//ietf//dtd html 2.0//",
        "-//ietf//dtd html 2.1e//",
        "-//ietf//dtd html 3.0//",
        "-//ietf//dtd html 3.2 final//",
        "-//ietf//dtd html 3.2//",
        "-//ietf//dtd html 3//",
        "-//ietf//dtd html level 0//",
        "-//ietf//dtd html level 1//",
        "-//ietf//dtd html level 2//",
        "-//ietf//dtd html level 3//",
        "-//ietf//dtd html strict level 0//",
        "-//ietf//dtd html strict level 1//",
        "-//ietf//dtd html strict level 2//",
        "-//ietf//dtd html strict level 3//",
        "-//ietf//dtd html strict//",
        "-//ietf//dtd html//",
        "-//metrius//dtd metrius presentational//",
        "-//microsoft//dtd internet explorer 2.0 html strict//",
        "-//microsoft//dtd internet explorer 2.0 html//",
        "-//microsoft//dtd internet explorer 2.0 tables//",
        "-//microsoft//dtd internet explorer 3.0 html strict//",
        "-//microsoft//dtd internet explorer 3.0 html//",
        "-//microsoft//dtd internet explorer 3.0 tables//",
        "-//netscape comm. corp.//dtd html//",
        "-//netscape comm. corp.//dtd strict html//",
        "-//o'reilly and associates//dtd html 2.0//",
        "-//o'reilly and associates//dtd html extended 1.0//",
        "-//o'reilly and associates//dtd html extended relaxed 1.0//",
        "-//sq//dtd html 2.0 hotmetal + extensions//",
        "-//softquad software//dtd hotmetal pro 6.0::19990601::extensions to html 4.0//",
        "-//softquad//dtd hotmetal pro 4.0::19971010::extensions to html 4.0//",
        "-//spyglass//dtd html 2.0 extended//",
        "-//sun microsystems corp.//dtd hotjava html//",
        "-//sun microsystems corp.//dtd hotjava strict html//",
        "-//w3c//dtd html 3 1995-03-24//",
        "-//w3c//dtd html 3.2 draft//",
        "-//w3c//dtd html 3.2 final//",
        "-//w3c//dtd html 3.2//",
        "-//w3c//dtd html 3.2s draft//",
        "-//w3c//dtd html 4.0 frameset//",
        "-//w3c//dtd html 4.0 transitional//",
        "-//w3c//dtd html experimental 19960712//",
        "-//w3c//dtd html experimental 970421//",
        "-//w3c//dtd w3 html//",
        "-//w3o//dtd w3 html 3.0//",
        "-//webtechs//dtd mozilla html 2.0//",
        "-//webtechs//dtd mozilla html//"
    };
    if (!public_id) return 0;
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        if (starts_with_ci_str(public_id, prefixes[i])) return 1;
    }
    return 0;
}

static int is_quirks_public_prefix_missing_system(const char *public_id) {
    static const char *prefixes[] = {
        "-//w3c//dtd html 4.01 frameset//",
        "-//w3c//dtd html 4.01 transitional//"
    };
    if (!public_id) return 0;
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        if (starts_with_ci_str(public_id, prefixes[i])) return 1;
    }
    return 0;
}

static int is_limited_quirks_public_prefix(const char *public_id) {
    static const char *prefixes[] = {
        "-//w3c//dtd xhtml 1.0 frameset//",
        "-//w3c//dtd xhtml 1.0 transitional//"
    };
    if (!public_id) return 0;
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        if (starts_with_ci_str(public_id, prefixes[i])) return 1;
    }
    return 0;
}

static int is_limited_quirks_public_prefix_system_not_missing(const char *public_id) {
    static const char *prefixes[] = {
        "-//w3c//dtd html 4.01 frameset//",
        "-//w3c//dtd html 4.01 transitional//"
    };
    if (!public_id) return 0;
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        if (starts_with_ci_str(public_id, prefixes[i])) return 1;
    }
    return 0;
}

static doc_mode determine_doc_mode(const token *t) {
    if (!t || t->type != TOKEN_DOCTYPE) return DOC_NO_QUIRKS;
    if (t->force_quirks) return DOC_QUIRKS;
    if (!t->name || strcmp(t->name, "html") != 0) return DOC_QUIRKS;
    const char *public_id = t->public_id;
    const char *system_id = t->system_id;
    int system_missing = (system_id == NULL);

    if (public_id && is_ci_equal(public_id, "-//w3o//dtd w3 html strict 3.0//en//")) return DOC_QUIRKS;
    if (public_id && is_ci_equal(public_id, "-/w3c/dtd html 4.0 transitional/en")) return DOC_QUIRKS;
    if (public_id && is_ci_equal(public_id, "html")) return DOC_QUIRKS;
    if (system_id && is_ci_equal(system_id, "http://www.ibm.com/data/dtd/v11/ibmxhtml1-transitional.dtd")) return DOC_QUIRKS;
    if (public_id && is_quirks_public_prefix(public_id)) return DOC_QUIRKS;
    if (public_id && system_missing && is_quirks_public_prefix_missing_system(public_id)) return DOC_QUIRKS;

    if (public_id && is_limited_quirks_public_prefix(public_id)) return DOC_LIMITED_QUIRKS;
    if (public_id && system_id && is_limited_quirks_public_prefix_system_not_missing(public_id)) return DOC_LIMITED_QUIRKS;

    return DOC_NO_QUIRKS;
}

static void body_autoclose_on_start(node_stack *st, const char *tag_name, doc_mode dmode) {
    (void)dmode;
    if (!tag_name) return;

    /* Auto-close <p> when a block-like element starts, or a new <p> starts. */
    if ((strcmp(tag_name, "p") == 0 || is_body_block_like_start(tag_name)) && has_element_in_button_scope(st, "p")) {
        generate_implied_end_tags_except(st, "p");
        stack_pop_until(st, "p");
    }

    /* Auto-close previous <li> when a new <li> starts. */
    if (strcmp(tag_name, "li") == 0 && has_element_in_list_item_scope(st, "li")) {
        generate_implied_end_tags_except(st, "li");
        stack_pop_until(st, "li");
    }

    /* Auto-close <dt>/<dd> when the other starts. */
    if (strcmp(tag_name, "dt") == 0 || strcmp(tag_name, "dd") == 0) {
        if (has_element_in_scope(st, "dd")) {
            generate_implied_end_tags_except(st, "dd");
            stack_pop_until(st, "dd");
        }
        if (has_element_in_scope(st, "dt")) {
            generate_implied_end_tags_except(st, "dt");
            stack_pop_until(st, "dt");
        }
    }

    /* Auto-close table sections and rows/cells on start of same kind. */
    if (is_table_section_element(tag_name) && (stack_has_open_named(st, "thead") || stack_has_open_named(st, "tbody") || stack_has_open_named(st, "tfoot"))) {
        stack_pop_until(st, "thead");
        stack_pop_until(st, "tbody");
        stack_pop_until(st, "tfoot");
    }
    if (strcmp(tag_name, "tr") == 0 && stack_has_open_named(st, "tr")) {
        stack_pop_until(st, "tr");
    }
    if ((strcmp(tag_name, "td") == 0 || strcmp(tag_name, "th") == 0) && (stack_has_open_named(st, "td") || stack_has_open_named(st, "th"))) {
        stack_pop_until_any(st, "td", "th");
    }
}

static void handle_in_body_start(const char *name, int self_closing, node *doc, node_stack *st, node **html, node **body,
                                 formatting_list *fmt, insertion_mode *mode,
                                 insertion_mode *template_mode_stack, int *template_mode_top,
                                 doc_mode dmode,
                                 const token_attr *attrs, size_t attr_count) {
    if (!mode) return;
    int in_template = in_template_context(st);
    fmt_tag ft = fmt_tag_from_name(name);
    if (ft != FMT_NONE) {
        node *parent = current_node(st, doc);
        reconstruct_active_formatting(st, fmt, parent);
    }
    if (name && strcmp(name, "html") == 0) {
        return;
    }
    if (name && is_heading_element(name)) {
        if (stack_has_open_heading(st)) {
            stack_pop_until_heading(st);
        }
    }
    if (name && strcmp(name, "body") == 0) {
        if (!in_template) {
            ensure_body(doc, st, html, body);
        }
        return;
    }
    if (name && strcmp(name, "select") == 0) {
        if (!in_template) {
            ensure_body(doc, st, html, body);
        }
        node *parent = current_node(st, doc);
        node *n = node_create(NODE_ELEMENT, "select", NULL);
        attach_attrs(n, attrs, attr_count);
        node_append_child(parent, n);
        stack_push(st, n);
        *mode = MODE_IN_SELECT;
        return;
    }
    if (name && strcmp(name, "table") == 0) {
        if (dmode != DOC_QUIRKS && has_element_in_button_scope(st, "p")) {
            stack_pop_until(st, "p");
        }
        if (!in_template) {
            ensure_body(doc, st, html, body);
        }
        node *parent = current_node(st, doc);
        node *n = node_create(NODE_ELEMENT, "table", NULL);
        attach_attrs(n, attrs, attr_count);
        node_append_child(parent, n);
        stack_push(st, n);
        *mode = MODE_IN_TABLE;
        return;
    }
    /* SVG / MathML enter foreign content */
    if (name && strcmp(name, "svg") == 0) {
        reconstruct_active_formatting(st, fmt, current_node(st, doc));
        if (!in_template) {
            ensure_body(doc, st, html, body);
        }
        node *n = node_create_ns(NODE_ELEMENT, "svg", NULL, NS_SVG);
        attach_attrs_svg(n, attrs, attr_count);
        node_append_child(current_node(st, doc), n);
        if (!self_closing) stack_push(st, n);
        return;
    }
    if (name && strcmp(name, "math") == 0) {
        reconstruct_active_formatting(st, fmt, current_node(st, doc));
        if (!in_template) {
            ensure_body(doc, st, html, body);
        }
        node *n = node_create_ns(NODE_ELEMENT, "math", NULL, NS_MATHML);
        attach_attrs(n, attrs, attr_count);
        node_append_child(current_node(st, doc), n);
        if (!self_closing) stack_push(st, n);
        return;
    }
    if (name && (strcmp(name, "applet") == 0 || strcmp(name, "marquee") == 0 || strcmp(name, "object") == 0)) {
        if (!in_template) {
            ensure_body(doc, st, html, body);
        }
        node *parent = current_node(st, doc);
        node *n = node_create(NODE_ELEMENT, name, NULL);
        attach_attrs(n, attrs, attr_count);
        node_append_child(parent, n);
        formatting_push_marker(fmt);
        if (!self_closing) stack_push(st, n);
        return;
    }
    if (name && strcmp(name, "template") == 0) {
        if (!in_template) {
            ensure_body(doc, st, html, body);
        }
        node *parent = current_node(st, doc);
        node *tmpl = create_template_element(attrs, attr_count);
        if (!tmpl) return;
        node_append_child(parent, tmpl);
        open_template_element(st, fmt, mode, template_mode_stack, template_mode_top, tmpl, self_closing);
        return;
    }
    body_autoclose_on_start(st, name, dmode);
    if (!in_template) {
        ensure_body(doc, st, html, body);
    }
    node *parent = current_node(st, doc);
    node *n = node_create(NODE_ELEMENT, name ? name : "", NULL);
    attach_attrs(n, attrs, attr_count);
    node_append_child(parent, n);
    if (!self_closing && !is_void_element(name)) {
        stack_push(st, n);
        if (ft != FMT_NONE) formatting_push(fmt, ft, n);
    }
}

static void close_cell(node_stack *st, formatting_list *fl) {
    if (!stack_has_open_named(st, "td") && !stack_has_open_named(st, "th")) {
        return;
    }
    stack_pop_until_any(st, "td", "th");
    formatting_clear_to_marker(fl);     /* clear active-formatting back to the cell marker */
}

static int is_all_whitespace(const char *s) {
    if (!s) return 1;
    for (size_t i = 0; s[i]; ++i) {
        char c = s[i];
        if (!(c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\f')) {
            return 0;
        }
    }
    return 1;
}

static int handle_in_template_mode(const token *t, node *doc, node_stack *st,
                                   formatting_list *fmt, insertion_mode *mode,
                                   insertion_mode *template_mode_stack, int *template_mode_top,
                                   int *reprocess) {
    if (!t || !st || !mode || !reprocess) return 0;

    switch (t->type) {
        case TOKEN_CHARACTER:
            if (t->data && t->data[0] != '\0') {
                if (is_all_whitespace(t->data)) {
                    return 1;
                }
                template_mode_replace(template_mode_stack, template_mode_top, MODE_IN_BODY);
                *mode = MODE_IN_BODY;
                *reprocess = 1;
            }
            return 1;

        case TOKEN_COMMENT: {
            node *parent = current_node(st, doc);
            node *n = node_create(NODE_COMMENT, NULL, t->data ? t->data : "");
            node_append_child(parent, n);
            return 1;
        }

        case TOKEN_DOCTYPE:
            return 1; /* ignore */

        case TOKEN_END_TAG:
            if (t->name && strcmp(t->name, "template") == 0 && stack_has_open_named(st, "template")) {
                close_template_element(st, fmt, mode, template_mode_stack, template_mode_top);
            }
            return 1;

        case TOKEN_START_TAG:
            if (t->name && is_template_head_element(t->name)) {
                node *parent = current_node(st, doc);
                if (strcmp(t->name, "template") == 0) {
                    node *tmpl = create_template_element(t->attrs, t->attr_count);
                    if (tmpl) {
                        node_append_child(parent, tmpl);
                        open_template_element(st, fmt, mode, template_mode_stack, template_mode_top,
                                              tmpl, t->self_closing);
                    }
                } else {
                    node *n = node_create(NODE_ELEMENT, t->name ? t->name : "", NULL);
                    attach_attrs(n, t->attrs, t->attr_count);
                    node_append_child(parent, n);
                    if (!t->self_closing && !is_void_element(t->name)) {
                        stack_push(st, n);
                    }
                }
                return 1;
            }

            if (t->name) {
                if (strcmp(t->name, "caption") == 0 ||
                    strcmp(t->name, "colgroup") == 0 ||
                    strcmp(t->name, "tbody") == 0 ||
                    strcmp(t->name, "tfoot") == 0 ||
                    strcmp(t->name, "thead") == 0 ||
                    strcmp(t->name, "table") == 0 ||
                    strcmp(t->name, "col") == 0) {
                    template_mode_replace(template_mode_stack, template_mode_top, MODE_IN_TABLE);
                    *mode = MODE_IN_TABLE;
                    *reprocess = 1;
                    return 1;
                }
                if (strcmp(t->name, "tr") == 0) {
                    template_mode_replace(template_mode_stack, template_mode_top, MODE_IN_TABLE_BODY);
                    *mode = MODE_IN_TABLE_BODY;
                    *reprocess = 1;
                    return 1;
                }
                if (strcmp(t->name, "td") == 0 || strcmp(t->name, "th") == 0) {
                    template_mode_replace(template_mode_stack, template_mode_top, MODE_IN_ROW);
                    *mode = MODE_IN_ROW;
                    *reprocess = 1;
                    return 1;
                }
                if (strcmp(t->name, "select") == 0) {
                    template_mode_replace(template_mode_stack, template_mode_top, MODE_IN_SELECT);
                    *mode = MODE_IN_SELECT;
                    *reprocess = 1;
                    return 1;
                }
            }

            template_mode_replace(template_mode_stack, template_mode_top, MODE_IN_BODY);
            *mode = MODE_IN_BODY;
            *reprocess = 1;
            return 1;

        default:
            return 0;
    }
}

static node *ensure_html(node *doc, node_stack *st, node **html_out) {
    if (*html_out) return *html_out;
    *html_out = node_create(NODE_ELEMENT, "html", NULL);
    if (!*html_out) return NULL;
    node_append_child(doc, *html_out);
    stack_push(st, *html_out);
    return *html_out;
}

static node *ensure_body(node *doc, node_stack *st, node **html_out, node **body_out) {
    node *html = ensure_html(doc, st, html_out);
    if (!html) return NULL;
    if (*body_out) {
        if (!stack_contains(st, *body_out)) {
            stack_push(st, *body_out);
        }
        return *body_out;
    }
    *body_out = node_create(NODE_ELEMENT, "body", NULL);
    if (!*body_out) return NULL;
    node_append_child(html, *body_out);
    stack_push(st, *body_out);
    return *body_out;
}

static void close_head(node_stack *st, node **head_out, insertion_mode *mode) {
    if (*head_out) {
        stack_pop_until(st, "head");
        *head_out = NULL;
    }
    *mode = MODE_IN_BODY;
}

/* ============================================================================
 * Foreign Content processing (WHATWG §13.2.6.7)
 * Returns 1 if the token was consumed in foreign mode.
 * Returns 0 if normal HTML mode should handle the token.
 * Sets *reprocess_flag = 1 if the token should be reprocessed after breakout.
 * ============================================================================ */
static int process_in_foreign_content(
    token_type type, const char *name, const char *data,
    int self_closing, const token_attr *attrs, size_t attr_count,
    node_stack *st, formatting_list *fl, node *doc,
    insertion_mode *mode, int *reprocess_flag,
    node *fragment_context)
{
    (void)fl; (void)mode; (void)fragment_context;

    /* Get adjusted current node */
    node *acn = stack_top(st);
    if (!acn) return 0;
    if (acn->ns == NS_HTML) return 0;

    /* MathML text integration point: start tag that is not mglyph/malignmark
       → process in HTML mode */
    if (acn->ns == NS_MATHML && is_mathml_text_integration_point(acn->name)) {
        if (type == TOKEN_START_TAG && name &&
            strcmp(name, "mglyph") != 0 && strcmp(name, "malignmark") != 0 &&
            strcmp(name, "svg") != 0 && strcmp(name, "math") != 0) {
            return 0; /* let HTML mode handle */
        }
        if (type == TOKEN_CHARACTER) {
            return 0; /* let HTML mode handle */
        }
    }

    /* HTML integration point: start tag or character → HTML mode */
    if (is_html_integration_point(acn->name, acn->ns, acn->attrs, acn->attr_count)) {
        if (type == TOKEN_START_TAG || type == TOKEN_CHARACTER) {
            return 0; /* let HTML mode handle */
        }
    }

    switch (type) {
        case TOKEN_CHARACTER: {
            /* Insert text node into current node */
            if (data && data[0] != '\0') {
                node *text = node_create(NODE_TEXT, NULL, data);
                node_append_child(current_node(st, doc), text);
            }
            return 1;
        }

        case TOKEN_COMMENT: {
            node *comment = node_create(NODE_COMMENT, NULL, data ? data : "");
            node_append_child(current_node(st, doc), comment);
            return 1;
        }

        case TOKEN_START_TAG: {
            /* Breakout: HTML tag inside foreign content */
            if (is_foreign_breakout_tag(name) ||
                (name && strcmp(name, "font") == 0 && font_has_breakout_attr(attrs, attr_count))) {
                /* Pop from stack until we reach an HTML element or an
                   integration point */
                while (st->size > 0) {
                    node *top = stack_top(st);
                    if (!top) break;
                    if (top->ns == NS_HTML) break;
                    if (is_mathml_text_integration_point(top->name) && top->ns == NS_MATHML) break;
                    if (is_html_integration_point(top->name, top->ns, top->attrs, top->attr_count)) break;
                    stack_pop(st);
                }
                *reprocess_flag = 1;
                return 1;
            }

            /* Stay in foreign content: create element in current namespace */
            node_namespace target_ns = acn->ns;
            const char *adjusted_name = name;
            if (target_ns == NS_SVG) {
                adjusted_name = svg_adjust_element_name(name);
            }

            node *n = node_create_ns(NODE_ELEMENT, adjusted_name, NULL, target_ns);
            if (!n) return 1;

            /* Adjust attribute names */
            if (attr_count > 0 && attrs) {
                n->attrs = (node_attr *)malloc(attr_count * sizeof(node_attr));
                if (n->attrs) {
                    n->attr_count = attr_count;
                    for (size_t i = 0; i < attr_count; ++i) {
                        const char *aname = attrs[i].name;
                        if (target_ns == NS_SVG && aname) {
                            aname = svg_adjust_attr_name(aname);
                        } else if (target_ns == NS_MATHML && aname) {
                            aname = mathml_adjust_attr_name(aname);
                        }
                        n->attrs[i].name  = aname ? strdup(aname) : NULL;
                        n->attrs[i].value = attrs[i].value ? strdup(attrs[i].value) : NULL;
                    }
                }
            }

            node_append_child(current_node(st, doc), n);
            if (!self_closing) {
                stack_push(st, n);
            }
            return 1;
        }

        case TOKEN_END_TAG: {
            if (!name) return 1;
            /* Walk stack from top downward looking for matching element */
            for (size_t i = st->size; i > 0; --i) {
                node *entry = st->items[i - 1];
                if (!entry || !entry->name) continue;

                /* Case-insensitive match for SVG adjusted names */
                const char *entry_name = entry->name;
                int match = 0;
                if (entry->ns == NS_SVG) {
                    /* Compare lowercased versions */
                    size_t nlen = strlen(name);
                    size_t elen = strlen(entry_name);
                    if (nlen == elen) {
                        match = 1;
                        for (size_t j = 0; j < nlen; ++j) {
                            char a = name[j];
                            char b = entry_name[j];
                            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
                            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
                            if (a != b) { match = 0; break; }
                        }
                    }
                } else {
                    match = (strcmp(entry_name, name) == 0);
                }

                if (match) {
                    /* Pop up to and including this element */
                    while (st->size >= i) {
                        stack_pop(st);
                    }
                    return 1;
                }

                /* If we hit an HTML element, hand off to normal processing */
                if (entry->ns == NS_HTML) {
                    return 0;
                }
            }
            return 1;
        }

        default:
            return 0;
    }
}

node *build_tree_from_tokens(const token *tokens, size_t count) {
    node *doc = node_create(NODE_DOCUMENT, NULL, NULL);
    node_stack st;
    insertion_mode mode = MODE_INITIAL;
    doc_mode dmode = DOC_NO_QUIRKS;
    node *html = NULL;
    node *head = NULL;
    node *body = NULL;
    formatting_list fmt = {0};
    insertion_mode template_mode_stack[64];
    int template_mode_top = 0;
    size_t i;

    if (!doc) return NULL;
    stack_init(&st);

    for (i = 0; i < count; ++i) {
        const token *t = &tokens[i];
        node *parent;
        node *n = NULL;
        int reprocess = 1;

        while (reprocess) {
            reprocess = 0;
            parent = current_node(&st, doc);

            /* Foreign content check */
            {
                node *acn = (st.size > 0) ? stack_top(&st) : NULL;
                if (acn && acn->ns != NS_HTML) {
                    int fc_reprocess = 0;
                    if (process_in_foreign_content(t->type, t->name, t->data,
                            t->self_closing, t->attrs, t->attr_count,
                            &st, &fmt, doc, &mode, &fc_reprocess, NULL)) {
                        if (fc_reprocess) { reprocess = 1; }
                        continue;
                    }
                }
            }

            if (mode == MODE_IN_TEMPLATE) {
                if (handle_in_template_mode(t, doc, &st, &fmt, &mode,
                                            template_mode_stack, &template_mode_top,
                                            &reprocess)) {
                    if (reprocess) continue;
                    break;
                }
            }

            switch (t->type) {
                case TOKEN_DOCTYPE:
                    n = node_create(NODE_DOCTYPE, t->name ? t->name : "", NULL);
                    node_append_child(doc, n);
                    dmode = determine_doc_mode(t);
                    if (mode == MODE_INITIAL) mode = MODE_BEFORE_HTML;
                    break;
                case TOKEN_START_TAG:
                    if (mode == MODE_INITIAL) {
                        dmode = DOC_QUIRKS;
                        mode = MODE_BEFORE_HTML;
                    }
                    if (mode == MODE_BEFORE_HTML) {
                        if (t->name && strcmp(t->name, "html") == 0) {
                            html = ensure_html(doc, &st, &html);
                            mode = MODE_IN_HEAD;
                            break;
                        }
                        html = ensure_html(doc, &st, &html);
                        if (t->name && strcmp(t->name, "head") == 0) {
                            head = node_create(NODE_ELEMENT, "head", NULL);
                            attach_attrs(head, t->attrs, t->attr_count);
                            node_append_child(html, head);
                            stack_push(&st, head);
                            mode = MODE_IN_HEAD;
                            break;
                        }
                        body = ensure_body(doc, &st, &html, &body);
                        mode = MODE_IN_BODY;
                        reprocess = 1;
                        break;
                    }
                    if (mode == MODE_IN_HEAD) {
                        if (t->name && strcmp(t->name, "head") == 0) {
                            if (!head) {
                                head = node_create(NODE_ELEMENT, "head", NULL);
                                attach_attrs(head, t->attrs, t->attr_count);
                                node_append_child(ensure_html(doc, &st, &html), head);
                                stack_push(&st, head);
                            }
                            break;
                        }
                        if (t->name && strcmp(t->name, "body") == 0) {
                            close_head(&st, &head, &mode);
                            body = ensure_body(doc, &st, &html, &body);
                            break;
                        }
                        if (t->name && strcmp(t->name, "template") == 0) {
                            parent = current_node(&st, doc);
                            node *tmpl = create_template_element(t->attrs, t->attr_count);
                            if (!tmpl) break;
                            node_append_child(parent, tmpl);
                            open_template_element(&st, &fmt, &mode, template_mode_stack, &template_mode_top,
                                                  tmpl, t->self_closing);
                            break;
                        }
                        if (!is_head_element(t->name)) {
                            close_head(&st, &head, &mode);
                            reprocess = 1;
                            break;
                        }
                    }
                    if (is_table_mode(mode)) {
                        node *cur = current_node(&st, doc);
                        if (cur && cur->name && !is_table_element(cur->name)) {
                            handle_in_body_start(t->name, t->self_closing, doc, &st, &html, &body, &fmt, &mode,
                                                 template_mode_stack, &template_mode_top, dmode,
                                                 t->attrs, t->attr_count);
                            break;
                        }
                    }
                    if (mode == MODE_IN_BODY) {
                        handle_in_body_start(t->name, t->self_closing, doc, &st, &html, &body, &fmt, &mode,
                                             template_mode_stack, &template_mode_top, dmode,
                                             t->attrs, t->attr_count);
                        break;
                    }
                    if (mode == MODE_IN_TABLE) {
                        if (t->name && strcmp(t->name, "caption") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "caption", NULL);
                            attach_attrs(n, t->attrs, t->attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            formatting_push_marker(&fmt);
                            mode = MODE_IN_CAPTION;
                            break;
                        }
                        if (t->name && strcmp(t->name, "colgroup") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "colgroup", NULL);
                            attach_attrs(n, t->attrs, t->attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            break;
                        }
                        if (t->name && strcmp(t->name, "col") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "col", NULL);
                            attach_attrs(n, t->attrs, t->attr_count);
                            node_append_child(parent, n);
                            break;
                        }
                        if (t->name && strcmp(t->name, "select") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "select", NULL);
                            attach_attrs(n, t->attrs, t->attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            mode = MODE_IN_SELECT_IN_TABLE;
                            break;
                        }
                        if (t->name && is_table_section_element(t->name)) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, t->name, NULL);
                            attach_attrs(n, t->attrs, t->attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            mode = MODE_IN_TABLE_BODY;
                            break;
                        }
                        if (t->name && strcmp(t->name, "tr") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "tr", NULL);
                            attach_attrs(n, t->attrs, t->attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            mode = MODE_IN_ROW;
                            break;
                        }
                        if (t->name && is_cell_element(t->name)) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, t->name, NULL);
                            attach_attrs(n, t->attrs, t->attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            formatting_push_marker(&fmt);
                            mode = MODE_IN_CELL;
                            break;
                        }
                        if (!is_table_element(t->name)) {
                            if (t->name && strcmp(t->name, "template") == 0) {
                                node *table = NULL;
                                node *fp = foster_parent(&st, doc, &table);
                                node *tmpl = create_template_element(t->attrs, t->attr_count);
                                if (!tmpl) break;
                                if (table && fp == table->parent) {
                                    node_insert_before(fp, tmpl, table);
                                } else {
                                    node_append_child(fp, tmpl);
                                }
                                open_template_element(&st, &fmt, &mode, template_mode_stack, &template_mode_top,
                                                      tmpl, t->self_closing);
                                break;
                            }
                            fmt_tag ft = fmt_tag_from_name(t->name);
                            node *table = NULL;
                            node *fp = foster_parent(&st, doc, &table);
                            if (ft != FMT_NONE) {
                                reconstruct_active_formatting(&st, &fmt, fp);
                            }
                            n = node_create(NODE_ELEMENT, t->name ? t->name : "", NULL);
                            attach_attrs(n, t->attrs, t->attr_count);
                            if (table && fp == table->parent) {
                                node_insert_before(fp, n, table);
                            } else {
                                node_append_child(fp, n);
                            }
                            if (!t->self_closing && !is_void_element(t->name)) {
                                stack_push(&st, n);
                                if (ft != FMT_NONE) formatting_push(&fmt, ft, n);
                            }
                            break;
                        }
                    } else if (mode == MODE_IN_HEAD) {
                        parent = current_node(&st, doc);
                        n = node_create(NODE_ELEMENT, t->name ? t->name : "", NULL);
                        attach_attrs(n, t->attrs, t->attr_count);
                        node_append_child(parent, n);
                        if (!t->self_closing && !is_void_element(t->name)) {
                            stack_push(&st, n);
                        }
                    } else if (mode == MODE_IN_TABLE_BODY) {
                        if (t->name && is_table_section_element(t->name)) {
                            if (stack_has_open_table_section(&st)) {
                                stack_pop_until(&st, "thead");
                                stack_pop_until(&st, "tbody");
                                stack_pop_until(&st, "tfoot");
                            }
                            mode = MODE_IN_TABLE;
                            reprocess = 1;
                            break;
                        }
                        if (t->name && strcmp(t->name, "tr") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "tr", NULL);
                            attach_attrs(n, t->attrs, t->attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            mode = MODE_IN_ROW;
                            break;
                        }
                        if (t->name && is_cell_element(t->name)) {
                            parent = current_node(&st, doc);
                            node *tr = node_create(NODE_ELEMENT, "tr", NULL);
                            node_append_child(parent, tr);
                            stack_push(&st, tr);
                            node *cell = node_create(NODE_ELEMENT, t->name, NULL);
                            attach_attrs(cell, t->attrs, t->attr_count);
                            node_append_child(tr, cell);
                            stack_push(&st, cell);
                            formatting_push_marker(&fmt);
                            mode = MODE_IN_CELL;
                            break;
                        }
                        if (!is_table_element(t->name)) {
                            if (t->name && strcmp(t->name, "template") == 0) {
                                node *table = NULL;
                                node *fp = foster_parent(&st, doc, &table);
                                node *tmpl = create_template_element(t->attrs, t->attr_count);
                                if (!tmpl) break;
                                if (table && fp == table->parent) {
                                    node_insert_before(fp, tmpl, table);
                                } else {
                                    node_append_child(fp, tmpl);
                                }
                                open_template_element(&st, &fmt, &mode, template_mode_stack, &template_mode_top,
                                                      tmpl, t->self_closing);
                                break;
                            }
                            fmt_tag ft = fmt_tag_from_name(t->name);
                            node *table = NULL;
                            node *fp = foster_parent(&st, doc, &table);
                            if (ft != FMT_NONE) {
                                reconstruct_active_formatting(&st, &fmt, fp);
                            }
                            n = node_create(NODE_ELEMENT, t->name ? t->name : "", NULL);
                            attach_attrs(n, t->attrs, t->attr_count);
                            if (table && fp == table->parent) {
                                node_insert_before(fp, n, table);
                            } else {
                                node_append_child(fp, n);
                            }
                            if (!t->self_closing && !is_void_element(t->name)) {
                                stack_push(&st, n);
                                if (ft != FMT_NONE) formatting_push(&fmt, ft, n);
                            }
                            break;
                        }
                    } else if (mode == MODE_IN_ROW) {
                        if (t->name && is_cell_element(t->name)) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, t->name, NULL);
                            attach_attrs(n, t->attrs, t->attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            formatting_push_marker(&fmt);
                            mode = MODE_IN_CELL;
                            break;
                        }
                        if (t->name && is_table_section_element(t->name)) {
                            if (stack_has_open_named(&st, "tr")) {
                                stack_pop_until(&st, "tr");
                            }
                            mode = MODE_IN_TABLE_BODY;
                            reprocess = 1;
                            break;
                        }
                        if (!is_table_element(t->name)) {
                            if (t->name && strcmp(t->name, "template") == 0) {
                                node *table = NULL;
                                node *fp = foster_parent(&st, doc, &table);
                                node *tmpl = create_template_element(t->attrs, t->attr_count);
                                if (!tmpl) break;
                                if (table && fp == table->parent) {
                                    node_insert_before(fp, tmpl, table);
                                } else {
                                    node_append_child(fp, tmpl);
                                }
                                open_template_element(&st, &fmt, &mode, template_mode_stack, &template_mode_top,
                                                      tmpl, t->self_closing);
                                break;
                            }
                            fmt_tag ft = fmt_tag_from_name(t->name);
                            node *table = NULL;
                            node *fp = foster_parent(&st, doc, &table);
                            if (ft != FMT_NONE) {
                                reconstruct_active_formatting(&st, &fmt, fp);
                            }
                            n = node_create(NODE_ELEMENT, t->name ? t->name : "", NULL);
                            attach_attrs(n, t->attrs, t->attr_count);
                            if (table && fp == table->parent) {
                                node_insert_before(fp, n, table);
                            } else {
                                node_append_child(fp, n);
                            }
                            if (!t->self_closing && !is_void_element(t->name)) {
                                stack_push(&st, n);
                                if (ft != FMT_NONE) formatting_push(&fmt, ft, n);
                            }
                            break;
                        }
                    } else if (mode == MODE_IN_CELL) {
                        if (t->name && strcmp(t->name, "select") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "select", NULL);
                            attach_attrs(n, t->attrs, t->attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            mode = MODE_IN_SELECT_IN_TABLE;
                            break;
                        }
                        if (t->name && is_cell_element(t->name)) {
                            close_cell(&st, &fmt);
                            mode = MODE_IN_ROW;
                            reprocess = 1;
                            break;
                        }
                        if (t->name && (strcmp(t->name, "tr") == 0 || is_table_section_element(t->name))) {
                            close_cell(&st, &fmt);
                            mode = MODE_IN_TABLE_BODY;
                            reprocess = 1;
                            break;
                        }
                        handle_in_body_start(t->name, t->self_closing, doc, &st, &html, &body, &fmt, &mode,
                                             template_mode_stack, &template_mode_top, dmode,
                                             t->attrs, t->attr_count);
                    } else if (mode == MODE_IN_CAPTION) {
                        if (t->name && (strcmp(t->name, "table") == 0 || strcmp(t->name, "tr") == 0 || is_table_section_element(t->name))) {
                            stack_pop_until(&st, "caption");
                            mode = MODE_IN_TABLE;
                            reprocess = 1;
                            break;
                        }
                        if (t->name && strcmp(t->name, "template") == 0) {
                            parent = current_node(&st, doc);
                            node *tmpl = create_template_element(t->attrs, t->attr_count);
                            if (!tmpl) break;
                            node_append_child(parent, tmpl);
                            open_template_element(&st, &fmt, &mode, template_mode_stack, &template_mode_top,
                                                  tmpl, t->self_closing);
                            break;
                        }
                        parent = current_node(&st, doc);
                        n = node_create(NODE_ELEMENT, t->name ? t->name : "", NULL);
                        attach_attrs(n, t->attrs, t->attr_count);
                        node_append_child(parent, n);
                        if (!t->self_closing && !is_void_element(t->name)) {
                            stack_push(&st, n);
                        }
                        break;
                    } else if (mode == MODE_IN_SELECT || mode == MODE_IN_SELECT_IN_TABLE) {
                        if (t->name && strcmp(t->name, "select") == 0) {
                            /* ignore nested select */
                            break;
                        }
                        /* Auto-close an open <option> before a new <option> or <optgroup> */
                        if (t->name && strcmp(t->name, "option") == 0 && stack_has_open_named(&st, "option")) {
                            stack_pop_until(&st, "option");
                        }
                        /* Auto-close an open <optgroup>: <option> closes it, <optgroup> closes it */
                        if (t->name && strcmp(t->name, "optgroup") == 0 && stack_has_open_named(&st, "optgroup")) {
                            if (stack_has_open_named(&st, "option")) {
                                stack_pop_until(&st, "option");
                            }
                            stack_pop_until(&st, "optgroup");
                        }
                        if (t->name && is_select_child_element(t->name)) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, t->name, NULL);
                            attach_attrs(n, t->attrs, t->attr_count);
                            node_append_child(parent, n);
                            if (!t->self_closing && !is_void_element(t->name)) {
                                stack_push(&st, n);
                            }
                            break;
                        }
                        if (mode == MODE_IN_SELECT_IN_TABLE && t->name && is_table_element(t->name)) {
                            stack_pop_until(&st, "select");
                            mode = MODE_IN_TABLE;
                            reprocess = 1;
                            break;
                        }
                        parent = current_node(&st, doc);
                        n = node_create(NODE_ELEMENT, t->name ? t->name : "", NULL);
                        attach_attrs(n, t->attrs, t->attr_count);
                        node_append_child(parent, n);
                        if (!t->self_closing && !is_void_element(t->name)) {
                            stack_push(&st, n);
                        }
                    }
                    break;
                case TOKEN_END_TAG:
                    if (t->name && strcmp(t->name, "template") == 0 && stack_has_open_named(&st, "template")) {
                        close_template_element(&st, &fmt, &mode, template_mode_stack, &template_mode_top);
                        break;
                    }
                    if (t->name && strcmp(t->name, "head") == 0 && mode == MODE_IN_HEAD) {
                        close_head(&st, &head, &mode);
                        break;
                    }
                    if (t->name && strcmp(t->name, "body") == 0 && mode == MODE_IN_BODY) {
                        generate_implied_end_tags(&st);
                        stack_pop_until(&st, "body");
                        mode = MODE_AFTER_BODY;
                        break;
                    }
                    if (t->name && strcmp(t->name, "p") == 0 && mode == MODE_IN_BODY) {
                        if (!has_element_in_button_scope(&st, "p")) {
                            node *parent = current_node(&st, doc);
                            node *pn = node_create(NODE_ELEMENT, "p", NULL);
                            node_append_child(parent, pn);
                            break;
                        }
                        generate_implied_end_tags_except(&st, "p");
                        stack_pop_until(&st, "p");
                        break;
                    }
                    if (t->name && strcmp(t->name, "li") == 0 && mode == MODE_IN_BODY) {
                        if (!has_element_in_list_item_scope(&st, "li")) break;
                        generate_implied_end_tags_except(&st, "li");
                        stack_pop_until(&st, "li");
                        break;
                    }
                    if (t->name && (strcmp(t->name, "dd") == 0 || strcmp(t->name, "dt") == 0) && mode == MODE_IN_BODY) {
                        if (!has_element_in_scope(&st, t->name)) break;
                        generate_implied_end_tags_except(&st, t->name);
                        stack_pop_until(&st, t->name);
                        break;
                    }
                    if (t->name && strcmp(t->name, "table") == 0) {
                        if (!has_element_in_table_scope(&st, "table")) {
                            break;
                        }
                        if (mode == MODE_IN_CELL) {
                            formatting_clear_to_marker(&fmt);
                        }
                        stack_pop_until(&st, "table");
                        mode = MODE_IN_BODY;
                        break;
                    }
                    if (t->name && strcmp(t->name, "tr") == 0 && mode == MODE_IN_ROW && has_element_in_table_scope(&st, "tr")) {
                        stack_pop_until(&st, "tr");
                        mode = stack_has_open_table_section(&st) ? MODE_IN_TABLE_BODY : MODE_IN_TABLE;
                        break;
                    }
                    if (t->name && is_cell_element(t->name) && mode == MODE_IN_CELL && has_element_in_table_scope(&st, t->name)) {
                        stack_pop_until(&st, t->name);
                        formatting_clear_to_marker(&fmt);
                        mode = MODE_IN_ROW;
                        break;
                    }
                    if (t->name && is_table_section_element(t->name) && mode == MODE_IN_CELL && has_element_in_table_scope(&st, t->name)) {
                        close_cell(&st, &fmt);
                        stack_pop_until(&st, t->name);
                        mode = MODE_IN_TABLE;
                        break;
                    }
                    if (t->name && is_table_section_element(t->name) && (mode == MODE_IN_TABLE || mode == MODE_IN_TABLE_BODY) &&
                        has_element_in_table_scope(&st, t->name)) {
                        stack_pop_until(&st, t->name);
                        mode = MODE_IN_TABLE;
                        break;
                    }
                    if (t->name && strcmp(t->name, "caption") == 0 && mode == MODE_IN_CAPTION && has_element_in_table_scope(&st, "caption")) {
                        stack_pop_until(&st, "caption");
                        formatting_clear_to_marker(&fmt);
                        mode = MODE_IN_TABLE;
                        break;
                    }
                    if (t->name && strcmp(t->name, "select") == 0 && (mode == MODE_IN_SELECT || mode == MODE_IN_SELECT_IN_TABLE) &&
                        stack_has_open_named(&st, "select")) {
                        stack_pop_until(&st, "select");
                        if (mode == MODE_IN_SELECT_IN_TABLE) {
                            mode = (stack_has_open_named(&st, "td") || stack_has_open_named(&st, "th")) ? MODE_IN_CELL : MODE_IN_TABLE;
                        } else {
                            mode = MODE_IN_BODY;
                        }
                        break;
                    }
                    if (t->name && (strcmp(t->name, "applet") == 0 || strcmp(t->name, "marquee") == 0 || strcmp(t->name, "object") == 0)) {
                        if (!has_element_in_scope(&st, t->name)) break;
                        generate_implied_end_tags(&st);
                        stack_pop_until(&st, t->name);
                        formatting_clear_to_marker(&fmt);
                        break;
                    }
                    if (t->name && strcmp(t->name, "html") == 0) {
                        stack_pop_until(&st, "html");
                        if (mode == MODE_AFTER_BODY) {
                            mode = MODE_AFTER_AFTER_BODY;
                        }
                        break;
                    }
                    if (mode == MODE_IN_BODY ||
                        mode == MODE_IN_CELL ||
                        mode == MODE_IN_TABLE ||
                        mode == MODE_IN_TABLE_BODY ||
                        mode == MODE_IN_ROW ||
                        mode == MODE_IN_CAPTION) {
                        if (adoption_agency(&st, &fmt, doc, t->name)) {
                            break;
                        }
                    }
                    if (t->name && !has_element_in_scope(&st, t->name)) {
                        break;
                    }
                    stack_pop_until(&st, t->name);
                    break;
                case TOKEN_COMMENT:
                    parent = current_node(&st, doc);
                    n = node_create(NODE_COMMENT, NULL, t->data ? t->data : "");
                    node_append_child(parent, n);
                    break;
                case TOKEN_CHARACTER:
                    if (t->data && t->data[0] != '\0') {
                        if (is_all_whitespace(t->data)) {
                            if (mode == MODE_IN_BODY) {
                                if (!in_template_context(&st)) {
                                    ensure_body(doc, &st, &html, &body);
                                }
                                parent = current_node(&st, doc);
                                if (parent) {
                                    reconstruct_active_formatting(&st, &fmt, parent);
                                }
                            }
                            break;
                        }
                        if (mode == MODE_AFTER_BODY || mode == MODE_AFTER_AFTER_BODY) {
                            mode = MODE_IN_BODY;
                        }
                        if (mode == MODE_IN_HEAD) {
                            if (!head) {
                                head = node_create(NODE_ELEMENT, "head", NULL);
                                node_append_child(ensure_html(doc, &st, &html), head);
                                stack_push(&st, head);
                            }
                            parent = current_node(&st, doc);
                            n = node_create(NODE_TEXT, NULL, t->data);
                            node_append_child(parent, n);
                            break;
                        }
                        if (is_table_mode(mode)) {
                            node *cur = current_node(&st, doc);
                            if (mode == MODE_IN_CELL || (cur && cur->name && !is_table_element(cur->name))) {
                                parent = cur;
                                n = node_create(NODE_TEXT, NULL, t->data);
                                node_append_child(parent, n);
                                break;
                            }
                            n = node_create(NODE_TEXT, NULL, t->data);
                            foster_insert(&st, doc, n);
                            break;
                        }
                        if (mode == MODE_INITIAL) {
                            dmode = DOC_QUIRKS;
                            mode = MODE_BEFORE_HTML;
                        }
                        if (mode == MODE_INITIAL || mode == MODE_BEFORE_HTML) {
                            ensure_body(doc, &st, &html, &body);
                            mode = MODE_IN_BODY;
                        }
                        if (mode == MODE_IN_BODY) {
                            parent = current_node(&st, doc);
                            reconstruct_active_formatting(&st, &fmt, parent);
                        }
                        parent = current_node(&st, doc);
                        n = node_create(NODE_TEXT, NULL, t->data);
                        node_append_child(parent, n);
                    }
                    break;
                case TOKEN_EOF:
                default:
                    break;
            }
        }
    }

    return doc;
}

node *build_tree_from_input(const char *input) {
    tokenizer tz;
    token t;
    node *doc = node_create(NODE_DOCUMENT, NULL, NULL);
    node_stack st;
    insertion_mode mode = MODE_INITIAL;
    doc_mode dmode = DOC_NO_QUIRKS;
    node *html = NULL;
    node *head = NULL;
    node *body = NULL;
    formatting_list fmt = {0};
    insertion_mode template_mode_stack[64];
    int template_mode_top = 0;

    if (!doc) return NULL;
    stack_init(&st);
    tokenizer_init(&tz, input);

    while (1) {
        token_init(&t);
        /* Set CDATA flag based on whether current node is in foreign content */
        {
            node *top = stack_top(&st);
            tz.allow_cdata = (top && top->ns != NS_HTML) ? 1 : 0;
        }
        tokenizer_next(&tz, &t);

        node *parent;
        node *n = NULL;
        int reprocess = 1;

        while (reprocess) {
            reprocess = 0;
            parent = current_node(&st, doc);

            /* Foreign content check */
            {
                node *acn = (st.size > 0) ? stack_top(&st) : NULL;
                if (acn && acn->ns != NS_HTML) {
                    int fc_reprocess = 0;
                    if (process_in_foreign_content(t.type, t.name, t.data,
                            t.self_closing, t.attrs, t.attr_count,
                            &st, &fmt, doc, &mode, &fc_reprocess, NULL)) {
                        /* Reset tokenizer if SVG <title> was handled in foreign
                           content — the tokenizer already switched to RCDATA
                           but SVG title is an HTML integration point, not RCDATA. */
                        if (t.type == TOKEN_START_TAG && t.name &&
                            acn->ns == NS_SVG && strcmp(t.name, "title") == 0 &&
                            !fc_reprocess) {
                            tz.state = TOKENIZE_DATA;
                            tz.raw_tag[0] = '\0';
                        }
                        if (fc_reprocess) { reprocess = 1; }
                        continue;
                    }
                }
            }

            if (mode == MODE_IN_TEMPLATE) {
                if (handle_in_template_mode(&t, doc, &st, &fmt, &mode,
                                            template_mode_stack, &template_mode_top,
                                            &reprocess)) {
                    if (reprocess) continue;
                    break;
                }
            }

            switch (t.type) {
                case TOKEN_DOCTYPE:
                    n = node_create(NODE_DOCTYPE, t.name ? t.name : "", NULL);
                    node_append_child(doc, n);
                    dmode = determine_doc_mode(&t);
                    if (mode == MODE_INITIAL) mode = MODE_BEFORE_HTML;
                    break;
                case TOKEN_START_TAG:
                    if (mode == MODE_INITIAL) {
                        dmode = DOC_QUIRKS;
                        mode = MODE_BEFORE_HTML;
                    }
                    if (mode == MODE_BEFORE_HTML) {
                        if (t.name && strcmp(t.name, "html") == 0) {
                            html = ensure_html(doc, &st, &html);
                            mode = MODE_IN_HEAD;
                            break;
                        }
                        html = ensure_html(doc, &st, &html);
                        if (t.name && strcmp(t.name, "head") == 0) {
                            head = node_create(NODE_ELEMENT, "head", NULL);
                            attach_attrs(head, t.attrs, t.attr_count);
                            node_append_child(html, head);
                            stack_push(&st, head);
                            mode = MODE_IN_HEAD;
                            break;
                        }
                        body = ensure_body(doc, &st, &html, &body);
                        mode = MODE_IN_BODY;
                        reprocess = 1;
                        break;
                    }
                    if (mode == MODE_IN_HEAD) {
                        if (t.name && strcmp(t.name, "head") == 0) {
                            if (!head) {
                                head = node_create(NODE_ELEMENT, "head", NULL);
                                attach_attrs(head, t.attrs, t.attr_count);
                                node_append_child(ensure_html(doc, &st, &html), head);
                                stack_push(&st, head);
                            }
                            break;
                        }
                        if (t.name && strcmp(t.name, "body") == 0) {
                            close_head(&st, &head, &mode);
                            body = ensure_body(doc, &st, &html, &body);
                            break;
                        }
                        if (t.name && strcmp(t.name, "template") == 0) {
                            parent = current_node(&st, doc);
                            node *tmpl = create_template_element(t.attrs, t.attr_count);
                            if (!tmpl) break;
                            node_append_child(parent, tmpl);
                            open_template_element(&st, &fmt, &mode, template_mode_stack, &template_mode_top,
                                                  tmpl, t.self_closing);
                            break;
                        }
                        if (!is_head_element(t.name)) {
                            close_head(&st, &head, &mode);
                            reprocess = 1;
                            break;
                        }
                    }
                    if (is_table_mode(mode)) {
                        node *cur = current_node(&st, doc);
                        if (cur && cur->name && !is_table_element(cur->name)) {
                            handle_in_body_start(t.name, t.self_closing, doc, &st, &html, &body, &fmt, &mode,
                                                 template_mode_stack, &template_mode_top, dmode,
                                                 t.attrs, t.attr_count);
                            break;
                        }
                    }
                    if (mode == MODE_IN_BODY) {
                        handle_in_body_start(t.name, t.self_closing, doc, &st, &html, &body, &fmt, &mode,
                                             template_mode_stack, &template_mode_top, dmode,
                                             t.attrs, t.attr_count);
                        break;
                    }
                    if (mode == MODE_IN_TABLE) {
                        if (t.name && strcmp(t.name, "caption") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "caption", NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            formatting_push_marker(&fmt);
                            mode = MODE_IN_CAPTION;
                            break;
                        }
                        if (t.name && strcmp(t.name, "colgroup") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "colgroup", NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            break;
                        }
                        if (t.name && strcmp(t.name, "col") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "col", NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            node_append_child(parent, n);
                            break;
                        }
                        if (t.name && strcmp(t.name, "select") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "select", NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            mode = MODE_IN_SELECT_IN_TABLE;
                            break;
                        }
                        if (t.name && is_table_section_element(t.name)) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, t.name, NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            mode = MODE_IN_TABLE_BODY;
                            break;
                        }
                        if (t.name && strcmp(t.name, "tr") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "tr", NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            mode = MODE_IN_ROW;
                            break;
                        }
                        if (t.name && is_cell_element(t.name)) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, t.name, NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            formatting_push_marker(&fmt);
                            mode = MODE_IN_CELL;
                            break;
                        }
                        if (!is_table_element(t.name)) {
                            if (t.name && strcmp(t.name, "template") == 0) {
                                node *table = NULL;
                                node *fp = foster_parent(&st, doc, &table);
                                node *tmpl = create_template_element(t.attrs, t.attr_count);
                                if (!tmpl) break;
                                if (table && fp == table->parent) {
                                    node_insert_before(fp, tmpl, table);
                                } else {
                                    node_append_child(fp, tmpl);
                                }
                                open_template_element(&st, &fmt, &mode, template_mode_stack, &template_mode_top,
                                                      tmpl, t.self_closing);
                                break;
                            }
                            fmt_tag ft = fmt_tag_from_name(t.name);
                            node *table = NULL;
                            node *fp = foster_parent(&st, doc, &table);
                            if (ft != FMT_NONE) {
                                reconstruct_active_formatting(&st, &fmt, fp);
                            }
                            n = node_create(NODE_ELEMENT, t.name ? t.name : "", NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            if (table && fp == table->parent) {
                                node_insert_before(fp, n, table);
                            } else {
                                node_append_child(fp, n);
                            }
                            if (!t.self_closing && !is_void_element(t.name)) {
                                stack_push(&st, n);
                                if (ft != FMT_NONE) formatting_push(&fmt, ft, n);
                            }
                            break;
                        }
                    } else if (mode == MODE_IN_HEAD) {
                        parent = current_node(&st, doc);
                        n = node_create(NODE_ELEMENT, t.name ? t.name : "", NULL);
                        attach_attrs(n, t.attrs, t.attr_count);
                        node_append_child(parent, n);
                        if (!t.self_closing && !is_void_element(t.name)) {
                            stack_push(&st, n);
                        }
                    } else if (mode == MODE_IN_TABLE_BODY) {
                        if (t.name && is_table_section_element(t.name)) {
                            if (stack_has_open_table_section(&st)) {
                                stack_pop_until(&st, "thead");
                                stack_pop_until(&st, "tbody");
                                stack_pop_until(&st, "tfoot");
                            }
                            mode = MODE_IN_TABLE;
                            reprocess = 1;
                            break;
                        }
                        if (t.name && strcmp(t.name, "tr") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "tr", NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            mode = MODE_IN_ROW;
                            break;
                        }
                        if (t.name && is_cell_element(t.name)) {
                            parent = current_node(&st, doc);
                            node *tr = node_create(NODE_ELEMENT, "tr", NULL);
                            node_append_child(parent, tr);
                            stack_push(&st, tr);
                            node *cell = node_create(NODE_ELEMENT, t.name, NULL);
                            attach_attrs(cell, t.attrs, t.attr_count);
                            node_append_child(tr, cell);
                            stack_push(&st, cell);
                            formatting_push_marker(&fmt);
                            mode = MODE_IN_CELL;
                            break;
                        }
                        if (!is_table_element(t.name)) {
                            if (t.name && strcmp(t.name, "template") == 0) {
                                node *table = NULL;
                                node *fp = foster_parent(&st, doc, &table);
                                node *tmpl = create_template_element(t.attrs, t.attr_count);
                                if (!tmpl) break;
                                if (table && fp == table->parent) {
                                    node_insert_before(fp, tmpl, table);
                                } else {
                                    node_append_child(fp, tmpl);
                                }
                                open_template_element(&st, &fmt, &mode, template_mode_stack, &template_mode_top,
                                                      tmpl, t.self_closing);
                                break;
                            }
                            fmt_tag ft = fmt_tag_from_name(t.name);
                            node *table = NULL;
                            node *fp = foster_parent(&st, doc, &table);
                            if (ft != FMT_NONE) {
                                reconstruct_active_formatting(&st, &fmt, fp);
                            }
                            n = node_create(NODE_ELEMENT, t.name ? t.name : "", NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            if (table && fp == table->parent) {
                                node_insert_before(fp, n, table);
                            } else {
                                node_append_child(fp, n);
                            }
                            if (!t.self_closing && !is_void_element(t.name)) {
                                stack_push(&st, n);
                                if (ft != FMT_NONE) formatting_push(&fmt, ft, n);
                            }
                            break;
                        }
                    } else if (mode == MODE_IN_ROW) {
                        if (t.name && is_cell_element(t.name)) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, t.name, NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            formatting_push_marker(&fmt);
                            mode = MODE_IN_CELL;
                            break;
                        }
                        if (t.name && is_table_section_element(t.name)) {
                            if (stack_has_open_named(&st, "tr")) {
                                stack_pop_until(&st, "tr");
                            }
                            mode = MODE_IN_TABLE_BODY;
                            reprocess = 1;
                            break;
                        }
                        if (!is_table_element(t.name)) {
                            if (t.name && strcmp(t.name, "template") == 0) {
                                node *table = NULL;
                                node *fp = foster_parent(&st, doc, &table);
                                node *tmpl = create_template_element(t.attrs, t.attr_count);
                                if (!tmpl) break;
                                if (table && fp == table->parent) {
                                    node_insert_before(fp, tmpl, table);
                                } else {
                                    node_append_child(fp, tmpl);
                                }
                                open_template_element(&st, &fmt, &mode, template_mode_stack, &template_mode_top,
                                                      tmpl, t.self_closing);
                                break;
                            }
                            fmt_tag ft = fmt_tag_from_name(t.name);
                            node *table = NULL;
                            node *fp = foster_parent(&st, doc, &table);
                            if (ft != FMT_NONE) {
                                reconstruct_active_formatting(&st, &fmt, fp);
                            }
                            n = node_create(NODE_ELEMENT, t.name ? t.name : "", NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            if (table && fp == table->parent) {
                                node_insert_before(fp, n, table);
                            } else {
                                node_append_child(fp, n);
                            }
                            if (!t.self_closing && !is_void_element(t.name)) {
                                stack_push(&st, n);
                                if (ft != FMT_NONE) formatting_push(&fmt, ft, n);
                            }
                            break;
                        }
                    } else if (mode == MODE_IN_CELL) {
                        if (t.name && strcmp(t.name, "select") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "select", NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            mode = MODE_IN_SELECT_IN_TABLE;
                            break;
                        }
                        if (t.name && is_cell_element(t.name)) {
                            close_cell(&st, &fmt);
                            mode = MODE_IN_ROW;
                            reprocess = 1;
                            break;
                        }
                        if (t.name && (strcmp(t.name, "tr") == 0 || is_table_section_element(t.name))) {
                            close_cell(&st, &fmt);
                            mode = MODE_IN_TABLE_BODY;
                            reprocess = 1;
                            break;
                        }
                        handle_in_body_start(t.name, t.self_closing, doc, &st, &html, &body, &fmt, &mode,
                                             template_mode_stack, &template_mode_top, dmode,
                                             t.attrs, t.attr_count);
                    } else if (mode == MODE_IN_CAPTION) {
                        if (t.name && (strcmp(t.name, "table") == 0 || strcmp(t.name, "tr") == 0 || is_table_section_element(t.name))) {
                            stack_pop_until(&st, "caption");
                            mode = MODE_IN_TABLE;
                            reprocess = 1;
                            break;
                        }
                        if (t.name && strcmp(t.name, "template") == 0) {
                            parent = current_node(&st, doc);
                            node *tmpl = create_template_element(t.attrs, t.attr_count);
                            if (!tmpl) break;
                            node_append_child(parent, tmpl);
                            open_template_element(&st, &fmt, &mode, template_mode_stack, &template_mode_top,
                                                  tmpl, t.self_closing);
                            break;
                        }
                        parent = current_node(&st, doc);
                        n = node_create(NODE_ELEMENT, t.name ? t.name : "", NULL);
                        attach_attrs(n, t.attrs, t.attr_count);
                        node_append_child(parent, n);
                        if (!t.self_closing && !is_void_element(t.name)) {
                            stack_push(&st, n);
                        }
                        break;
                    } else if (mode == MODE_IN_SELECT || mode == MODE_IN_SELECT_IN_TABLE) {
                        if (t.name && strcmp(t.name, "select") == 0) {
                            break;
                        }
                        /* Auto-close an open <option> before a new <option> or <optgroup> */
                        if (t.name && strcmp(t.name, "option") == 0 && stack_has_open_named(&st, "option")) {
                            stack_pop_until(&st, "option");
                        }
                        /* Auto-close an open <optgroup>: <option> closes it, <optgroup> closes it */
                        if (t.name && strcmp(t.name, "optgroup") == 0 && stack_has_open_named(&st, "optgroup")) {
                            if (stack_has_open_named(&st, "option")) {
                                stack_pop_until(&st, "option");
                            }
                            stack_pop_until(&st, "optgroup");
                        }
                        if (t.name && is_select_child_element(t.name)) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, t.name, NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            node_append_child(parent, n);
                            if (!t.self_closing && !is_void_element(t.name)) {
                                stack_push(&st, n);
                            }
                            break;
                        }
                        if (mode == MODE_IN_SELECT_IN_TABLE && t.name && is_table_element(t.name)) {
                            stack_pop_until(&st, "select");
                            mode = MODE_IN_TABLE;
                            reprocess = 1;
                            break;
                        }
                        parent = current_node(&st, doc);
                        n = node_create(NODE_ELEMENT, t.name ? t.name : "", NULL);
                        attach_attrs(n, t.attrs, t.attr_count);
                        node_append_child(parent, n);
                        if (!t.self_closing && !is_void_element(t.name)) {
                            stack_push(&st, n);
                        }
                    }
                    break;
                case TOKEN_END_TAG:
                    if (t.name && strcmp(t.name, "template") == 0 && stack_has_open_named(&st, "template")) {
                        close_template_element(&st, &fmt, &mode, template_mode_stack, &template_mode_top);
                        break;
                    }
                    if (t.name && strcmp(t.name, "head") == 0 && mode == MODE_IN_HEAD) {
                        close_head(&st, &head, &mode);
                        break;
                    }
                    if (t.name && strcmp(t.name, "body") == 0 && mode == MODE_IN_BODY) {
                        generate_implied_end_tags(&st);
                        stack_pop_until(&st, "body");
                        mode = MODE_AFTER_BODY;
                        break;
                    }
                    if (t.name && strcmp(t.name, "p") == 0 && mode == MODE_IN_BODY) {
                        if (!has_element_in_button_scope(&st, "p")) {
                            node *parent = current_node(&st, doc);
                            node *pn = node_create(NODE_ELEMENT, "p", NULL);
                            node_append_child(parent, pn);
                            break;
                        }
                        generate_implied_end_tags_except(&st, "p");
                        stack_pop_until(&st, "p");
                        break;
                    }
                    if (t.name && strcmp(t.name, "li") == 0 && mode == MODE_IN_BODY) {
                        if (!has_element_in_list_item_scope(&st, "li")) break;
                        generate_implied_end_tags_except(&st, "li");
                        stack_pop_until(&st, "li");
                        break;
                    }
                    if (t.name && (strcmp(t.name, "dd") == 0 || strcmp(t.name, "dt") == 0) && mode == MODE_IN_BODY) {
                        if (!has_element_in_scope(&st, t.name)) break;
                        generate_implied_end_tags_except(&st, t.name);
                        stack_pop_until(&st, t.name);
                        break;
                    }
                    if (t.name && strcmp(t.name, "table") == 0) {
                        if (!has_element_in_table_scope(&st, "table")) {
                            break;
                        }
                        if (mode == MODE_IN_CELL) {
                            formatting_clear_to_marker(&fmt);
                        }
                        stack_pop_until(&st, "table");
                        mode = MODE_IN_BODY;
                        break;
                    }
                    if (t.name && strcmp(t.name, "tr") == 0 && mode == MODE_IN_ROW && has_element_in_table_scope(&st, "tr")) {
                        stack_pop_until(&st, "tr");
                        mode = stack_has_open_table_section(&st) ? MODE_IN_TABLE_BODY : MODE_IN_TABLE;
                        break;
                    }
                    if (t.name && is_cell_element(t.name) && mode == MODE_IN_CELL && has_element_in_table_scope(&st, t.name)) {
                        stack_pop_until(&st, t.name);
                        formatting_clear_to_marker(&fmt);
                        mode = MODE_IN_ROW;
                        break;
                    }
                    if (t.name && is_table_section_element(t.name) && mode == MODE_IN_CELL && has_element_in_table_scope(&st, t.name)) {
                        close_cell(&st, &fmt);
                        stack_pop_until(&st, t.name);
                        mode = MODE_IN_TABLE;
                        break;
                    }
                    if (t.name && is_table_section_element(t.name) && (mode == MODE_IN_TABLE || mode == MODE_IN_TABLE_BODY) &&
                        has_element_in_table_scope(&st, t.name)) {
                        stack_pop_until(&st, t.name);
                        mode = MODE_IN_TABLE;
                        break;
                    }
                    if (t.name && strcmp(t.name, "caption") == 0 && mode == MODE_IN_CAPTION && has_element_in_table_scope(&st, "caption")) {
                        stack_pop_until(&st, "caption");
                        formatting_clear_to_marker(&fmt);
                        mode = MODE_IN_TABLE;
                        break;
                    }
                    if (t.name && strcmp(t.name, "select") == 0 && (mode == MODE_IN_SELECT || mode == MODE_IN_SELECT_IN_TABLE) &&
                        stack_has_open_named(&st, "select")) {
                        stack_pop_until(&st, "select");
                        if (mode == MODE_IN_SELECT_IN_TABLE) {
                            mode = (stack_has_open_named(&st, "td") || stack_has_open_named(&st, "th")) ? MODE_IN_CELL : MODE_IN_TABLE;
                        } else {
                            mode = MODE_IN_BODY;
                        }
                        break;
                    }
                    if (t.name && (strcmp(t.name, "applet") == 0 || strcmp(t.name, "marquee") == 0 || strcmp(t.name, "object") == 0)) {
                        if (!has_element_in_scope(&st, t.name)) break;
                        generate_implied_end_tags(&st);
                        stack_pop_until(&st, t.name);
                        formatting_clear_to_marker(&fmt);
                        break;
                    }
                    if (t.name && strcmp(t.name, "html") == 0) {
                        stack_pop_until(&st, "html");
                        if (mode == MODE_AFTER_BODY) {
                            mode = MODE_AFTER_AFTER_BODY;
                        }
                        break;
                    }
                    if (mode == MODE_IN_BODY ||
                        mode == MODE_IN_CELL ||
                        mode == MODE_IN_TABLE ||
                        mode == MODE_IN_TABLE_BODY ||
                        mode == MODE_IN_ROW ||
                        mode == MODE_IN_CAPTION) {
                        if (adoption_agency(&st, &fmt, doc, t.name)) {
                            break;
                        }
                    }
                    if (t.name && !has_element_in_scope(&st, t.name)) {
                        break;
                    }
                    stack_pop_until(&st, t.name);
                    break;
                case TOKEN_COMMENT:
                    parent = current_node(&st, doc);
                    n = node_create(NODE_COMMENT, NULL, t.data ? t.data : "");
                    node_append_child(parent, n);
                    break;
                case TOKEN_CHARACTER:
                    if (t.data && t.data[0] != '\0') {
                        if (is_all_whitespace(t.data)) {
                            if (mode == MODE_IN_BODY) {
                                if (!in_template_context(&st)) {
                                    ensure_body(doc, &st, &html, &body);
                                }
                                parent = current_node(&st, doc);
                                if (parent) {
                                    reconstruct_active_formatting(&st, &fmt, parent);
                                }
                            }
                            break;
                        }
                        if (mode == MODE_AFTER_BODY || mode == MODE_AFTER_AFTER_BODY) {
                            mode = MODE_IN_BODY;
                        }
                        if (mode == MODE_IN_HEAD) {
                            if (!head) {
                                head = node_create(NODE_ELEMENT, "head", NULL);
                                node_append_child(ensure_html(doc, &st, &html), head);
                                stack_push(&st, head);
                            }
                            parent = current_node(&st, doc);
                            n = node_create(NODE_TEXT, NULL, t.data);
                            node_append_child(parent, n);
                            break;
                        }
                        if (is_table_mode(mode)) {
                            node *cur = current_node(&st, doc);
                            if (mode == MODE_IN_CELL || (cur && cur->name && !is_table_element(cur->name))) {
                                parent = cur;
                                n = node_create(NODE_TEXT, NULL, t.data);
                                node_append_child(parent, n);
                                break;
                            }
                            n = node_create(NODE_TEXT, NULL, t.data);
                            foster_insert(&st, doc, n);
                            break;
                        }
                        if (mode == MODE_INITIAL) {
                            dmode = DOC_QUIRKS;
                            mode = MODE_BEFORE_HTML;
                        }
                        if (mode == MODE_INITIAL || mode == MODE_BEFORE_HTML) {
                            ensure_body(doc, &st, &html, &body);
                            mode = MODE_IN_BODY;
                        }
                        if (mode == MODE_IN_BODY) {
                            parent = current_node(&st, doc);
                            reconstruct_active_formatting(&st, &fmt, parent);
                        }
                        parent = current_node(&st, doc);
                        n = node_create(NODE_TEXT, NULL, t.data);
                        node_append_child(parent, n);
                    }
                    break;
                case TOKEN_EOF:
                default:
                    token_free(&t);
                    return doc;
            }
        }

        token_free(&t);
    }
}

node *build_fragment_from_input(const char *input, const char *context_tag) {
    tokenizer tz;
    token t;
    node *doc = node_create(NODE_DOCUMENT, NULL, NULL);
    node_stack st;
    insertion_mode mode = MODE_IN_BODY;
    formatting_list fmt = {0};
    insertion_mode template_mode_stack[64];
    int template_mode_top = 0;
    node *context = NULL;

    if (!doc) return NULL;
    stack_init(&st);

    if (context_tag && context_tag[0]) {
        if (strcmp(context_tag, "template") == 0) {
            context = create_template_element(NULL, 0);
            if (!context) { node_free(doc); return NULL; }
            open_template_element(&st, &fmt, &mode, template_mode_stack, &template_mode_top, context, 0);
        } else {
            context = node_create(NODE_ELEMENT, context_tag, NULL);
            if (!context) { node_free(doc); return NULL; }
            stack_push(&st, context);
            mode = fragment_mode_for_context(context_tag);
        }
    }

    tokenizer_init_with_context(&tz, input, context_tag);

    while (1) {
        token_init(&t);
        /* Set CDATA flag based on whether current node is in foreign content */
        {
            node *top = stack_top(&st);
            tz.allow_cdata = (top && top->ns != NS_HTML) ? 1 : 0;
        }
        tokenizer_next(&tz, &t);

        node *parent;
        node *n = NULL;
        int reprocess = 1;

        while (reprocess) {
            reprocess = 0;
            parent = current_node(&st, doc);

            /* Foreign content check */
            {
                node *acn = (st.size > 0) ? stack_top(&st) : NULL;
                if (acn && acn->ns != NS_HTML) {
                    int fc_reprocess = 0;
                    if (process_in_foreign_content(t.type, t.name, t.data,
                            t.self_closing, t.attrs, t.attr_count,
                            &st, &fmt, doc, &mode, &fc_reprocess, context)) {
                        /* Reset tokenizer if SVG <title> was handled in foreign
                           content — tokenizer already switched to RCDATA. */
                        if (t.type == TOKEN_START_TAG && t.name &&
                            acn->ns == NS_SVG && strcmp(t.name, "title") == 0 &&
                            !fc_reprocess) {
                            tz.state = TOKENIZE_DATA;
                            tz.raw_tag[0] = '\0';
                        }
                        if (fc_reprocess) { reprocess = 1; }
                        continue;
                    }
                }
            }

            if (mode == MODE_IN_TEMPLATE) {
                if (handle_in_template_mode(&t, doc, &st, &fmt, &mode,
                                            template_mode_stack, &template_mode_top,
                                            &reprocess)) {
                    if (reprocess) continue;
                    break;
                }
            }

            switch (t.type) {
                case TOKEN_START_TAG:
                    if (mode == MODE_IN_HEAD) {
                        if (t.name && strcmp(t.name, "template") == 0) {
                            parent = current_node(&st, doc);
                            n = create_template_element(t.attrs, t.attr_count);
                            if (!n) break;
                            node_append_child(parent, n);
                            open_template_element(&st, &fmt, &mode, template_mode_stack, &template_mode_top,
                                                  n, t.self_closing);
                            break;
                        }
                        if (t.name && !is_head_element(t.name)) {
                            mode = MODE_IN_BODY;
                            reprocess = 1;
                            break;
                        }
                    }
                    if (is_table_mode(mode)) {
                        node *cur = current_node(&st, doc);
                        if (cur && cur->name && !is_table_element(cur->name)) {
                            handle_in_body_start_fragment(t.name, t.self_closing, doc, &st, &fmt, &mode,
                                                          template_mode_stack, &template_mode_top,
                                                          DOC_NO_QUIRKS, t.attrs, t.attr_count);
                            break;
                        }
                    }
                    if (mode == MODE_IN_BODY) {
                        handle_in_body_start_fragment(t.name, t.self_closing, doc, &st, &fmt, &mode,
                                                      template_mode_stack, &template_mode_top,
                                                      DOC_NO_QUIRKS, t.attrs, t.attr_count);
                        break;
                    }
                    if (mode == MODE_IN_TABLE) {
                        if (t.name && strcmp(t.name, "caption") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "caption", NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            formatting_push_marker(&fmt);
                            mode = MODE_IN_CAPTION;
                            break;
                        }
                        if (t.name && strcmp(t.name, "colgroup") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "colgroup", NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            break;
                        }
                        if (t.name && strcmp(t.name, "col") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "col", NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            node_append_child(parent, n);
                            break;
                        }
                        if (t.name && strcmp(t.name, "select") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "select", NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            mode = MODE_IN_SELECT_IN_TABLE;
                            break;
                        }
                        if (t.name && is_table_section_element(t.name)) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, t.name, NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            mode = MODE_IN_TABLE_BODY;
                            break;
                        }
                        if (t.name && (strcmp(t.name, "tr") == 0 || is_cell_element(t.name))) {
                            /* Implicit <tbody> then reprocess in IN_TABLE_BODY */
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "tbody", NULL);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            mode = MODE_IN_TABLE_BODY;
                            reprocess = 1;
                            break;
                        }
                        if (!is_table_element(t.name)) {
                            if (t.name && strcmp(t.name, "template") == 0) {
                                node *table = NULL;
                                node *fp = foster_parent(&st, doc, &table);
                                node *tmpl = create_template_element(t.attrs, t.attr_count);
                                if (!tmpl) break;
                                if (table && fp == table->parent) {
                                    node_insert_before(fp, tmpl, table);
                                } else {
                                    node_append_child(fp, tmpl);
                                }
                                open_template_element(&st, &fmt, &mode, template_mode_stack, &template_mode_top,
                                                      tmpl, t.self_closing);
                                break;
                            }
                            n = node_create(NODE_ELEMENT, t.name ? t.name : "", NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            foster_insert(&st, doc, n);
                            if (!t.self_closing && !is_void_element(t.name)) {
                                stack_push(&st, n);
                            }
                            break;
                        }
                    } else if (mode == MODE_IN_TABLE_BODY) {
                        if (t.name && strcmp(t.name, "tr") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "tr", NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            mode = MODE_IN_ROW;
                            break;
                        }
                        if (t.name && is_cell_element(t.name)) {
                            parent = current_node(&st, doc);
                            node *tr = node_create(NODE_ELEMENT, "tr", NULL);
                            node_append_child(parent, tr);
                            stack_push(&st, tr);
                            node *cell = node_create(NODE_ELEMENT, t.name, NULL);
                            attach_attrs(cell, t.attrs, t.attr_count);
                            node_append_child(tr, cell);
                            stack_push(&st, cell);
                            formatting_push_marker(&fmt);
                            mode = MODE_IN_CELL;
                            break;
                        }
                        if (!is_table_element(t.name)) {
                            if (t.name && strcmp(t.name, "template") == 0) {
                                node *table = NULL;
                                node *fp = foster_parent(&st, doc, &table);
                                node *tmpl = create_template_element(t.attrs, t.attr_count);
                                if (!tmpl) break;
                                if (table && fp == table->parent) {
                                    node_insert_before(fp, tmpl, table);
                                } else {
                                    node_append_child(fp, tmpl);
                                }
                                open_template_element(&st, &fmt, &mode, template_mode_stack, &template_mode_top,
                                                      tmpl, t.self_closing);
                                break;
                            }
                            n = node_create(NODE_ELEMENT, t.name ? t.name : "", NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            foster_insert(&st, doc, n);
                            if (!t.self_closing && !is_void_element(t.name)) {
                                stack_push(&st, n);
                            }
                            break;
                        }
                    } else if (mode == MODE_IN_ROW) {
                        if (t.name && is_cell_element(t.name)) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, t.name, NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            formatting_push_marker(&fmt);
                            mode = MODE_IN_CELL;
                            break;
                        }
                        if (!is_table_element(t.name)) {
                            if (t.name && strcmp(t.name, "template") == 0) {
                                node *table = NULL;
                                node *fp = foster_parent(&st, doc, &table);
                                node *tmpl = create_template_element(t.attrs, t.attr_count);
                                if (!tmpl) break;
                                if (table && fp == table->parent) {
                                    node_insert_before(fp, tmpl, table);
                                } else {
                                    node_append_child(fp, tmpl);
                                }
                                open_template_element(&st, &fmt, &mode, template_mode_stack, &template_mode_top,
                                                      tmpl, t.self_closing);
                                break;
                            }
                            n = node_create(NODE_ELEMENT, t.name ? t.name : "", NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            foster_insert(&st, doc, n);
                            if (!t.self_closing && !is_void_element(t.name)) {
                                stack_push(&st, n);
                            }
                            break;
                        }
                    } else if (mode == MODE_IN_CELL) {
                        if (t.name && is_cell_element(t.name)) {
                            close_cell(&st, &fmt);
                            mode = MODE_IN_ROW;
                            reprocess = 1;
                            break;
                        }
                        if (t.name && strcmp(t.name, "select") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "select", NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            mode = MODE_IN_SELECT_IN_TABLE;
                            break;
                        }
                        handle_in_body_start_fragment(t.name, t.self_closing, doc, &st, &fmt, &mode,
                                                      template_mode_stack, &template_mode_top,
                                                      DOC_NO_QUIRKS, t.attrs, t.attr_count);
                    } else if (mode == MODE_IN_CAPTION) {
                        if (t.name && strcmp(t.name, "table") == 0) {
                            stack_pop_until(&st, "caption");
                            mode = MODE_IN_TABLE;
                            reprocess = 1;
                            break;
                        }
                        if (t.name && strcmp(t.name, "template") == 0) {
                            parent = current_node(&st, doc);
                            node *tmpl = create_template_element(t.attrs, t.attr_count);
                            if (!tmpl) break;
                            node_append_child(parent, tmpl);
                            open_template_element(&st, &fmt, &mode, template_mode_stack, &template_mode_top,
                                                  tmpl, t.self_closing);
                            break;
                        }
                        parent = current_node(&st, doc);
                        n = node_create(NODE_ELEMENT, t.name ? t.name : "", NULL);
                        attach_attrs(n, t.attrs, t.attr_count);
                        node_append_child(parent, n);
                        if (!t.self_closing && !is_void_element(t.name)) {
                            stack_push(&st, n);
                        }
                        break;
                    } else if (mode == MODE_IN_SELECT || mode == MODE_IN_SELECT_IN_TABLE) {
                        if (t.name && strcmp(t.name, "select") == 0) {
                            break;
                        }
                        /* Auto-close an open <option> before a new <option> or <optgroup> */
                        if (t.name && strcmp(t.name, "option") == 0 && stack_has_open_named(&st, "option")) {
                            stack_pop_until(&st, "option");
                        }
                        /* Auto-close an open <optgroup>: <option> closes it, <optgroup> closes it */
                        if (t.name && strcmp(t.name, "optgroup") == 0 && stack_has_open_named(&st, "optgroup")) {
                            if (stack_has_open_named(&st, "option")) {
                                stack_pop_until(&st, "option");
                            }
                            stack_pop_until(&st, "optgroup");
                        }
                        if (t.name && is_select_child_element(t.name)) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, t.name, NULL);
                            attach_attrs(n, t.attrs, t.attr_count);
                            node_append_child(parent, n);
                            if (!t.self_closing && !is_void_element(t.name)) {
                                stack_push(&st, n);
                            }
                            break;
                        }
                        parent = current_node(&st, doc);
                        n = node_create(NODE_ELEMENT, t.name ? t.name : "", NULL);
                        attach_attrs(n, t.attrs, t.attr_count);
                        node_append_child(parent, n);
                        if (!t.self_closing && !is_void_element(t.name)) {
                            stack_push(&st, n);
                        }
                    }
                    break;
                case TOKEN_END_TAG:
                    /* </template>: pop content + template, restore saved mode */
                    if (t.name && strcmp(t.name, "template") == 0 && stack_has_open_named(&st, "template")) {
                        close_template_element(&st, &fmt, &mode, template_mode_stack, &template_mode_top);
                        break;
                    }
                    /* Dedicated end tag handlers for implied-end-tag elements */
                    if (t.name && strcmp(t.name, "p") == 0 && mode == MODE_IN_BODY) {
                        if (!has_element_in_button_scope(&st, "p")) {
                            node *parent = current_node(&st, doc);
                            node *pn = node_create(NODE_ELEMENT, "p", NULL);
                            node_append_child(parent, pn);
                            break;
                        }
                        generate_implied_end_tags_except(&st, "p");
                        stack_pop_until(&st, "p");
                        break;
                    }
                    if (t.name && strcmp(t.name, "li") == 0 && mode == MODE_IN_BODY) {
                        if (!has_element_in_list_item_scope(&st, "li")) break;
                        generate_implied_end_tags_except(&st, "li");
                        stack_pop_until(&st, "li");
                        break;
                    }
                    if (t.name && (strcmp(t.name, "dd") == 0 || strcmp(t.name, "dt") == 0) && mode == MODE_IN_BODY) {
                        if (!has_element_in_scope(&st, t.name)) break;
                        generate_implied_end_tags_except(&st, t.name);
                        stack_pop_until(&st, t.name);
                        break;
                    }
                    /* End-tag mode transitions for table/cell/row/section/select/caption */
                    if (t.name && strcmp(t.name, "table") == 0 && has_element_in_table_scope(&st, "table")) {
                        if (mode == MODE_IN_CELL) {
                            formatting_clear_to_marker(&fmt);
                        }
                        stack_pop_until(&st, "table");
                        mode = MODE_IN_BODY;
                        break;
                    }
                    if (t.name && is_cell_element(t.name) && (mode == MODE_IN_CELL) && has_element_in_table_scope(&st, t.name)) {
                        stack_pop_until(&st, t.name);
                        formatting_clear_to_marker(&fmt);
                        mode = MODE_IN_ROW;
                        break;
                    }
                    if (t.name && strcmp(t.name, "tr") == 0 && mode == MODE_IN_ROW && has_element_in_table_scope(&st, "tr")) {
                        stack_pop_until(&st, "tr");
                        mode = stack_has_open_named(&st, "tbody") || stack_has_open_named(&st, "thead") || stack_has_open_named(&st, "tfoot")
                               ? MODE_IN_TABLE_BODY : MODE_IN_TABLE;
                        break;
                    }
                    if (t.name && is_table_section_element(t.name) && mode == MODE_IN_TABLE_BODY && has_element_in_table_scope(&st, t.name)) {
                        stack_pop_until(&st, t.name);
                        mode = MODE_IN_TABLE;
                        break;
                    }
                    if (t.name && strcmp(t.name, "caption") == 0 && mode == MODE_IN_CAPTION && has_element_in_table_scope(&st, "caption")) {
                        stack_pop_until(&st, "caption");
                        formatting_clear_to_marker(&fmt);
                        mode = MODE_IN_TABLE;
                        break;
                    }
                    if (t.name && strcmp(t.name, "select") == 0 && (mode == MODE_IN_SELECT || mode == MODE_IN_SELECT_IN_TABLE) && stack_has_open_named(&st, "select")) {
                        stack_pop_until(&st, "select");
                        mode = mode == MODE_IN_SELECT_IN_TABLE ? MODE_IN_TABLE : MODE_IN_BODY;
                        break;
                    }
                    if (mode == MODE_IN_BODY ||
                        mode == MODE_IN_CELL ||
                        mode == MODE_IN_TABLE ||
                        mode == MODE_IN_TABLE_BODY ||
                        mode == MODE_IN_ROW ||
                        mode == MODE_IN_CAPTION) {
                        if (adoption_agency(&st, &fmt, doc, t.name)) {
                            break;
                        }
                    }
                    if (t.name && !has_element_in_scope(&st, t.name)) {
                        break;
                    }
                    stack_pop_until(&st, t.name);
                    break;
                case TOKEN_COMMENT:
                    parent = current_node(&st, doc);
                    n = node_create(NODE_COMMENT, NULL, t.data ? t.data : "");
                    node_append_child(parent, n);
                    break;
                case TOKEN_CHARACTER:
                    if (t.data && t.data[0] != '\0') {
                        if (is_all_whitespace(t.data)) {
                            if (mode == MODE_IN_BODY) {
                                parent = current_node(&st, doc);
                                if (parent) {
                                    reconstruct_active_formatting(&st, &fmt, parent);
                                }
                            }
                            break;
                        }
                        if (is_table_mode(mode)) {
                            node *cur = current_node(&st, doc);
                            if (mode == MODE_IN_CELL || (cur && cur->name && !is_table_element(cur->name))) {
                                parent = cur;
                                n = node_create(NODE_TEXT, NULL, t.data);
                                node_append_child(parent, n);
                                break;
                            }
                            n = node_create(NODE_TEXT, NULL, t.data);
                            foster_insert(&st, doc, n);
                            break;
                        }
                        if (mode == MODE_IN_BODY) {
                            parent = current_node(&st, doc);
                            reconstruct_active_formatting(&st, &fmt, parent);
                        }
                        parent = current_node(&st, doc);
                        n = node_create(NODE_TEXT, NULL, t.data);
                        node_append_child(parent, n);
                    }
                    break;
                case TOKEN_EOF:
                default:
                    token_free(&t);
                    if (context) {
                        node *adopt = context;
                        if (context->name && strcmp(context->name, "template") == 0 &&
                            context->first_child && context->first_child->name &&
                            strcmp(context->first_child->name, "content") == 0) {
                            adopt = context->first_child;
                        }
                        node *child = adopt->first_child;
                        doc->first_child = child;
                        doc->last_child = adopt->last_child;
                        while (child) {
                            child->parent = doc;
                            if (!child->next_sibling) {
                                doc->last_child = child;
                            }
                            child = child->next_sibling;
                        }
                        if (adopt != context) {
                            node_free_shallow(adopt);
                        }
                        node_free_shallow(context);
                    }
                    return doc;
            }
        }

        token_free(&t);
    }
}
