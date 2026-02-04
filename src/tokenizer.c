#include "tokenizer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_ascii_whitespace(char c) {
    return c == ' ' || c == '\n' || c == '\t' || c == '\f' || c == '\r';
}

static int is_tag_start(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int is_attr_name_char(char c) {
    return isalnum((unsigned char)c) || c == '-' || c == '_' || c == ':';
}

static char to_lower_ascii(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static char peek(const tokenizer *tz, size_t ahead) {
    size_t idx = tz->pos + ahead;
    if (idx >= tz->len) return '\0';
    return tz->input[idx];
}

static void advance(tokenizer *tz, size_t n) {
    size_t i;
    if (!tz) return;
    for (i = 0; i < n && tz->pos < tz->len; ++i) {
        char c = tz->input[tz->pos++];
        if (c == '\n') {
            tz->line++;
            tz->col = 1;
        } else {
            tz->col++;
        }
    }
}

static char *substr_dup(const char *s, size_t start, size_t end) {
    size_t len = (end > start) ? (end - start) : 0;
    char *out = (char *)malloc(len + 1);
    if (!out) return NULL;
    if (len > 0) memcpy(out, s + start, len);
    out[len] = '\0';
    return out;
}

static char *dup_string(const char *s) {
    size_t len;
    char *out;
    if (!s) s = "";
    len = strlen(s);
    out = (char *)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len + 1);
    return out;
}

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} strbuf;

static void sb_init(strbuf *sb) {
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static void sb_free(strbuf *sb) {
    free(sb->buf);
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static int sb_reserve(strbuf *sb, size_t extra) {
    if (sb->len + extra + 1 <= sb->cap) return 1;
    size_t new_cap = sb->cap ? sb->cap * 2 : 32;
    while (new_cap < sb->len + extra + 1) new_cap *= 2;
    char *next = (char *)realloc(sb->buf, new_cap);
    if (!next) return 0;
    sb->buf = next;
    sb->cap = new_cap;
    return 1;
}

static int sb_push_char(strbuf *sb, char c) {
    if (!sb_reserve(sb, 1)) return 0;
    sb->buf[sb->len++] = c;
    sb->buf[sb->len] = '\0';
    return 1;
}

static char *sb_to_string(strbuf *sb) {
    if (!sb->buf) return dup_string("");
    char *out = (char *)malloc(sb->len + 1);
    if (!out) return NULL;
    memcpy(out, sb->buf, sb->len + 1);
    return out;
}

static void report_error(const tokenizer *tz, const char *msg) {
    if (!msg) return;
    printf("[parse error] line=%zu col=%zu: %s\n", tz ? tz->line : 0, tz ? tz->col : 0, msg);
}

static int is_hex_digit(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static size_t encode_utf8(unsigned int codepoint, char *out) {
    if (codepoint <= 0x7F) {
        out[0] = (char)codepoint;
        return 1;
    }
    if (codepoint <= 0x7FF) {
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    }
    if (codepoint <= 0xFFFF) {
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    }
    if (codepoint <= 0x10FFFF) {
        out[0] = (char)(0xF0 | (codepoint >> 18));
        out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
    out[0] = '?';
    return 1;
}

typedef struct {
    char *name;
    char *value;
} named_entity;

static int is_ascii_alnum(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z');
}

static named_entity *g_entities = NULL;
static size_t g_entities_count = 0;
static int g_entities_loaded = 0;

static void entities_add(const char *name, const char *value) {
    named_entity *next = (named_entity *)realloc(g_entities, sizeof(named_entity) * (g_entities_count + 1));
    if (!next) return;
    g_entities = next;
    g_entities[g_entities_count].name = dup_string(name);
    g_entities[g_entities_count].value = dup_string(value);
    g_entities_count++;
}

static void entities_load_from_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = '\0';
        char *name = line;
        char *value = tab + 1;
        char *nl = strchr(value, '\n');
        if (nl) *nl = '\0';
        if (name[0] == '\0' || value[0] == '\0') continue;
        entities_add(name, value);
    }
    fclose(fp);
}

static void entities_load_once(void) {
    if (g_entities_loaded) return;
    g_entities_loaded = 1;

    /* Try to load full list from entities.tsv if present */
    entities_load_from_file("entities.tsv");

    /* Fallback to built-in common entities */
    if (g_entities_count == 0) {
        entities_add("amp", "&");
        entities_add("lt", "<");
        entities_add("gt", ">");
        entities_add("quot", "\"");
        entities_add("apos", "'");
        entities_add("nbsp", "\xC2\xA0");
        entities_add("copy", "\xC2\xA9");
        entities_add("reg", "\xC2\xAE");
        entities_add("trade", "\xE2\x84\xA2");
        entities_add("cent", "\xC2\xA2");
        entities_add("pound", "\xC2\xA3");
        entities_add("yen", "\xC2\xA5");
        entities_add("euro", "\xE2\x82\xAC");
        entities_add("deg", "\xC2\xB0");
        entities_add("plusmn", "\xC2\xB1");
        entities_add("times", "\xC3\x97");
        entities_add("divide", "\xC3\xB7");
        entities_add("ndash", "\xE2\x80\x93");
        entities_add("mdash", "\xE2\x80\x94");
        entities_add("hellip", "\xE2\x80\xA6");
        entities_add("middot", "\xC2\xB7");
        entities_add("laquo", "\xC2\xAB");
        entities_add("raquo", "\xC2\xBB");
        entities_add("lsquo", "\xE2\x80\x98");
        entities_add("rsquo", "\xE2\x80\x99");
        entities_add("ldquo", "\xE2\x80\x9C");
        entities_add("rdquo", "\xE2\x80\x9D");
        entities_add("sect", "\xC2\xA7");
        entities_add("para", "\xC2\xB6");
        entities_add("bull", "\xE2\x80\xA2");
    }
}

static const char *match_named_entity(const char *s, size_t *consumed, int allow_missing_semicolon) {
    entities_load_once();
    for (size_t i = 0; i < g_entities_count; ++i) {
        const char *name = g_entities[i].name;
        size_t nlen = strlen(name);
        if (strncmp(s, name, nlen) == 0) {
            if (s[nlen] == ';') {
                *consumed = nlen + 1;
                return g_entities[i].value;
            }
            if (allow_missing_semicolon && !is_ascii_alnum(s[nlen]) && s[nlen] != '=') {
                *consumed = nlen;
                return g_entities[i].value;
            }
        }
    }
    return NULL;
}

static char *decode_character_references(const char *s) {
    size_t cap = strlen(s) + 1;
    size_t len = 0;
    char *out = (char *)malloc(cap);
    if (!out) return NULL;

    for (size_t i = 0; s[i] != '\0'; ) {
        if (s[i] != '&') {
            if (len + 2 > cap) {
                cap *= 2;
                out = (char *)realloc(out, cap);
                if (!out) return NULL;
            }
            out[len++] = s[i++];
            continue;
        }

        size_t j = i + 1;
        if (s[j] == '#') {
            j++;
            int is_hex = 0;
            if (s[j] == 'x' || s[j] == 'X') {
                is_hex = 1;
                j++;
            }
            unsigned int codepoint = 0;
            size_t start = j;
            while (s[j] && ((is_hex && is_hex_digit(s[j])) || (!is_hex && isdigit((unsigned char)s[j])))) {
                if (is_hex) {
                    codepoint = codepoint * 16 + (unsigned int)hex_value(s[j]);
                } else {
                    codepoint = codepoint * 10 + (unsigned int)(s[j] - '0');
                }
                j++;
            }
            if (j > start && s[j] == ';') {
                char buf[4];
                size_t n = encode_utf8(codepoint, buf);
                if (len + n + 1 > cap) {
                    cap = (cap + n + 1) * 2;
                    out = (char *)realloc(out, cap);
                    if (!out) return NULL;
                }
                for (size_t k = 0; k < n; ++k) out[len++] = buf[k];
                i = j + 1;
                continue;
            }
            if (j > start && s[j] != ';' && !is_ascii_alnum(s[j]) && s[j] != '=') {
                char buf[4];
                size_t n = encode_utf8(codepoint, buf);
                if (len + n + 1 > cap) {
                    cap = (cap + n + 1) * 2;
                    out = (char *)realloc(out, cap);
                    if (!out) return NULL;
                }
                for (size_t k = 0; k < n; ++k) out[len++] = buf[k];
                i = j;
                continue;
            }
        } else {
            size_t consumed = 0;
            const char *value = match_named_entity(s + j, &consumed, 1);
            if (value) {
                size_t vlen = strlen(value);
                if (len + vlen + 1 > cap) {
                    cap = (cap + vlen + 1) * 2;
                    out = (char *)realloc(out, cap);
                    if (!out) return NULL;
                }
                memcpy(out + len, value, vlen);
                len += vlen;
                i += 1 + consumed;
                continue;
            }
        }

        if (len + 2 > cap) {
            cap *= 2;
            out = (char *)realloc(out, cap);
            if (!out) return NULL;
        }
        out[len++] = s[i++];
    }

    out[len] = '\0';
    return out;
}

static void skip_whitespace(tokenizer *tz) {
    while (is_ascii_whitespace(peek(tz, 0))) {
        advance(tz, 1);
    }
}

static int starts_with_ci(const tokenizer *tz, const char *s) {
    size_t i = 0;
    while (s[i] != '\0') {
        char c = peek(tz, i);
        if (c == '\0') return 0;
        if (to_lower_ascii(c) != to_lower_ascii(s[i])) return 0;
        i++;
    }
    return 1;
}

static int starts_with_ci_at(const char *input, size_t pos, size_t len, const char *s) {
    size_t i = 0;
    while (s[i] != '\0') {
        if (pos + i >= len) return 0;
        char c = input[pos + i];
        if (to_lower_ascii(c) != to_lower_ascii(s[i])) return 0;
        i++;
    }
    return 1;
}

static size_t find_end_tag(const tokenizer *tz, const char *tag) {
    size_t i = tz->pos;
    size_t tag_len = strlen(tag);
    if (tag_len == 0) return tz->len;
    while (i + 2 + tag_len <= tz->len) {
        if (tz->input[i] == '<' && tz->input[i + 1] == '/') {
            if (starts_with_ci_at(tz->input, i + 2, tz->len, tag)) {
                size_t end = i + 2 + tag_len;
                if (end < tz->len && (is_ascii_whitespace(tz->input[end]) || tz->input[end] == '>')) {
                    return i;
                }
            }
        }
        i++;
    }
    return tz->len;
}

typedef enum {
    SCRIPT_MARK_NONE = 0,
    SCRIPT_MARK_ENDSCRIPT,
    SCRIPT_MARK_OPEN_COMMENT,
    SCRIPT_MARK_CLOSE_COMMENT
} script_marker_kind;

static size_t find_script_marker(const tokenizer *tz, script_marker_kind *kind) {
    size_t i = tz->pos;
    size_t best_pos = tz->len;
    script_marker_kind best_kind = SCRIPT_MARK_NONE;

    /* Always look for </script */
    size_t end_pos = find_end_tag(tz, "script");
    if (end_pos < best_pos) {
        best_pos = end_pos;
        best_kind = SCRIPT_MARK_ENDSCRIPT;
    }

    /* In escaped modes, recognize comment open/close markers */
    if (tz->state == TOKENIZE_SCRIPT_DATA || tz->state == TOKENIZE_SCRIPT_DATA_ESCAPED || tz->state == TOKENIZE_SCRIPT_DATA_DOUBLE_ESCAPED) {
        while (i + 3 < tz->len) {
            if (tz->input[i] == '<' && tz->input[i + 1] == '!' && tz->input[i + 2] == '-' && tz->input[i + 3] == '-') {
                if (i < best_pos) {
                    best_pos = i;
                    best_kind = SCRIPT_MARK_OPEN_COMMENT;
                }
                break;
            }
            i++;
        }
        i = tz->pos;
        while (i + 2 < tz->len) {
            if (tz->input[i] == '-' && tz->input[i + 1] == '-' && tz->input[i + 2] == '>') {
                if (i < best_pos) {
                    best_pos = i;
                    best_kind = SCRIPT_MARK_CLOSE_COMMENT;
                }
                break;
            }
            i++;
        }
    }

    if (kind) *kind = best_kind;
    return best_pos;
}

static void enter_raw_state(tokenizer *tz, const char *tag, tokenizer_state state) {
    if (!tz || !tag) return;
    strncpy(tz->raw_tag, tag, sizeof(tz->raw_tag) - 1);
    tz->raw_tag[sizeof(tz->raw_tag) - 1] = '\0';
    tz->state = state;
}

static void append_attr(token *out, const char *name, const char *value) {
    token_attr *next = (token_attr *)realloc(out->attrs, sizeof(token_attr) * (out->attr_count + 1));
    if (!next) return;
    out->attrs = next;
    out->attrs[out->attr_count].name = dup_string(name);
    out->attrs[out->attr_count].value = dup_string(value);
    out->attr_count++;
}

static void parse_comment(tokenizer *tz, token *out) {
    size_t start;
    size_t end;
    advance(tz, 4); /* "<!--" */
    start = tz->pos;
    while (tz->pos + 2 < tz->len) {
        if (peek(tz, 0) == '-' && peek(tz, 1) == '-' && peek(tz, 2) == '>') {
            end = tz->pos;
            out->type = TOKEN_COMMENT;
            out->data = substr_dup(tz->input, start, end);
            advance(tz, 3);
            return;
        }
        advance(tz, 1);
    }
    end = tz->len;
    if (tz->pos < tz->len) {
        advance(tz, tz->len - tz->pos);
    }
    out->type = TOKEN_COMMENT;
    out->data = substr_dup(tz->input, start, end);
}

static void parse_doctype(tokenizer *tz, token *out) {
    size_t name_start;
    size_t name_end;
    advance(tz, 2); /* "<!" */
    advance(tz, 7); /* "DOCTYPE" */
    skip_whitespace(tz);
    name_start = tz->pos;
    while (tz->pos < tz->len && !is_ascii_whitespace(peek(tz, 0)) && peek(tz, 0) != '>') {
        advance(tz, 1);
    }
    name_end = tz->pos;
    out->type = TOKEN_DOCTYPE;
    out->name = substr_dup(tz->input, name_start, name_end);
    if (out->name) {
        for (size_t i = 0; out->name[i]; ++i) out->name[i] = to_lower_ascii(out->name[i]);
    }
    if (name_end == name_start) {
        out->force_quirks = 1;
        report_error(tz, "doctype name missing");
    }
    while (tz->pos < tz->len && peek(tz, 0) != '>') {
        advance(tz, 1);
    }
    if (peek(tz, 0) == '>') advance(tz, 1);
}

static void parse_end_tag(tokenizer *tz, token *out) {
    size_t name_start;
    size_t name_end;
    advance(tz, 2); /* "</" */
    name_start = tz->pos;
    while (tz->pos < tz->len && !is_ascii_whitespace(peek(tz, 0)) && peek(tz, 0) != '>') {
        advance(tz, 1);
    }
    name_end = tz->pos;
    out->type = TOKEN_END_TAG;
    out->name = substr_dup(tz->input, name_start, name_end);
    if (out->name) {
        for (size_t i = 0; out->name[i]; ++i) out->name[i] = to_lower_ascii(out->name[i]);
    }
    if (peek(tz, 0) != '>' && tz->pos < tz->len) {
        report_error(tz, "end tag has trailing garbage/attributes");
    }
    while (tz->pos < tz->len && peek(tz, 0) != '>') {
        advance(tz, 1);
    }
    if (peek(tz, 0) == '>') advance(tz, 1);
}

static void parse_start_tag(tokenizer *tz, token *out) {
    enum {
        ST_TAG_OPEN = 0,
        ST_TAG_NAME,
        ST_BEFORE_ATTR_NAME,
        ST_ATTR_NAME,
        ST_AFTER_ATTR_NAME,
        ST_BEFORE_ATTR_VALUE,
        ST_ATTR_VALUE_DQ,
        ST_ATTR_VALUE_SQ,
        ST_ATTR_VALUE_UQ,
        ST_SELF_CLOSING
    } state = ST_TAG_OPEN;

    strbuf tag_name;
    strbuf attr_name;
    strbuf attr_value;
    char c;

    sb_init(&tag_name);
    sb_init(&attr_name);
    sb_init(&attr_value);

    out->type = TOKEN_START_TAG;
    advance(tz, 1); /* '<' */
    state = ST_TAG_NAME;

    while (tz->pos <= tz->len) {
        c = peek(tz, 0);
        switch (state) {
            case ST_TAG_OPEN:
                state = ST_TAG_NAME;
                break;
            case ST_TAG_NAME:
                if (is_ascii_whitespace(c)) {
                    state = ST_BEFORE_ATTR_NAME;
                    advance(tz, 1);
                } else if (c == '/') {
                    state = ST_SELF_CLOSING;
                    advance(tz, 1);
                } else if (c == '>') {
                    advance(tz, 1);
                    goto done;
                } else if (c == '\0') {
                    goto done;
                } else {
                    sb_push_char(&tag_name, to_lower_ascii(c));
                    advance(tz, 1);
                }
                break;
            case ST_BEFORE_ATTR_NAME:
                if (is_ascii_whitespace(c)) {
                    advance(tz, 1);
                } else if (c == '/') {
                    state = ST_SELF_CLOSING;
                    advance(tz, 1);
                } else if (c == '>') {
                    advance(tz, 1);
                    goto done;
                } else if (c == '=') {
                    report_error(tz, "attribute name missing before '='");
                    advance(tz, 1);
                } else {
                    sb_free(&attr_name);
                    sb_free(&attr_value);
                    sb_init(&attr_name);
                    sb_init(&attr_value);
                    state = ST_ATTR_NAME;
                }
                break;
            case ST_ATTR_NAME:
                if (is_ascii_whitespace(c)) {
                    state = ST_AFTER_ATTR_NAME;
                    advance(tz, 1);
                } else if (c == '=') {
                    state = ST_BEFORE_ATTR_VALUE;
                    advance(tz, 1);
                } else if (c == '/' || c == '>' || c == '\0') {
                    char *an = sb_to_string(&attr_name);
                    append_attr(out, an ? an : "", "");
                    free(an);
                    if (c == '/') {
                        state = ST_SELF_CLOSING;
                        advance(tz, 1);
                    } else if (c == '>') {
                        advance(tz, 1);
                        goto done;
                    } else {
                        goto done;
                    }
                } else {
                    if (!is_attr_name_char(c)) {
                        report_error(tz, "unexpected character in attribute name");
                    }
                    sb_push_char(&attr_name, to_lower_ascii(c));
                    advance(tz, 1);
                }
                break;
            case ST_AFTER_ATTR_NAME:
                if (is_ascii_whitespace(c)) {
                    advance(tz, 1);
                } else if (c == '=') {
                    state = ST_BEFORE_ATTR_VALUE;
                    advance(tz, 1);
                } else if (c == '>') {
                    char *an = sb_to_string(&attr_name);
                    append_attr(out, an ? an : "", "");
                    free(an);
                    advance(tz, 1);
                    goto done;
                } else if (c == '/') {
                    char *an = sb_to_string(&attr_name);
                    append_attr(out, an ? an : "", "");
                    free(an);
                    state = ST_SELF_CLOSING;
                    advance(tz, 1);
                } else {
                    char *an = sb_to_string(&attr_name);
                    append_attr(out, an ? an : "", "");
                    free(an);
                    state = ST_ATTR_NAME;
                }
                break;
            case ST_BEFORE_ATTR_VALUE:
                if (is_ascii_whitespace(c)) {
                    advance(tz, 1);
                } else if (c == '"') {
                    state = ST_ATTR_VALUE_DQ;
                    advance(tz, 1);
                } else if (c == '\'') {
                    state = ST_ATTR_VALUE_SQ;
                    advance(tz, 1);
                } else if (c == '>') {
                    report_error(tz, "attribute value missing");
                    char *an = sb_to_string(&attr_name);
                    append_attr(out, an ? an : "", "");
                    free(an);
                    advance(tz, 1);
                    goto done;
                } else {
                    state = ST_ATTR_VALUE_UQ;
                }
                break;
            case ST_ATTR_VALUE_DQ:
                if (c == '"') {
                    char *av = sb_to_string(&attr_value);
                    char *decoded = decode_character_references(av);
                    char *an = sb_to_string(&attr_name);
                    append_attr(out, an ? an : "", decoded ? decoded : "");
                    free(an);
                    free(av);
                    free(decoded);
                    state = ST_BEFORE_ATTR_NAME;
                    advance(tz, 1);
                } else if (c == '\0') {
                    goto done;
                } else {
                    sb_push_char(&attr_value, c);
                    advance(tz, 1);
                }
                break;
            case ST_ATTR_VALUE_SQ:
                if (c == '\'') {
                    char *av = sb_to_string(&attr_value);
                    char *decoded = decode_character_references(av);
                    char *an = sb_to_string(&attr_name);
                    append_attr(out, an ? an : "", decoded ? decoded : "");
                    free(an);
                    free(av);
                    free(decoded);
                    state = ST_BEFORE_ATTR_NAME;
                    advance(tz, 1);
                } else if (c == '\0') {
                    goto done;
                } else {
                    sb_push_char(&attr_value, c);
                    advance(tz, 1);
                }
                break;
            case ST_ATTR_VALUE_UQ:
                if (is_ascii_whitespace(c)) {
                    char *av = sb_to_string(&attr_value);
                    char *decoded = decode_character_references(av);
                    char *an = sb_to_string(&attr_name);
                    append_attr(out, an ? an : "", decoded ? decoded : "");
                    free(an);
                    free(av);
                    free(decoded);
                    state = ST_BEFORE_ATTR_NAME;
                    advance(tz, 1);
                } else if (c == '>') {
                    char *av = sb_to_string(&attr_value);
                    char *decoded = decode_character_references(av);
                    char *an = sb_to_string(&attr_name);
                    append_attr(out, an ? an : "", decoded ? decoded : "");
                    free(an);
                    free(av);
                    free(decoded);
                    advance(tz, 1);
                    goto done;
                } else if (c == '\0') {
                    goto done;
                } else {
                    sb_push_char(&attr_value, c);
                    advance(tz, 1);
                }
                break;
            case ST_SELF_CLOSING:
                if (c == '>') {
                    out->self_closing = 1;
                    advance(tz, 1);
                } else {
                    report_error(tz, "unexpected '/' in start tag");
                }
                goto done;
        }
    }

done:
    out->name = sb_to_string(&tag_name);
    sb_free(&tag_name);
    sb_free(&attr_name);
    sb_free(&attr_value);

    if (out->name && out->name[0] == '\0') {
        report_error(tz, "tag name missing");
    }

    if (out->name) {
        if (strcmp(out->name, "title") == 0 || strcmp(out->name, "textarea") == 0) {
            enter_raw_state(tz, out->name, TOKENIZE_RCDATA);
        } else if (strcmp(out->name, "script") == 0) {
            enter_raw_state(tz, out->name, TOKENIZE_SCRIPT_DATA);
        } else if (strcmp(out->name, "style") == 0) {
            enter_raw_state(tz, out->name, TOKENIZE_RAWTEXT);
        }
    }
}

void tokenizer_init(tokenizer *tz, const char *input) {
    if (!tz) return;
    tz->input = input ? input : "";
    tz->pos = 0;
    tz->len = strlen(tz->input);
    tz->line = 1;
    tz->col = 1;
    tz->state = TOKENIZE_DATA;
    tz->raw_tag[0] = '\0';
}

void tokenizer_next(tokenizer *tz, token *out) {
    char c;
    if (!tz || !out) return;
    token_init(out);

    if (tz->pos >= tz->len) {
        out->type = TOKEN_EOF;
        return;
    }

    while (tz->state != TOKENIZE_DATA) {
        if (tz->state == TOKENIZE_SCRIPT_DATA || tz->state == TOKENIZE_SCRIPT_DATA_ESCAPED || tz->state == TOKENIZE_SCRIPT_DATA_DOUBLE_ESCAPED) {
            script_marker_kind mk = SCRIPT_MARK_NONE;
            size_t pos = find_script_marker(tz, &mk);
            if (pos > tz->pos) {
                out->type = TOKEN_CHARACTER;
                out->data = substr_dup(tz->input, tz->pos, pos);
                advance(tz, pos - tz->pos);
                return;
            }
            if (mk == SCRIPT_MARK_ENDSCRIPT && pos == tz->pos) {
                tz->state = TOKENIZE_DATA;
                continue;
            }
            if (mk == SCRIPT_MARK_OPEN_COMMENT && pos == tz->pos) {
                out->type = TOKEN_CHARACTER;
                out->data = substr_dup(tz->input, tz->pos, tz->pos + 4);
                advance(tz, 4);
                if (tz->state == TOKENIZE_SCRIPT_DATA) {
                    tz->state = TOKENIZE_SCRIPT_DATA_ESCAPED;
                } else if (tz->state == TOKENIZE_SCRIPT_DATA_ESCAPED) {
                    tz->state = TOKENIZE_SCRIPT_DATA_DOUBLE_ESCAPED;
                }
                return;
            }
            if (mk == SCRIPT_MARK_CLOSE_COMMENT && pos == tz->pos) {
                out->type = TOKEN_CHARACTER;
                out->data = substr_dup(tz->input, tz->pos, tz->pos + 3);
                advance(tz, 3);
                if (tz->state == TOKENIZE_SCRIPT_DATA_DOUBLE_ESCAPED) {
                    tz->state = TOKENIZE_SCRIPT_DATA_ESCAPED;
                } else {
                    tz->state = TOKENIZE_SCRIPT_DATA;
                }
                return;
            }
            /* fallback: emit remaining */
            out->type = TOKEN_CHARACTER;
            out->data = substr_dup(tz->input, tz->pos, tz->len);
            advance(tz, tz->len - tz->pos);
            tz->state = TOKENIZE_DATA;
            return;
        }

        /* RCDATA/RAWTEXT */
        size_t end = find_end_tag(tz, tz->raw_tag);
        out->type = TOKEN_CHARACTER;
        out->data = substr_dup(tz->input, tz->pos, end);
        if (out->data && tz->state == TOKENIZE_RCDATA) {
            char *decoded = decode_character_references(out->data);
            if (decoded) {
                free(out->data);
                out->data = decoded;
            }
        }
        if (end < tz->len) {
            advance(tz, end - tz->pos);
        } else {
            advance(tz, tz->len - tz->pos);
            tz->state = TOKENIZE_DATA;
        }
        if (end < tz->len) {
            tz->state = TOKENIZE_DATA;
        }
        return;
    }

    c = peek(tz, 0);
    if (c == '<') {
        char next = peek(tz, 1);
        if (next == '/' && is_tag_start(peek(tz, 2))) {
            parse_end_tag(tz, out);
            return;
        }
        if (next == '/' && !is_tag_start(peek(tz, 2))) {
            report_error(tz, "invalid end tag");
            out->type = TOKEN_CHARACTER;
            out->data = dup_string("<");
            advance(tz, 1);
            return;
        }
        if (next == '!' && peek(tz, 2) == '-' && peek(tz, 3) == '-') {
            parse_comment(tz, out);
            return;
        }
        if (next == '!' && starts_with_ci(tz, "<!DOCTYPE")) {
            parse_doctype(tz, out);
            return;
        }
        if (next == '!') {
            /* Bogus comment: consume until '>' */
            report_error(tz, "bogus markup declaration");
            advance(tz, 2);
            size_t start = tz->pos;
            while (tz->pos < tz->len && peek(tz, 0) != '>') {
                advance(tz, 1);
            }
            out->type = TOKEN_COMMENT;
            out->data = substr_dup(tz->input, start, tz->pos);
            if (peek(tz, 0) == '>') advance(tz, 1);
            return;
        }
        if (is_tag_start(next)) {
            parse_start_tag(tz, out);
            return;
        }
        /* Not a valid tag start; emit literal '<' */
        out->type = TOKEN_CHARACTER;
        out->data = dup_string("<");
        advance(tz, 1);
        return;
    }

    /* Character data */
    size_t start = tz->pos;
    while (tz->pos < tz->len && peek(tz, 0) != '<') {
        advance(tz, 1);
    }
    out->type = TOKEN_CHARACTER;
    out->data = substr_dup(tz->input, start, tz->pos);
    if (out->data) {
        char *decoded = decode_character_references(out->data);
        if (decoded) {
            free(out->data);
            out->data = decoded;
        }
    }
}
