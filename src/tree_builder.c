#include "tree_builder.h"
#include "tokenizer.h"

#include <stdlib.h>
#include <string.h>

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
    FMT_B,
    FMT_I,
    FMT_EM,
    FMT_STRONG,
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

static void stack_init(node_stack *st) {
    st->size = 0;
}

static void stack_push(node_stack *st, node *n) {
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

static const char *fmt_tag_name(fmt_tag t) {
    switch (t) {
        case FMT_B: return "b";
        case FMT_I: return "i";
        case FMT_EM: return "em";
        case FMT_STRONG: return "strong";
        default: return "";
    }
}

static fmt_tag fmt_tag_from_name(const char *name) {
    if (!name) return FMT_NONE;
    if (strcmp(name, "b") == 0) return FMT_B;
    if (strcmp(name, "i") == 0) return FMT_I;
    if (strcmp(name, "em") == 0) return FMT_EM;
    if (strcmp(name, "strong") == 0) return FMT_STRONG;
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

static int stack_has_open_table_section(node_stack *st) {
    return stack_has_open_named(st, "thead") || stack_has_open_named(st, "tbody") || stack_has_open_named(st, "tfoot");
}

static int is_scoping_element(const char *name) {
    if (!name) return 0;
    return strcmp(name, "html") == 0 ||
           strcmp(name, "table") == 0 ||
           strcmp(name, "td") == 0 ||
           strcmp(name, "th") == 0 ||
           strcmp(name, "caption") == 0;
}

static int has_element_in_scope(node_stack *st, const char *name) {
    if (!name) return 0;
    for (size_t i = st->size; i > 0; --i) {
        node *n = st->items[i - 1];
        if (!n || !n->name) continue;
        if (strcmp(n->name, name) == 0) return 1;
        if (is_scoping_element(n->name)) return 0;
    }
    return 0;
}

static void formatting_remove_tag(formatting_list *fl, fmt_tag tag) {
    if (!fl || tag == FMT_NONE) return;
    for (size_t i = fl->count; i > 0; --i) {
        if (fl->items[i - 1].tag == tag) {
            for (size_t j = i; j < fl->count; ++j) {
                fl->items[j - 1] = fl->items[j];
            }
            fl->count--;
            return;
        }
    }
}

static void formatting_push(formatting_list *fl, fmt_tag tag, node *element) {
    if (!fl || tag == FMT_NONE || !element) return;
    size_t count_same = 0;
    size_t earliest_index = 0;
    for (size_t i = 0; i < fl->count; ++i) {
        if (fl->items[i].tag == tag) {
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
        const char *name = fmt_tag_name(fl->items[i].tag);
        if (name[0] == '\0') continue;
        node *n = node_create(NODE_ELEMENT, name, NULL);
        node_append_child(parent, n);
        stack_push(st, n);
        fl->items[i].element = n;
    }
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
                                          formatting_list *fmt, insertion_mode *mode) {
    /* Table-related tags are parse errors in IN_BODY; ignore them. */
    if (is_body_ignored_start(name)) {
        return;
    }
    /* <table> closes an open <p>, then switches to IN_TABLE. */
    if (name && strcmp(name, "table") == 0) {
        body_autoclose_on_start(st, name, DOC_NO_QUIRKS);
        node *parent = current_node(st, doc);
        node *n = node_create(NODE_ELEMENT, "table", NULL);
        node_append_child(parent, n);
        stack_push(st, n);
        *mode = MODE_IN_TABLE;
        return;
    }
    /* <select> in body switches to IN_SELECT. */
    if (name && strcmp(name, "select") == 0) {
        node *parent = current_node(st, doc);
        node *n = node_create(NODE_ELEMENT, "select", NULL);
        node_append_child(parent, n);
        stack_push(st, n);
        *mode = MODE_IN_SELECT;
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
    if ((strcmp(tag_name, "p") == 0 || is_body_block_like_start(tag_name)) && stack_has_open_named(st, "p")) {
        stack_pop_until(st, "p"); /* pops the matching <p> as well */
    }

    /* Auto-close previous <li> when a new <li> starts. */
    if (strcmp(tag_name, "li") == 0 && stack_has_open_named(st, "li")) {
        stack_pop_until(st, "li");
    }

    /* Auto-close <dt>/<dd> when the other starts. */
    if ((strcmp(tag_name, "dt") == 0 || strcmp(tag_name, "dd") == 0) && (stack_has_open_named(st, "dt") || stack_has_open_named(st, "dd"))) {
        stack_pop_until(st, "dt");
        stack_pop_until(st, "dd");
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
                                 formatting_list *fmt, insertion_mode *mode, doc_mode dmode) {
    if (!mode) return;
    fmt_tag ft = fmt_tag_from_name(name);
    if (ft != FMT_NONE) {
        node *parent = current_node(st, doc);
        reconstruct_active_formatting(st, fmt, parent);
    }
    if (name && strcmp(name, "html") == 0) {
        return;
    }
    if (name && strcmp(name, "body") == 0) {
        ensure_body(doc, st, html, body);
        return;
    }
    if (name && strcmp(name, "select") == 0) {
        ensure_body(doc, st, html, body);
        node *parent = current_node(st, doc);
        node *n = node_create(NODE_ELEMENT, "select", NULL);
        node_append_child(parent, n);
        stack_push(st, n);
        *mode = MODE_IN_SELECT;
        return;
    }
    if (name && strcmp(name, "table") == 0) {
        if (stack_has_open_named(st, "p")) {
            stack_pop_until(st, "p");
        }
        ensure_body(doc, st, html, body);
        node *parent = current_node(st, doc);
        node *n = node_create(NODE_ELEMENT, "table", NULL);
        node_append_child(parent, n);
        stack_push(st, n);
        *mode = MODE_IN_TABLE;
        return;
    }
    body_autoclose_on_start(st, name, dmode);
    ensure_body(doc, st, html, body);
    node *parent = current_node(st, doc);
    node *n = node_create(NODE_ELEMENT, name ? name : "", NULL);
    node_append_child(parent, n);
    if (!self_closing) {
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

static node *ensure_html(node *doc, node_stack *st, node **html_out) {
    if (*html_out) return *html_out;
    *html_out = node_create(NODE_ELEMENT, "html", NULL);
    node_append_child(doc, *html_out);
    stack_push(st, *html_out);
    return *html_out;
}

static node *ensure_body(node *doc, node_stack *st, node **html_out, node **body_out) {
    node *html = ensure_html(doc, st, html_out);
    if (*body_out) {
        if (!stack_contains(st, *body_out)) {
            stack_push(st, *body_out);
        }
        return *body_out;
    }
    *body_out = node_create(NODE_ELEMENT, "body", NULL);
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

node *build_tree_from_tokens(const token *tokens, size_t count) {
    node *doc = node_create(NODE_DOCUMENT, NULL, NULL);
    node_stack st;
    insertion_mode mode = MODE_INITIAL;
    doc_mode dmode = DOC_NO_QUIRKS;
    node *html = NULL;
    node *head = NULL;
    node *body = NULL;
    formatting_list fmt = {0};
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
                        if (!is_head_element(t->name)) {
                            close_head(&st, &head, &mode);
                            reprocess = 1;
                            break;
                        }
                    }
                    if (is_table_mode(mode)) {
                        node *cur = current_node(&st, doc);
                        if (cur && cur->name && !is_table_element(cur->name)) {
                            handle_in_body_start(t->name, t->self_closing, doc, &st, &html, &body, &fmt, &mode, dmode);
                            break;
                        }
                    }
                    if (mode == MODE_IN_BODY) {
                        handle_in_body_start(t->name, t->self_closing, doc, &st, &html, &body, &fmt, &mode, dmode);
                        break;
                    }
                    if (mode == MODE_IN_TABLE) {
                        if (t->name && strcmp(t->name, "caption") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "caption", NULL);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            formatting_push_marker(&fmt);
                            mode = MODE_IN_CAPTION;
                            break;
                        }
                        if (t->name && strcmp(t->name, "colgroup") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "colgroup", NULL);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            break;
                        }
                        if (t->name && strcmp(t->name, "col") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "col", NULL);
                            node_append_child(parent, n);
                            break;
                        }
                        if (t->name && strcmp(t->name, "select") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "select", NULL);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            mode = MODE_IN_SELECT_IN_TABLE;
                            break;
                        }
                        if (t->name && is_table_section_element(t->name)) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, t->name, NULL);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            mode = MODE_IN_TABLE_BODY;
                            break;
                        }
                        if (t->name && strcmp(t->name, "tr") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "tr", NULL);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            mode = MODE_IN_ROW;
                            break;
                        }
                        if (t->name && is_cell_element(t->name)) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, t->name, NULL);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            formatting_push_marker(&fmt);
                            mode = MODE_IN_CELL;
                            break;
                        }
                        if (!is_table_element(t->name)) {
                            fmt_tag ft = fmt_tag_from_name(t->name);
                            node *table = NULL;
                            node *fp = foster_parent(&st, doc, &table);
                            if (ft != FMT_NONE) {
                                reconstruct_active_formatting(&st, &fmt, fp);
                            }
                            n = node_create(NODE_ELEMENT, t->name ? t->name : "", NULL);
                            if (table && fp == table->parent) {
                                node_insert_before(fp, n, table);
                            } else {
                                node_append_child(fp, n);
                            }
                            if (!t->self_closing) {
                                stack_push(&st, n);
                                if (ft != FMT_NONE) formatting_push(&fmt, ft, n);
                            }
                            break;
                        }
                    } else if (mode == MODE_IN_HEAD) {
                        parent = current_node(&st, doc);
                        n = node_create(NODE_ELEMENT, t->name ? t->name : "", NULL);
                        node_append_child(parent, n);
                        if (!t->self_closing) {
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
                            node_append_child(tr, cell);
                            stack_push(&st, cell);
                            formatting_push_marker(&fmt);
                            mode = MODE_IN_CELL;
                            break;
                        }
                        if (!is_table_element(t->name)) {
                            fmt_tag ft = fmt_tag_from_name(t->name);
                            node *table = NULL;
                            node *fp = foster_parent(&st, doc, &table);
                            if (ft != FMT_NONE) {
                                reconstruct_active_formatting(&st, &fmt, fp);
                            }
                            n = node_create(NODE_ELEMENT, t->name ? t->name : "", NULL);
                            if (table && fp == table->parent) {
                                node_insert_before(fp, n, table);
                            } else {
                                node_append_child(fp, n);
                            }
                            if (!t->self_closing) {
                                stack_push(&st, n);
                                if (ft != FMT_NONE) formatting_push(&fmt, ft, n);
                            }
                            break;
                        }
                    } else if (mode == MODE_IN_ROW) {
                        if (t->name && is_cell_element(t->name)) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, t->name, NULL);
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
                            fmt_tag ft = fmt_tag_from_name(t->name);
                            node *table = NULL;
                            node *fp = foster_parent(&st, doc, &table);
                            if (ft != FMT_NONE) {
                                reconstruct_active_formatting(&st, &fmt, fp);
                            }
                            n = node_create(NODE_ELEMENT, t->name ? t->name : "", NULL);
                            if (table && fp == table->parent) {
                                node_insert_before(fp, n, table);
                            } else {
                                node_append_child(fp, n);
                            }
                            if (!t->self_closing) {
                                stack_push(&st, n);
                                if (ft != FMT_NONE) formatting_push(&fmt, ft, n);
                            }
                            break;
                        }
                    } else if (mode == MODE_IN_CELL) {
                        if (t->name && strcmp(t->name, "select") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "select", NULL);
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
                        handle_in_body_start(t->name, t->self_closing, doc, &st, &html, &body, &fmt, &mode, dmode);
                    } else if (mode == MODE_IN_CAPTION) {
                        if (t->name && (strcmp(t->name, "table") == 0 || strcmp(t->name, "tr") == 0 || is_table_section_element(t->name))) {
                            stack_pop_until(&st, "caption");
                            mode = MODE_IN_TABLE;
                            reprocess = 1;
                            break;
                        }
                        parent = current_node(&st, doc);
                        n = node_create(NODE_ELEMENT, t->name ? t->name : "", NULL);
                        node_append_child(parent, n);
                        if (!t->self_closing) {
                            stack_push(&st, n);
                        }
                        break;
                    } else if (mode == MODE_IN_SELECT || mode == MODE_IN_SELECT_IN_TABLE) {
                        if (t->name && strcmp(t->name, "select") == 0) {
                            /* ignore nested select */
                            break;
                        }
                        if (t->name && is_select_child_element(t->name)) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, t->name, NULL);
                            node_append_child(parent, n);
                            if (!t->self_closing) {
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
                        node_append_child(parent, n);
                        if (!t->self_closing) {
                            stack_push(&st, n);
                        }
                    }
                    break;
                case TOKEN_END_TAG:
                    if (t->name && strcmp(t->name, "head") == 0 && mode == MODE_IN_HEAD) {
                        close_head(&st, &head, &mode);
                        break;
                    }
                    if (t->name && strcmp(t->name, "body") == 0 && mode == MODE_IN_BODY) {
                        stack_pop_until(&st, "body");
                        mode = MODE_AFTER_BODY;
                        break;
                    }
                    if (t->name && strcmp(t->name, "table") == 0) {
                        if (mode == MODE_IN_CELL) {
                            formatting_clear_to_marker(&fmt);
                        }
                        stack_pop_until(&st, "table");
                        mode = MODE_IN_BODY;
                        break;
                    }
                    if (t->name && strcmp(t->name, "tr") == 0 && mode == MODE_IN_ROW) {
                        stack_pop_until(&st, "tr");
                        mode = stack_has_open_table_section(&st) ? MODE_IN_TABLE_BODY : MODE_IN_TABLE;
                        break;
                    }
                    if (t->name && is_cell_element(t->name) && mode == MODE_IN_CELL) {
                        stack_pop_until(&st, t->name);
                        formatting_clear_to_marker(&fmt);
                        mode = MODE_IN_ROW;
                        break;
                    }
                    if (t->name && is_table_section_element(t->name) && mode == MODE_IN_CELL) {
                        close_cell(&st, &fmt);
                        stack_pop_until(&st, t->name);
                        mode = MODE_IN_TABLE;
                        break;
                    }
                    if (t->name && is_table_section_element(t->name) && (mode == MODE_IN_TABLE || mode == MODE_IN_TABLE_BODY)) {
                        stack_pop_until(&st, t->name);
                        mode = MODE_IN_TABLE;
                        break;
                    }
                    if (t->name && strcmp(t->name, "caption") == 0 && mode == MODE_IN_CAPTION) {
                        stack_pop_until(&st, "caption");
                        formatting_clear_to_marker(&fmt);
                        mode = MODE_IN_TABLE;
                        break;
                    }
                    if (t->name && strcmp(t->name, "select") == 0 && (mode == MODE_IN_SELECT || mode == MODE_IN_SELECT_IN_TABLE)) {
                        stack_pop_until(&st, "select");
                        if (mode == MODE_IN_SELECT_IN_TABLE) {
                            mode = (stack_has_open_named(&st, "td") || stack_has_open_named(&st, "th")) ? MODE_IN_CELL : MODE_IN_TABLE;
                        } else {
                            mode = MODE_IN_BODY;
                        }
                        break;
                    }
                    if (t->name && strcmp(t->name, "html") == 0) {
                        stack_pop_until(&st, "html");
                        if (mode == MODE_AFTER_BODY) {
                            mode = MODE_AFTER_AFTER_BODY;
                        }
                        break;
                    }
                    if (mode == MODE_IN_BODY) {
                        fmt_tag ft = fmt_tag_from_name(t->name);
                        if (ft != FMT_NONE) {
                            if (!has_element_in_scope(&st, t->name)) {
                                break;
                            }
                            formatting_remove_tag(&fmt, ft);
                        }
                    }
                    if (t->name && !stack_has_open_named(&st, t->name)) {
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

    if (!doc) return NULL;
    stack_init(&st);
    tokenizer_init(&tz, input);

    while (1) {
        token_init(&t);
        tokenizer_next(&tz, &t);

        node *parent;
        node *n = NULL;
        int reprocess = 1;

        while (reprocess) {
            reprocess = 0;
            parent = current_node(&st, doc);

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
                        if (!is_head_element(t.name)) {
                            close_head(&st, &head, &mode);
                            reprocess = 1;
                            break;
                        }
                    }
                    if (is_table_mode(mode)) {
                        node *cur = current_node(&st, doc);
                        if (cur && cur->name && !is_table_element(cur->name)) {
                            handle_in_body_start(t.name, t.self_closing, doc, &st, &html, &body, &fmt, &mode, dmode);
                            break;
                        }
                    }
                    if (mode == MODE_IN_BODY) {
                        handle_in_body_start(t.name, t.self_closing, doc, &st, &html, &body, &fmt, &mode, dmode);
                        break;
                    }
                    if (mode == MODE_IN_TABLE) {
                        if (t.name && strcmp(t.name, "caption") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "caption", NULL);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            formatting_push_marker(&fmt);
                            mode = MODE_IN_CAPTION;
                            break;
                        }
                        if (t.name && strcmp(t.name, "colgroup") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "colgroup", NULL);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            break;
                        }
                        if (t.name && strcmp(t.name, "col") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "col", NULL);
                            node_append_child(parent, n);
                            break;
                        }
                        if (t.name && strcmp(t.name, "select") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "select", NULL);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            mode = MODE_IN_SELECT_IN_TABLE;
                            break;
                        }
                        if (t.name && is_table_section_element(t.name)) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, t.name, NULL);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            mode = MODE_IN_TABLE_BODY;
                            break;
                        }
                        if (t.name && strcmp(t.name, "tr") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "tr", NULL);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            mode = MODE_IN_ROW;
                            break;
                        }
                        if (t.name && is_cell_element(t.name)) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, t.name, NULL);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            formatting_push_marker(&fmt);
                            mode = MODE_IN_CELL;
                            break;
                        }
                        if (!is_table_element(t.name)) {
                            fmt_tag ft = fmt_tag_from_name(t.name);
                            node *table = NULL;
                            node *fp = foster_parent(&st, doc, &table);
                            if (ft != FMT_NONE) {
                                reconstruct_active_formatting(&st, &fmt, fp);
                            }
                            n = node_create(NODE_ELEMENT, t.name ? t.name : "", NULL);
                            if (table && fp == table->parent) {
                                node_insert_before(fp, n, table);
                            } else {
                                node_append_child(fp, n);
                            }
                            if (!t.self_closing) {
                                stack_push(&st, n);
                                if (ft != FMT_NONE) formatting_push(&fmt, ft, n);
                            }
                            break;
                        }
                    } else if (mode == MODE_IN_HEAD) {
                        parent = current_node(&st, doc);
                        n = node_create(NODE_ELEMENT, t.name ? t.name : "", NULL);
                        node_append_child(parent, n);
                        if (!t.self_closing) {
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
                            node_append_child(tr, cell);
                            stack_push(&st, cell);
                            formatting_push_marker(&fmt);
                            mode = MODE_IN_CELL;
                            break;
                        }
                        if (!is_table_element(t.name)) {
                            fmt_tag ft = fmt_tag_from_name(t.name);
                            node *table = NULL;
                            node *fp = foster_parent(&st, doc, &table);
                            if (ft != FMT_NONE) {
                                reconstruct_active_formatting(&st, &fmt, fp);
                            }
                            n = node_create(NODE_ELEMENT, t.name ? t.name : "", NULL);
                            if (table && fp == table->parent) {
                                node_insert_before(fp, n, table);
                            } else {
                                node_append_child(fp, n);
                            }
                            if (!t.self_closing) {
                                stack_push(&st, n);
                                if (ft != FMT_NONE) formatting_push(&fmt, ft, n);
                            }
                            break;
                        }
                    } else if (mode == MODE_IN_ROW) {
                        if (t.name && is_cell_element(t.name)) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, t.name, NULL);
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
                            fmt_tag ft = fmt_tag_from_name(t.name);
                            node *table = NULL;
                            node *fp = foster_parent(&st, doc, &table);
                            if (ft != FMT_NONE) {
                                reconstruct_active_formatting(&st, &fmt, fp);
                            }
                            n = node_create(NODE_ELEMENT, t.name ? t.name : "", NULL);
                            if (table && fp == table->parent) {
                                node_insert_before(fp, n, table);
                            } else {
                                node_append_child(fp, n);
                            }
                            if (!t.self_closing) {
                                stack_push(&st, n);
                                if (ft != FMT_NONE) formatting_push(&fmt, ft, n);
                            }
                            break;
                        }
                    } else if (mode == MODE_IN_CELL) {
                        if (t.name && strcmp(t.name, "select") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "select", NULL);
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
                        handle_in_body_start(t.name, t.self_closing, doc, &st, &html, &body, &fmt, &mode, dmode);
                    } else if (mode == MODE_IN_CAPTION) {
                        if (t.name && (strcmp(t.name, "table") == 0 || strcmp(t.name, "tr") == 0 || is_table_section_element(t.name))) {
                            stack_pop_until(&st, "caption");
                            mode = MODE_IN_TABLE;
                            reprocess = 1;
                            break;
                        }
                        parent = current_node(&st, doc);
                        n = node_create(NODE_ELEMENT, t.name ? t.name : "", NULL);
                        node_append_child(parent, n);
                        if (!t.self_closing) {
                            stack_push(&st, n);
                        }
                        break;
                    } else if (mode == MODE_IN_SELECT || mode == MODE_IN_SELECT_IN_TABLE) {
                        if (t.name && strcmp(t.name, "select") == 0) {
                            break;
                        }
                        if (t.name && is_select_child_element(t.name)) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, t.name, NULL);
                            node_append_child(parent, n);
                            if (!t.self_closing) {
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
                        node_append_child(parent, n);
                        if (!t.self_closing) {
                            stack_push(&st, n);
                        }
                    }
                    break;
                case TOKEN_END_TAG:
                    if (t.name && strcmp(t.name, "head") == 0 && mode == MODE_IN_HEAD) {
                        close_head(&st, &head, &mode);
                        break;
                    }
                    if (t.name && strcmp(t.name, "body") == 0 && mode == MODE_IN_BODY) {
                        stack_pop_until(&st, "body");
                        mode = MODE_AFTER_BODY;
                        break;
                    }
                    if (t.name && strcmp(t.name, "table") == 0) {
                        if (mode == MODE_IN_CELL) {
                            formatting_clear_to_marker(&fmt);
                        }
                        stack_pop_until(&st, "table");
                        mode = MODE_IN_BODY;
                        break;
                    }
                    if (t.name && strcmp(t.name, "tr") == 0 && mode == MODE_IN_ROW) {
                        stack_pop_until(&st, "tr");
                        mode = stack_has_open_table_section(&st) ? MODE_IN_TABLE_BODY : MODE_IN_TABLE;
                        break;
                    }
                    if (t.name && is_cell_element(t.name) && mode == MODE_IN_CELL) {
                        stack_pop_until(&st, t.name);
                        formatting_clear_to_marker(&fmt);
                        mode = MODE_IN_ROW;
                        break;
                    }
                    if (t.name && is_table_section_element(t.name) && mode == MODE_IN_CELL) {
                        close_cell(&st, &fmt);
                        stack_pop_until(&st, t.name);
                        mode = MODE_IN_TABLE;
                        break;
                    }
                    if (t.name && is_table_section_element(t.name) && (mode == MODE_IN_TABLE || mode == MODE_IN_TABLE_BODY)) {
                        stack_pop_until(&st, t.name);
                        mode = MODE_IN_TABLE;
                        break;
                    }
                    if (t.name && strcmp(t.name, "caption") == 0 && mode == MODE_IN_CAPTION) {
                        stack_pop_until(&st, "caption");
                        formatting_clear_to_marker(&fmt);
                        mode = MODE_IN_TABLE;
                        break;
                    }
                    if (t.name && strcmp(t.name, "select") == 0 && (mode == MODE_IN_SELECT || mode == MODE_IN_SELECT_IN_TABLE)) {
                        stack_pop_until(&st, "select");
                        if (mode == MODE_IN_SELECT_IN_TABLE) {
                            mode = (stack_has_open_named(&st, "td") || stack_has_open_named(&st, "th")) ? MODE_IN_CELL : MODE_IN_TABLE;
                        } else {
                            mode = MODE_IN_BODY;
                        }
                        break;
                    }
                    if (t.name && strcmp(t.name, "html") == 0) {
                        stack_pop_until(&st, "html");
                        if (mode == MODE_AFTER_BODY) {
                            mode = MODE_AFTER_AFTER_BODY;
                        }
                        break;
                    }
                    if (mode == MODE_IN_BODY) {
                        fmt_tag ft = fmt_tag_from_name(t.name);
                        if (ft != FMT_NONE) {
                            if (!has_element_in_scope(&st, t.name)) {
                                break;
                            }
                            formatting_remove_tag(&fmt, ft);
                        }
                    }
                    if (t.name && !stack_has_open_named(&st, t.name)) {
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
        context = node_create(NODE_ELEMENT, context_tag, NULL);
        stack_push(&st, context);
        mode = fragment_mode_for_context(context_tag);
    }

    tokenizer_init_with_context(&tz, input, context_tag);

    while (1) {
        token_init(&t);
        tokenizer_next(&tz, &t);

        node *parent;
        node *n = NULL;
        int reprocess = 1;

        while (reprocess) {
            reprocess = 0;
            parent = current_node(&st, doc);

            switch (t.type) {
                case TOKEN_START_TAG:
                    if (mode == MODE_IN_HEAD) {
                        if (t.name && !is_head_element(t.name)) {
                            mode = MODE_IN_BODY;
                            reprocess = 1;
                            break;
                        }
                    }
                    /* <template>: push content DocumentFragment wrapper */
                    if (t.name && strcmp(t.name, "template") == 0) {
                        parent = current_node(&st, doc);
                        n = node_create(NODE_ELEMENT, "template", NULL);
                        node_append_child(parent, n);
                        stack_push(&st, n);
                        node *content = node_create(NODE_ELEMENT, "content", NULL);
                        node_append_child(n, content);
                        stack_push(&st, content);
                        if (template_mode_top < 64)
                            template_mode_stack[template_mode_top++] = mode;
                        mode = MODE_IN_BODY;
                        break;
                    }
                    if (is_table_mode(mode)) {
                        node *cur = current_node(&st, doc);
                        if (cur && cur->name && !is_table_element(cur->name)) {
                            handle_in_body_start_fragment(t.name, t.self_closing, doc, &st, &fmt, &mode);
                            break;
                        }
                    }
                    if (mode == MODE_IN_BODY) {
                        handle_in_body_start_fragment(t.name, t.self_closing, doc, &st, &fmt, &mode);
                        break;
                    }
                    if (mode == MODE_IN_TABLE) {
                        if (t.name && strcmp(t.name, "caption") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "caption", NULL);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            formatting_push_marker(&fmt);
                            mode = MODE_IN_CAPTION;
                            break;
                        }
                        if (t.name && strcmp(t.name, "colgroup") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "colgroup", NULL);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            break;
                        }
                        if (t.name && strcmp(t.name, "col") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "col", NULL);
                            node_append_child(parent, n);
                            break;
                        }
                        if (t.name && strcmp(t.name, "select") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "select", NULL);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            mode = MODE_IN_SELECT_IN_TABLE;
                            break;
                        }
                        if (t.name && is_table_section_element(t.name)) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, t.name, NULL);
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
                            n = node_create(NODE_ELEMENT, t.name ? t.name : "", NULL);
                            foster_insert(&st, doc, n);
                            if (!t.self_closing) {
                                stack_push(&st, n);
                            }
                            break;
                        }
                    } else if (mode == MODE_IN_TABLE_BODY) {
                        if (t.name && strcmp(t.name, "tr") == 0) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, "tr", NULL);
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
                            node_append_child(tr, cell);
                            stack_push(&st, cell);
                            formatting_push_marker(&fmt);
                            mode = MODE_IN_CELL;
                            break;
                        }
                        if (!is_table_element(t.name)) {
                            n = node_create(NODE_ELEMENT, t.name ? t.name : "", NULL);
                            foster_insert(&st, doc, n);
                            if (!t.self_closing) {
                                stack_push(&st, n);
                            }
                            break;
                        }
                    } else if (mode == MODE_IN_ROW) {
                        if (t.name && is_cell_element(t.name)) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, t.name, NULL);
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            formatting_push_marker(&fmt);
                            mode = MODE_IN_CELL;
                            break;
                        }
                        if (!is_table_element(t.name)) {
                            n = node_create(NODE_ELEMENT, t.name ? t.name : "", NULL);
                            foster_insert(&st, doc, n);
                            if (!t.self_closing) {
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
                            node_append_child(parent, n);
                            stack_push(&st, n);
                            mode = MODE_IN_SELECT_IN_TABLE;
                            break;
                        }
                        handle_in_body_start_fragment(t.name, t.self_closing, doc, &st, &fmt, &mode);
                    } else if (mode == MODE_IN_CAPTION) {
                        if (t.name && strcmp(t.name, "table") == 0) {
                            stack_pop_until(&st, "caption");
                            mode = MODE_IN_TABLE;
                            reprocess = 1;
                            break;
                        }
                        parent = current_node(&st, doc);
                        n = node_create(NODE_ELEMENT, t.name ? t.name : "", NULL);
                        node_append_child(parent, n);
                        if (!t.self_closing) {
                            stack_push(&st, n);
                        }
                        break;
                    } else if (mode == MODE_IN_SELECT || mode == MODE_IN_SELECT_IN_TABLE) {
                        if (t.name && strcmp(t.name, "select") == 0) {
                            break;
                        }
                        /* Auto-close an open <option> before a new <option> */
                        if (t.name && strcmp(t.name, "option") == 0 && stack_has_open_named(&st, "option")) {
                            stack_pop_until(&st, "option");
                        }
                        if (t.name && is_select_child_element(t.name)) {
                            parent = current_node(&st, doc);
                            n = node_create(NODE_ELEMENT, t.name, NULL);
                            node_append_child(parent, n);
                            if (!t.self_closing) {
                                stack_push(&st, n);
                            }
                            break;
                        }
                        parent = current_node(&st, doc);
                        n = node_create(NODE_ELEMENT, t.name ? t.name : "", NULL);
                        node_append_child(parent, n);
                        if (!t.self_closing) {
                            stack_push(&st, n);
                        }
                    }
                    break;
                case TOKEN_END_TAG:
                    /* </template>: pop content + template, restore saved mode */
                    if (t.name && strcmp(t.name, "template") == 0 && stack_has_open_named(&st, "template")) {
                        stack_pop_until(&st, "template");
                        if (template_mode_top > 0)
                            mode = template_mode_stack[--template_mode_top];
                        break;
                    }
                    /* End-tag mode transitions for table/cell/row/section/select/caption */
                    if (t.name && strcmp(t.name, "table") == 0 && stack_has_open_named(&st, "table")) {
                        if (mode == MODE_IN_CELL) {
                            formatting_clear_to_marker(&fmt);
                        }
                        stack_pop_until(&st, "table");
                        mode = MODE_IN_BODY;
                        break;
                    }
                    if (t.name && is_cell_element(t.name) && (mode == MODE_IN_CELL) && stack_has_open_named(&st, t.name)) {
                        stack_pop_until(&st, t.name);
                        formatting_clear_to_marker(&fmt);
                        mode = MODE_IN_ROW;
                        break;
                    }
                    if (t.name && strcmp(t.name, "tr") == 0 && mode == MODE_IN_ROW && stack_has_open_named(&st, "tr")) {
                        stack_pop_until(&st, "tr");
                        mode = stack_has_open_named(&st, "tbody") || stack_has_open_named(&st, "thead") || stack_has_open_named(&st, "tfoot")
                               ? MODE_IN_TABLE_BODY : MODE_IN_TABLE;
                        break;
                    }
                    if (t.name && is_table_section_element(t.name) && mode == MODE_IN_TABLE_BODY && stack_has_open_named(&st, t.name)) {
                        stack_pop_until(&st, t.name);
                        mode = MODE_IN_TABLE;
                        break;
                    }
                    if (t.name && strcmp(t.name, "caption") == 0 && mode == MODE_IN_CAPTION && stack_has_open_named(&st, "caption")) {
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
                    if (mode == MODE_IN_BODY) {
                        fmt_tag ft = fmt_tag_from_name(t.name);
                        if (ft != FMT_NONE) {
                            if (!has_element_in_scope(&st, t.name)) {
                                break;
                            }
                            formatting_remove_tag(&fmt, ft);
                        }
                    }
                    if (t.name && !stack_has_open_named(&st, t.name)) {
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
                        node *child = context->first_child;
                        doc->first_child = child;
                        doc->last_child = context->last_child;
                        while (child) {
                            child->parent = doc;
                            if (!child->next_sibling) {
                                doc->last_child = child;
                            }
                            child = child->next_sibling;
                        }
                        node_free_shallow(context);
                    }
                    return doc;
            }
        }

        token_free(&t);
    }
}
