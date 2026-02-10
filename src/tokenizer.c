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

/* WHATWG numeric character reference replacement table (Windows-1252 control area) */
static unsigned int numeric_ref_adjust(unsigned int cp) {
    if (cp == 0x00) return 0xFFFD;
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0xFFFD;

    switch (cp) {
        case 0x80: return 0x20AC;
        case 0x82: return 0x201A;
        case 0x83: return 0x0192;
        case 0x84: return 0x201E;
        case 0x85: return 0x2026;
        case 0x86: return 0x2020;
        case 0x87: return 0x2021;
        case 0x88: return 0x02C6;
        case 0x89: return 0x2030;
        case 0x8A: return 0x0160;
        case 0x8B: return 0x2039;
        case 0x8C: return 0x0152;
        case 0x8E: return 0x017D;
        case 0x91: return 0x2018;
        case 0x92: return 0x2019;
        case 0x93: return 0x201C;
        case 0x94: return 0x201D;
        case 0x95: return 0x2022;
        case 0x96: return 0x2013;
        case 0x97: return 0x2014;
        case 0x98: return 0x02DC;
        case 0x99: return 0x2122;
        case 0x9A: return 0x0161;
        case 0x9B: return 0x203A;
        case 0x9C: return 0x0153;
        case 0x9E: return 0x017E;
        case 0x9F: return 0x0178;
        default: break;
    }

    if ((cp >= 0x01 && cp <= 0x08) ||
        (cp >= 0x0E && cp <= 0x1F) ||
        (cp >= 0x7F && cp <= 0x9F)) {
        return 0xFFFD;
    }

    return cp;
}

typedef struct {
    char *name;
    char *value;
    int legacy;  /* 1 = this entity has a no-semicolon form (WHATWG legacy table) */
} named_entity;

static int is_ascii_alnum(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z');
}

static named_entity *g_entities = NULL;
static size_t g_entities_count = 0;
static int g_entities_loaded = 0;

static void entities_cleanup(void) {
    for (size_t i = 0; i < g_entities_count; ++i) {
        free(g_entities[i].name);
        free(g_entities[i].value);
    }
    free(g_entities);
    g_entities = NULL;
    g_entities_count = 0;
    g_entities_loaded = 0;
}

static void entities_add(const char *name, const char *value, int legacy) {
    /* Dedup: if name already exists, mark as legacy and skip */
    for (size_t i = 0; i < g_entities_count; ++i) {
        if (g_entities[i].name && strcmp(g_entities[i].name, name) == 0) {
            g_entities[i].legacy = 1;
            return;
        }
    }
    named_entity *next = (named_entity *)realloc(g_entities, sizeof(named_entity) * (g_entities_count + 1));
    if (!next) return;
    g_entities = next;
    g_entities[g_entities_count].name = dup_string(name);
    g_entities[g_entities_count].value = dup_string(value);
    g_entities[g_entities_count].legacy = legacy;
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
        entities_add(name, value, 0);
    }
    fclose(fp);
}

static void entities_load_once(void) {
    if (g_entities_loaded) return;
    g_entities_loaded = 1;
    atexit(entities_cleanup);

    /* Try to load full list from entities.tsv if present */
    entities_load_from_file("entities.tsv");

    /* Fallback to built-in common entities */
    if (g_entities_count == 0) {
        /* legacy=1: these entities have a no-semicolon form in WHATWG */
        entities_add("amp", "&", 1);
        entities_add("lt", "<", 1);
        entities_add("gt", ">", 1);
        entities_add("quot", "\"", 1);
        entities_add("apos", "'", 0);
        entities_add("nbsp", "\xC2\xA0", 1);
        entities_add("copy", "\xC2\xA9", 1);
        entities_add("reg", "\xC2\xAE", 1);
        entities_add("trade", "\xE2\x84\xA2", 0);
        entities_add("cent", "\xC2\xA2", 1);
        entities_add("pound", "\xC2\xA3", 1);
        entities_add("yen", "\xC2\xA5", 1);
        entities_add("euro", "\xE2\x82\xAC", 0);
        entities_add("deg", "\xC2\xB0", 1);
        entities_add("plusmn", "\xC2\xB1", 1);
        entities_add("times", "\xC3\x97", 1);
        entities_add("divide", "\xC3\xB7", 1);
        entities_add("ndash", "\xE2\x80\x93", 0);
        entities_add("mdash", "\xE2\x80\x94", 0);
        entities_add("hellip", "\xE2\x80\xA6", 0);
        entities_add("middot", "\xC2\xB7", 1);
        entities_add("laquo", "\xC2\xAB", 1);
        entities_add("raquo", "\xC2\xBB", 1);
        entities_add("lsquo", "\xE2\x80\x98", 0);
        entities_add("rsquo", "\xE2\x80\x99", 0);
        entities_add("ldquo", "\xE2\x80\x9C", 0);
        entities_add("rdquo", "\xE2\x80\x9D", 0);
        entities_add("sect", "\xC2\xA7", 1);
        entities_add("para", "\xC2\xB6", 1);
        entities_add("bull", "\xE2\x80\xA2", 0);
    }
}

static const char *match_named_entity(const char *s, size_t *consumed, int in_attribute) {
    entities_load_once();
    const char *best_value = NULL;
    size_t best_consumed = 0;
    for (size_t i = 0; i < g_entities_count; ++i) {
        const char *name = g_entities[i].name;
        size_t nlen = strlen(name);
        if (strncmp(s, name, nlen) != 0) continue;
        char after = s[nlen];
        if (after == ';') {
            /* With semicolon: always match; prefer longest match */
            if (nlen + 1 > best_consumed) {
                best_consumed = nlen + 1;
                best_value = g_entities[i].value;
            }
            continue;
        }
        /* Without semicolon: only legacy entities can match */
        if (!g_entities[i].legacy) continue;
        if (in_attribute) {
            /* Attribute context: '=' or alnum after entity name → no match */
            if (after == '=' || is_ascii_alnum(after)) continue;
        } else {
            /* Text context: alnum after → no match (name may continue) */
            if (is_ascii_alnum(after)) continue;
        }
        /* Match without semicolon (parse error, handled by caller) */
        if (nlen > best_consumed) {
            best_consumed = nlen;
            best_value = g_entities[i].value;
        }
    }
    if (best_value) {
        *consumed = best_consumed;
    }
    return best_value;
}

static char *decode_character_references(const char *s, int in_attribute) {
    size_t cap = strlen(s) + 1;
    size_t len = 0;
    char *out = (char *)malloc(cap);
    if (!out) return NULL;

    for (size_t i = 0; s[i] != '\0'; ) {
        if (s[i] != '&') {
            if (len + 2 > cap) {
                cap *= 2;
                char *tmp = (char *)realloc(out, cap);
                if (!tmp) { free(out); return NULL; }
                out = tmp;
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
                codepoint = numeric_ref_adjust(codepoint);
                char buf[4];
                size_t n = encode_utf8(codepoint, buf);
                if (len + n + 1 > cap) {
                    cap = (cap + n + 1) * 2;
                    char *tmp = (char *)realloc(out, cap);
                    if (!tmp) { free(out); return NULL; }
                    out = tmp;
                }
                for (size_t k = 0; k < n; ++k) out[len++] = buf[k];
                i = j + 1;
                continue;
            }
            if (j > start && s[j] != ';') {
                codepoint = numeric_ref_adjust(codepoint);
                char buf[4];
                size_t n = encode_utf8(codepoint, buf);
                if (len + n + 1 > cap) {
                    cap = (cap + n + 1) * 2;
                    char *tmp = (char *)realloc(out, cap);
                    if (!tmp) { free(out); return NULL; }
                    out = tmp;
                }
                for (size_t k = 0; k < n; ++k) out[len++] = buf[k];
                i = j;
                continue;
            }
        } else {
            size_t consumed = 0;
            const char *value = match_named_entity(s + j, &consumed, in_attribute);
            if (value) {
                size_t vlen = strlen(value);
                if (len + vlen + 1 > cap) {
                    cap = (cap + vlen + 1) * 2;
                    char *tmp = (char *)realloc(out, cap);
                    if (!tmp) { free(out); return NULL; }
                    out = tmp;
                }
                memcpy(out + len, value, vlen);
                len += vlen;
                i += 1 + consumed;
                continue;
            }
        }

        if (len + 2 > cap) {
            cap *= 2;
            char *tmp = (char *)realloc(out, cap);
            if (!tmp) { free(out); return NULL; }
            out = tmp;
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
    /* WHATWG: duplicate attribute name is a parse error; drop the new one */
    for (size_t i = 0; i < out->attr_count; ++i) {
        if (out->attrs[i].name && strcmp(out->attrs[i].name, name) == 0) {
            return;
        }
    }
    token_attr *next = (token_attr *)realloc(out->attrs, sizeof(token_attr) * (out->attr_count + 1));
    if (!next) return;
    out->attrs = next;
    out->attrs[out->attr_count].name = dup_string(name);
    out->attrs[out->attr_count].value = dup_string(value);
    out->attr_count++;
}

static void parse_comment(tokenizer *tz, token *out) {
    /*
     * WHATWG HTML Standard §13.2.5 Comment tokenization
     * Handles edge cases: <!-->  <!--->  <!----->  <!---->  etc.
     */
    typedef enum {
        CS_COMMENT_START,
        CS_COMMENT_START_DASH,
        CS_COMMENT,
        CS_COMMENT_LESS_THAN_SIGN,
        CS_COMMENT_LESS_THAN_SIGN_BANG,
        CS_COMMENT_LESS_THAN_SIGN_BANG_DASH,
        CS_COMMENT_LESS_THAN_SIGN_BANG_DASH_DASH,
        CS_COMMENT_END_DASH,
        CS_COMMENT_END,
        CS_COMMENT_END_BANG
    } comment_state;

    strbuf data;
    sb_init(&data);
    comment_state state = CS_COMMENT_START;
    char c;

    advance(tz, 4); /* "<!--" */
    out->type = TOKEN_COMMENT;

    while (tz->pos <= tz->len) {
        c = peek(tz, 0);

        switch (state) {
            case CS_COMMENT_START:
                if (c == '-') {
                    state = CS_COMMENT_START_DASH;
                    advance(tz, 1);
                } else if (c == '>') {
                    /* <!-->  — abrupt-closing-of-empty-comment parse error */
                    report_error(tz, "abrupt-closing-of-empty-comment");
                    advance(tz, 1);
                    goto emit;
                } else if (c == '\0') {
                    /* EOF — eof-in-comment parse error */
                    report_error(tz, "eof-in-comment");
                    goto emit;
                } else {
                    /* Reconsume in COMMENT state */
                    state = CS_COMMENT;
                }
                break;

            case CS_COMMENT_START_DASH:
                if (c == '-') {
                    /* Seen "<!---", next char decides */
                    state = CS_COMMENT_END;
                    advance(tz, 1);
                } else if (c == '>') {
                    /* <!--->  — abrupt-closing-of-empty-comment parse error */
                    report_error(tz, "abrupt-closing-of-empty-comment");
                    advance(tz, 1);
                    goto emit;
                } else if (c == '\0') {
                    /* EOF — eof-in-comment parse error */
                    report_error(tz, "eof-in-comment");
                    sb_push_char(&data, '-');
                    goto emit;
                } else {
                    /* Append '-' from COMMENT_START_DASH, reconsume in COMMENT */
                    sb_push_char(&data, '-');
                    state = CS_COMMENT;
                }
                break;

            case CS_COMMENT:
                if (c == '<') {
                    sb_push_char(&data, c);
                    state = CS_COMMENT_LESS_THAN_SIGN;
                    advance(tz, 1);
                } else if (c == '-') {
                    state = CS_COMMENT_END_DASH;
                    advance(tz, 1);
                } else if (c == '\0') {
                    /* EOF — eof-in-comment parse error */
                    report_error(tz, "eof-in-comment");
                    goto emit;
                } else {
                    sb_push_char(&data, c);
                    advance(tz, 1);
                }
                break;

            case CS_COMMENT_LESS_THAN_SIGN:
                if (c == '!') {
                    sb_push_char(&data, c);
                    state = CS_COMMENT_LESS_THAN_SIGN_BANG;
                    advance(tz, 1);
                } else if (c == '<') {
                    sb_push_char(&data, c);
                    advance(tz, 1);
                    /* Stay in CS_COMMENT_LESS_THAN_SIGN */
                } else {
                    state = CS_COMMENT;
                }
                break;

            case CS_COMMENT_LESS_THAN_SIGN_BANG:
                if (c == '-') {
                    state = CS_COMMENT_LESS_THAN_SIGN_BANG_DASH;
                    advance(tz, 1);
                } else {
                    state = CS_COMMENT;
                }
                break;

            case CS_COMMENT_LESS_THAN_SIGN_BANG_DASH:
                if (c == '-') {
                    state = CS_COMMENT_LESS_THAN_SIGN_BANG_DASH_DASH;
                    advance(tz, 1);
                } else {
                    state = CS_COMMENT_END_DASH;
                }
                break;

            case CS_COMMENT_LESS_THAN_SIGN_BANG_DASH_DASH:
                if (c == '>' || c == '\0') {
                    state = CS_COMMENT_END;
                } else {
                    /* nested-comment parse error */
                    report_error(tz, "nested-comment");
                    state = CS_COMMENT_END;
                }
                break;

            case CS_COMMENT_END_DASH:
                if (c == '-') {
                    state = CS_COMMENT_END;
                    advance(tz, 1);
                } else if (c == '\0') {
                    /* EOF — eof-in-comment parse error */
                    report_error(tz, "eof-in-comment");
                    sb_push_char(&data, '-');
                    goto emit;
                } else {
                    /* Append '-' and reconsume in COMMENT */
                    sb_push_char(&data, '-');
                    state = CS_COMMENT;
                }
                break;

            case CS_COMMENT_END:
                if (c == '>') {
                    /* Normal end: --> */
                    advance(tz, 1);
                    goto emit;
                } else if (c == '!') {
                    state = CS_COMMENT_END_BANG;
                    advance(tz, 1);
                } else if (c == '-') {
                    /* Extra '-' in "---" sequence: append one '-' to data */
                    sb_push_char(&data, '-');
                    advance(tz, 1);
                    /* Stay in CS_COMMENT_END */
                } else if (c == '\0') {
                    /* EOF — eof-in-comment parse error */
                    report_error(tz, "eof-in-comment");
                    sb_push_char(&data, '-');
                    sb_push_char(&data, '-');
                    goto emit;
                } else {
                    /* "--" not followed by '>': append "--" to data, reconsume */
                    sb_push_char(&data, '-');
                    sb_push_char(&data, '-');
                    state = CS_COMMENT;
                }
                break;

            case CS_COMMENT_END_BANG:
                if (c == '-') {
                    /* "--!-" pattern: append "--!" to data */
                    sb_push_char(&data, '-');
                    sb_push_char(&data, '-');
                    sb_push_char(&data, '!');
                    state = CS_COMMENT_END_DASH;
                    advance(tz, 1);
                } else if (c == '>') {
                    /* "--!>" — incorrectly-closed-comment parse error, but still emit */
                    report_error(tz, "incorrectly-closed-comment");
                    advance(tz, 1);
                    goto emit;
                } else if (c == '\0') {
                    /* EOF — eof-in-comment parse error */
                    report_error(tz, "eof-in-comment");
                    sb_push_char(&data, '-');
                    sb_push_char(&data, '-');
                    sb_push_char(&data, '!');
                    goto emit;
                } else {
                    /* "--!X" — append "--!" to data, reconsume in COMMENT */
                    sb_push_char(&data, '-');
                    sb_push_char(&data, '-');
                    sb_push_char(&data, '!');
                    state = CS_COMMENT;
                }
                break;
        }
    }

emit:
    out->data = sb_to_string(&data);
    sb_free(&data);
}

static void parse_doctype(tokenizer *tz, token *out) {
    size_t name_start;
    size_t name_end;
    char *public_id = NULL;
    char *system_id = NULL;
    int ok = 1;
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
    skip_whitespace(tz);
    if (starts_with_ci(tz, "public")) {
        advance(tz, 6);
        skip_whitespace(tz);
        char quote = peek(tz, 0);
        if (quote != '"' && quote != '\'') {
            out->force_quirks = 1;
            report_error(tz, "doctype public id missing");
            ok = 0;
        } else {
            advance(tz, 1);
            size_t start = tz->pos;
            while (tz->pos < tz->len && peek(tz, 0) != quote) {
                advance(tz, 1);
            }
            public_id = substr_dup(tz->input, start, tz->pos);
            if (peek(tz, 0) == quote) advance(tz, 1);
            else {
                out->force_quirks = 1;
                report_error(tz, "doctype public id missing closing quote");
                ok = 0;
            }
        }
        skip_whitespace(tz);
        if (peek(tz, 0) == '"' || peek(tz, 0) == '\'') {
            char quote2 = peek(tz, 0);
            advance(tz, 1);
            size_t start2 = tz->pos;
            while (tz->pos < tz->len && peek(tz, 0) != quote2) {
                advance(tz, 1);
            }
            system_id = substr_dup(tz->input, start2, tz->pos);
            if (peek(tz, 0) == quote2) advance(tz, 1);
            else {
                out->force_quirks = 1;
                report_error(tz, "doctype system id missing closing quote");
                ok = 0;
            }
        }
    } else if (starts_with_ci(tz, "system")) {
        advance(tz, 6);
        skip_whitespace(tz);
        char quote = peek(tz, 0);
        if (quote != '"' && quote != '\'') {
            out->force_quirks = 1;
            report_error(tz, "doctype system id missing");
            ok = 0;
        } else {
            advance(tz, 1);
            size_t start = tz->pos;
            while (tz->pos < tz->len && peek(tz, 0) != quote) {
                advance(tz, 1);
            }
            system_id = substr_dup(tz->input, start, tz->pos);
            if (peek(tz, 0) == quote) advance(tz, 1);
            else {
                out->force_quirks = 1;
                report_error(tz, "doctype system id missing closing quote");
                ok = 0;
            }
        }
    }
    if (!ok) {
        out->force_quirks = 1;
    }
    if (public_id) {
        for (size_t i = 0; public_id[i]; ++i) public_id[i] = to_lower_ascii(public_id[i]);
    }
    if (system_id) {
        for (size_t i = 0; system_id[i]; ++i) system_id[i] = to_lower_ascii(system_id[i]);
    }
    out->public_id = public_id;
    out->system_id = system_id;
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
                    char *decoded = decode_character_references(av, 1);
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
                    char *decoded = decode_character_references(av, 1);
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
                    char *decoded = decode_character_references(av, 1);
                    char *an = sb_to_string(&attr_name);
                    append_attr(out, an ? an : "", decoded ? decoded : "");
                    free(an);
                    free(av);
                    free(decoded);
                    state = ST_BEFORE_ATTR_NAME;
                    advance(tz, 1);
                } else if (c == '>') {
                    char *av = sb_to_string(&attr_value);
                    char *decoded = decode_character_references(av, 1);
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

char *tokenizer_replace_nulls(const char *raw, size_t raw_len) {
    if (!raw || raw_len == 0) return dup_string("");

    /* First pass: compute output length after NULL replacement and CR/LF normalization */
    size_t out_len = 0;
    size_t null_count = 0;
    for (size_t i = 0; i < raw_len; i++) {
        unsigned char c = (unsigned char)raw[i];
        if (c == '\0') {
            null_count++;
            out_len += 3; /* U+FFFD UTF-8 */
        } else if (c == '\r') {
            /* CRLF collapses to single LF */
            if (i + 1 < raw_len && raw[i + 1] == '\n') {
                i++; /* skip LF */
            }
            out_len += 1; /* normalized LF */
        } else {
            out_len += 1;
        }
    }

    if (null_count == 0 && out_len == raw_len) {
        /* No changes needed */
        char *out = (char *)malloc(raw_len + 1);
        if (!out) return NULL;
        memcpy(out, raw, raw_len);
        out[raw_len] = '\0';
        return out;
    }

    char *out = (char *)malloc(out_len + 1);
    if (!out) return NULL;

    size_t j = 0;
    size_t line = 1, col = 1;
    for (size_t i = 0; i < raw_len; i++) {
        unsigned char c = (unsigned char)raw[i];
        if (c == '\0') {
            printf("[parse error] line=%zu col=%zu: unexpected null character\n", line, col);
            out[j++] = (char)0xEF;
            out[j++] = (char)0xBF;
            out[j++] = (char)0xBD;
            col++;
            continue;
        }
        if (c == '\r') {
            /* Normalize CR and CRLF to LF */
            if (i + 1 < raw_len && raw[i + 1] == '\n') {
                i++; /* consume LF partner */
            }
            out[j++] = '\n';
            line++;
            col = 1;
            continue;
        }
        out[j++] = (char)c;
        if (c == '\n') {
            line++;
            col = 1;
        } else {
            col++;
        }
    }
    out[j] = '\0';
    return out;
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
    tz->allow_cdata = 0;
}

static void set_raw_state(tokenizer *tz, const char *tag, tokenizer_state state) {
    if (!tz || !tag) return;
    strncpy(tz->raw_tag, tag, sizeof(tz->raw_tag) - 1);
    tz->raw_tag[sizeof(tz->raw_tag) - 1] = '\0';
    tz->state = state;
}

void tokenizer_init_with_context(tokenizer *tz, const char *input, const char *context_tag) {
    tokenizer_init(tz, input);
    if (!tz || !context_tag) return;
    char lowered[32];
    size_t i = 0;
    while (context_tag[i] && i < sizeof(lowered) - 1) {
        lowered[i] = to_lower_ascii(context_tag[i]);
        i++;
    }
    lowered[i] = '\0';
    if (strcmp(lowered, "title") == 0 || strcmp(lowered, "textarea") == 0) {
        set_raw_state(tz, lowered, TOKENIZE_RCDATA);
    } else if (strcmp(lowered, "script") == 0) {
        set_raw_state(tz, lowered, TOKENIZE_SCRIPT_DATA);
    } else if (strcmp(lowered, "style") == 0) {
        set_raw_state(tz, lowered, TOKENIZE_RAWTEXT);
    }
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
            char *decoded = decode_character_references(out->data, 0);
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
        if (next == '!' && tz->allow_cdata &&
            peek(tz, 2) == '[' && peek(tz, 3) == 'C' && peek(tz, 4) == 'D' &&
            peek(tz, 5) == 'A' && peek(tz, 6) == 'T' && peek(tz, 7) == 'A' &&
            peek(tz, 8) == '[') {
            advance(tz, 9); /* skip <![CDATA[ */
            size_t start = tz->pos;
            while (tz->pos + 2 < tz->len) {
                if (tz->input[tz->pos] == ']' && tz->input[tz->pos+1] == ']' &&
                    tz->input[tz->pos+2] == '>') {
                    out->type = TOKEN_CHARACTER;
                    out->data = substr_dup(tz->input, start, tz->pos);
                    advance(tz, 3); /* skip ]]> */
                    return;
                }
                advance(tz, 1);
            }
            /* Unclosed CDATA: emit remaining as character */
            out->type = TOKEN_CHARACTER;
            out->data = substr_dup(tz->input, start, tz->len);
            tz->pos = tz->len;
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
        char *decoded = decode_character_references(out->data, 0);
        if (decoded) {
            free(out->data);
            out->data = decoded;
        }
    }
}
