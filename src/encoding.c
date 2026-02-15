#include "encoding.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_ICONV
#include <iconv.h>
#endif

#include "jis0208_table.h"

/* ========================================================================
 * Encoding label lookup table (WHATWG Encoding Standard)
 * Sorted by label for bsearch(). ~230 entries covering all 39 encodings.
 * ======================================================================== */

typedef struct {
    const char *label;      /* lowercase, trimmed */
    const char *canonical;  /* WHATWG canonical name */
    const char *iconv_name; /* iconv-compatible name */
} encoding_entry;

static const encoding_entry encoding_table[] = {
    {"866", "IBM866", "IBM866"},
    {"ansi_x3.4-1968", "windows-1252", "WINDOWS-1252"},
    {"arabic", "ISO-8859-6", "ISO-8859-6"},
    {"ascii", "windows-1252", "WINDOWS-1252"},
    {"asmo-708", "ISO-8859-6", "ISO-8859-6"},
    {"big5", "Big5", "Big5"},
    {"big5-hkscs", "Big5", "Big5"},
    {"chinese", "GBK", "GBK"},
    {"cn-big5", "Big5", "Big5"},
    {"cp1250", "windows-1250", "WINDOWS-1250"},
    {"cp1251", "windows-1251", "WINDOWS-1251"},
    {"cp1252", "windows-1252", "WINDOWS-1252"},
    {"cp1253", "windows-1253", "WINDOWS-1253"},
    {"cp1254", "windows-1254", "WINDOWS-1254"},
    {"cp1255", "windows-1255", "WINDOWS-1255"},
    {"cp1256", "windows-1256", "WINDOWS-1256"},
    {"cp1257", "windows-1257", "WINDOWS-1257"},
    {"cp1258", "windows-1258", "WINDOWS-1258"},
    {"cp819", "windows-1252", "WINDOWS-1252"},
    {"cp866", "IBM866", "IBM866"},
    {"csbig5", "Big5", "Big5"},
    {"cseuckr", "EUC-KR", "EUC-KR"},
    {"cseucpkdfmtjapanese", "EUC-JP", "EUC-JP"},
    {"csgb2312", "GBK", "GBK"},
    {"csibm866", "IBM866", "IBM866"},
    {"csiso2022jp", "ISO-2022-JP", "ISO-2022-JP"},
    {"csiso2022kr", "replacement", NULL},
    {"csiso58gb231280", "GBK", "GBK"},
    {"csiso88596e", "ISO-8859-6", "ISO-8859-6"},
    {"csiso88596i", "ISO-8859-6", "ISO-8859-6"},
    {"csiso88598e", "ISO-8859-8", "ISO-8859-8"},
    {"csiso88598i", "ISO-8859-8-I", "ISO-8859-8"},
    {"csisolatin1", "windows-1252", "WINDOWS-1252"},
    {"csisolatin2", "ISO-8859-2", "ISO-8859-2"},
    {"csisolatin3", "ISO-8859-3", "ISO-8859-3"},
    {"csisolatin4", "ISO-8859-4", "ISO-8859-4"},
    {"csisolatin5", "windows-1254", "WINDOWS-1254"},
    {"csisolatin6", "ISO-8859-10", "ISO-8859-10"},
    {"csisolatin9", "ISO-8859-15", "ISO-8859-15"},
    {"csisolatinarabic", "ISO-8859-6", "ISO-8859-6"},
    {"csisolatincyrillic", "ISO-8859-5", "ISO-8859-5"},
    {"csisolatingreek", "ISO-8859-7", "ISO-8859-7"},
    {"csisolatinhebrew", "ISO-8859-8", "ISO-8859-8"},
    {"cskoi8r", "KOI8-R", "KOI8-R"},
    {"csksc56011987", "EUC-KR", "EUC-KR"},
    {"csmacintosh", "macintosh", "MACINTOSH"},
    {"csshiftjis", "Shift_JIS", "SHIFT_JIS"},
    {"cyrillic", "ISO-8859-5", "ISO-8859-5"},
    {"dos-874", "windows-874", "TIS-620"},
    {"ecma-114", "ISO-8859-6", "ISO-8859-6"},
    {"ecma-118", "ISO-8859-7", "ISO-8859-7"},
    {"elot_928", "ISO-8859-7", "ISO-8859-7"},
    {"euc-jp", "EUC-JP", "EUC-JP"},
    {"euc-kr", "EUC-KR", "EUC-KR"},
    {"gb18030", "gb18030", "GB18030"},
    {"gb2312", "GBK", "GBK"},
    {"gb_2312", "GBK", "GBK"},
    {"gb_2312-80", "GBK", "GBK"},
    {"gbk", "GBK", "GBK"},
    {"greek", "ISO-8859-7", "ISO-8859-7"},
    {"greek8", "ISO-8859-7", "ISO-8859-7"},
    {"hebrew", "ISO-8859-8", "ISO-8859-8"},
    {"hz-gb-2312", "replacement", NULL},
    {"ibm819", "windows-1252", "WINDOWS-1252"},
    {"ibm866", "IBM866", "IBM866"},
    {"iso-2022-cn", "replacement", NULL},
    {"iso-2022-cn-ext", "replacement", NULL},
    {"iso-2022-jp", "ISO-2022-JP", "ISO-2022-JP"},
    {"iso-2022-kr", "replacement", NULL},
    {"iso-8859-1", "windows-1252", "WINDOWS-1252"},
    {"iso-8859-10", "ISO-8859-10", "ISO-8859-10"},
    {"iso-8859-11", "windows-874", "TIS-620"},
    {"iso-8859-13", "ISO-8859-13", "ISO-8859-13"},
    {"iso-8859-14", "ISO-8859-14", "ISO-8859-14"},
    {"iso-8859-15", "ISO-8859-15", "ISO-8859-15"},
    {"iso-8859-16", "ISO-8859-16", "ISO-8859-16"},
    {"iso-8859-2", "ISO-8859-2", "ISO-8859-2"},
    {"iso-8859-3", "ISO-8859-3", "ISO-8859-3"},
    {"iso-8859-4", "ISO-8859-4", "ISO-8859-4"},
    {"iso-8859-5", "ISO-8859-5", "ISO-8859-5"},
    {"iso-8859-6", "ISO-8859-6", "ISO-8859-6"},
    {"iso-8859-6-e", "ISO-8859-6", "ISO-8859-6"},
    {"iso-8859-6-i", "ISO-8859-6", "ISO-8859-6"},
    {"iso-8859-7", "ISO-8859-7", "ISO-8859-7"},
    {"iso-8859-8", "ISO-8859-8", "ISO-8859-8"},
    {"iso-8859-8-e", "ISO-8859-8", "ISO-8859-8"},
    {"iso-8859-8-i", "ISO-8859-8-I", "ISO-8859-8"},
    {"iso-8859-9", "windows-1254", "WINDOWS-1254"},
    {"iso-ir-100", "windows-1252", "WINDOWS-1252"},
    {"iso-ir-101", "ISO-8859-2", "ISO-8859-2"},
    {"iso-ir-109", "ISO-8859-3", "ISO-8859-3"},
    {"iso-ir-110", "ISO-8859-4", "ISO-8859-4"},
    {"iso-ir-126", "ISO-8859-7", "ISO-8859-7"},
    {"iso-ir-127", "ISO-8859-6", "ISO-8859-6"},
    {"iso-ir-138", "ISO-8859-8", "ISO-8859-8"},
    {"iso-ir-144", "ISO-8859-5", "ISO-8859-5"},
    {"iso-ir-148", "windows-1254", "WINDOWS-1254"},
    {"iso-ir-149", "EUC-KR", "EUC-KR"},
    {"iso-ir-157", "ISO-8859-10", "ISO-8859-10"},
    {"iso-ir-58", "GBK", "GBK"},
    {"iso8859-1", "windows-1252", "WINDOWS-1252"},
    {"iso8859-10", "ISO-8859-10", "ISO-8859-10"},
    {"iso8859-11", "windows-874", "TIS-620"},
    {"iso8859-13", "ISO-8859-13", "ISO-8859-13"},
    {"iso8859-14", "ISO-8859-14", "ISO-8859-14"},
    {"iso8859-15", "ISO-8859-15", "ISO-8859-15"},
    {"iso8859-2", "ISO-8859-2", "ISO-8859-2"},
    {"iso8859-3", "ISO-8859-3", "ISO-8859-3"},
    {"iso8859-4", "ISO-8859-4", "ISO-8859-4"},
    {"iso8859-5", "ISO-8859-5", "ISO-8859-5"},
    {"iso8859-6", "ISO-8859-6", "ISO-8859-6"},
    {"iso8859-7", "ISO-8859-7", "ISO-8859-7"},
    {"iso8859-8", "ISO-8859-8", "ISO-8859-8"},
    {"iso8859-9", "windows-1254", "WINDOWS-1254"},
    {"iso88591", "windows-1252", "WINDOWS-1252"},
    {"iso885910", "ISO-8859-10", "ISO-8859-10"},
    {"iso885911", "windows-874", "TIS-620"},
    {"iso885913", "ISO-8859-13", "ISO-8859-13"},
    {"iso885914", "ISO-8859-14", "ISO-8859-14"},
    {"iso885915", "ISO-8859-15", "ISO-8859-15"},
    {"iso88592", "ISO-8859-2", "ISO-8859-2"},
    {"iso88593", "ISO-8859-3", "ISO-8859-3"},
    {"iso88594", "ISO-8859-4", "ISO-8859-4"},
    {"iso88595", "ISO-8859-5", "ISO-8859-5"},
    {"iso88596", "ISO-8859-6", "ISO-8859-6"},
    {"iso88597", "ISO-8859-7", "ISO-8859-7"},
    {"iso88598", "ISO-8859-8", "ISO-8859-8"},
    {"iso88599", "windows-1254", "WINDOWS-1254"},
    {"iso_8859-1", "windows-1252", "WINDOWS-1252"},
    {"iso_8859-15", "ISO-8859-15", "ISO-8859-15"},
    {"iso_8859-1:1987", "windows-1252", "WINDOWS-1252"},
    {"iso_8859-2", "ISO-8859-2", "ISO-8859-2"},
    {"iso_8859-2:1987", "ISO-8859-2", "ISO-8859-2"},
    {"iso_8859-3", "ISO-8859-3", "ISO-8859-3"},
    {"iso_8859-3:1988", "ISO-8859-3", "ISO-8859-3"},
    {"iso_8859-4", "ISO-8859-4", "ISO-8859-4"},
    {"iso_8859-4:1988", "ISO-8859-4", "ISO-8859-4"},
    {"iso_8859-5", "ISO-8859-5", "ISO-8859-5"},
    {"iso_8859-5:1988", "ISO-8859-5", "ISO-8859-5"},
    {"iso_8859-6", "ISO-8859-6", "ISO-8859-6"},
    {"iso_8859-6:1987", "ISO-8859-6", "ISO-8859-6"},
    {"iso_8859-7", "ISO-8859-7", "ISO-8859-7"},
    {"iso_8859-7:1987", "ISO-8859-7", "ISO-8859-7"},
    {"iso_8859-8", "ISO-8859-8", "ISO-8859-8"},
    {"iso_8859-8:1988", "ISO-8859-8", "ISO-8859-8"},
    {"iso_8859-9", "windows-1254", "WINDOWS-1254"},
    {"iso_8859-9:1989", "windows-1254", "WINDOWS-1254"},
    {"koi", "KOI8-R", "KOI8-R"},
    {"koi8", "KOI8-R", "KOI8-R"},
    {"koi8-r", "KOI8-R", "KOI8-R"},
    {"koi8-ru", "KOI8-U", "KOI8-U"},
    {"koi8-u", "KOI8-U", "KOI8-U"},
    {"koi8_r", "KOI8-R", "KOI8-R"},
    {"korean", "EUC-KR", "EUC-KR"},
    {"ks_c_5601-1987", "EUC-KR", "EUC-KR"},
    {"ks_c_5601-1989", "EUC-KR", "EUC-KR"},
    {"ksc5601", "EUC-KR", "EUC-KR"},
    {"ksc_5601", "EUC-KR", "EUC-KR"},
    {"l1", "windows-1252", "WINDOWS-1252"},
    {"l2", "ISO-8859-2", "ISO-8859-2"},
    {"l3", "ISO-8859-3", "ISO-8859-3"},
    {"l4", "ISO-8859-4", "ISO-8859-4"},
    {"l5", "windows-1254", "WINDOWS-1254"},
    {"l6", "ISO-8859-10", "ISO-8859-10"},
    {"l9", "ISO-8859-15", "ISO-8859-15"},
    {"latin1", "windows-1252", "WINDOWS-1252"},
    {"latin2", "ISO-8859-2", "ISO-8859-2"},
    {"latin3", "ISO-8859-3", "ISO-8859-3"},
    {"latin4", "ISO-8859-4", "ISO-8859-4"},
    {"latin5", "windows-1254", "WINDOWS-1254"},
    {"latin6", "ISO-8859-10", "ISO-8859-10"},
    {"logical", "ISO-8859-8-I", "ISO-8859-8"},
    {"mac", "macintosh", "MACINTOSH"},
    {"macintosh", "macintosh", "MACINTOSH"},
    {"ms932", "Shift_JIS", "SHIFT_JIS"},
    {"ms_kanji", "Shift_JIS", "SHIFT_JIS"},
    {"shift-jis", "Shift_JIS", "SHIFT_JIS"},
    {"shift_jis", "Shift_JIS", "SHIFT_JIS"},
    {"sjis", "Shift_JIS", "SHIFT_JIS"},
    {"sun_eu_greek", "ISO-8859-7", "ISO-8859-7"},
    {"tis-620", "windows-874", "TIS-620"},
    {"unicode-1-1-utf-8", "UTF-8", "UTF-8"},
    {"unicode11utf8", "UTF-8", "UTF-8"},
    {"unicode20utf8", "UTF-8", "UTF-8"},
    {"us-ascii", "windows-1252", "WINDOWS-1252"},
    {"utf-16", "UTF-16LE", "UTF-16LE"},
    {"utf-16be", "UTF-16BE", "UTF-16BE"},
    {"utf-16le", "UTF-16LE", "UTF-16LE"},
    {"utf-8", "UTF-8", "UTF-8"},
    {"utf8", "UTF-8", "UTF-8"},
    {"visual", "ISO-8859-8", "ISO-8859-8"},
    {"windows-1250", "windows-1250", "WINDOWS-1250"},
    {"windows-1251", "windows-1251", "WINDOWS-1251"},
    {"windows-1252", "windows-1252", "WINDOWS-1252"},
    {"windows-1253", "windows-1253", "WINDOWS-1253"},
    {"windows-1254", "windows-1254", "WINDOWS-1254"},
    {"windows-1255", "windows-1255", "WINDOWS-1255"},
    {"windows-1256", "windows-1256", "WINDOWS-1256"},
    {"windows-1257", "windows-1257", "WINDOWS-1257"},
    {"windows-1258", "windows-1258", "WINDOWS-1258"},
    {"windows-31j", "Shift_JIS", "SHIFT_JIS"},
    {"windows-874", "windows-874", "TIS-620"},
    {"windows-949", "EUC-KR", "EUC-KR"},
    {"x-cp1250", "windows-1250", "WINDOWS-1250"},
    {"x-cp1251", "windows-1251", "WINDOWS-1251"},
    {"x-cp1252", "windows-1252", "WINDOWS-1252"},
    {"x-cp1253", "windows-1253", "WINDOWS-1253"},
    {"x-cp1255", "windows-1255", "WINDOWS-1255"},
    {"x-cp1256", "windows-1256", "WINDOWS-1256"},
    {"x-cp1257", "windows-1257", "WINDOWS-1257"},
    {"x-cp1258", "windows-1258", "WINDOWS-1258"},
    {"x-euc-jp", "EUC-JP", "EUC-JP"},
    {"x-gbk", "GBK", "GBK"},
    {"x-mac-cyrillic", "x-mac-cyrillic", "MAC-CYRILLIC"},
    {"x-mac-roman", "macintosh", "MACINTOSH"},
    {"x-mac-ukrainian", "x-mac-cyrillic", "MAC-CYRILLIC"},
    {"x-sjis", "Shift_JIS", "SHIFT_JIS"},
    {"x-unicode20utf8", "UTF-8", "UTF-8"},
    {"x-user-defined", "x-user-defined", NULL},
    {"x-x-big5", "Big5", "Big5"},
};

static const size_t encoding_table_size =
    sizeof(encoding_table) / sizeof(encoding_table[0]);

/* ========================================================================
 * Label normalization and lookup
 * ======================================================================== */

static int entry_cmp(const void *key, const void *entry) {
    return strcmp((const char *)key, ((const encoding_entry *)entry)->label);
}

static char *normalize_label(const char *label) {
    if (!label) return NULL;
    /* skip leading ASCII whitespace */
    while (*label == ' ' || *label == '\t' || *label == '\n' ||
           *label == '\r' || *label == '\f')
        label++;
    size_t len = strlen(label);
    /* skip trailing ASCII whitespace */
    while (len > 0) {
        char c = label[len - 1];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f')
            len--;
        else
            break;
    }
    if (len == 0) return NULL;
    char *norm = (char *)malloc(len + 1);
    if (!norm) return NULL;
    for (size_t i = 0; i < len; i++)
        norm[i] = (char)tolower((unsigned char)label[i]);
    norm[len] = '\0';
    return norm;
}

const char *encoding_resolve_label(const char *label) {
    char *norm = normalize_label(label);
    if (!norm) return NULL;
    encoding_entry *found = (encoding_entry *)bsearch(
        norm, encoding_table, encoding_table_size,
        sizeof(encoding_entry), entry_cmp);
    free(norm);
    return found ? found->canonical : NULL;
}

static const encoding_entry *lookup_entry(const char *label) {
    char *norm = normalize_label(label);
    if (!norm) return NULL;
    encoding_entry *found = (encoding_entry *)bsearch(
        norm, encoding_table, encoding_table_size,
        sizeof(encoding_entry), entry_cmp);
    free(norm);
    return found;
}

/* ========================================================================
 * BOM detection
 * ======================================================================== */

typedef struct {
    const char *encoding;
    size_t skip;  /* bytes to skip past BOM */
} bom_result;

static bom_result detect_bom(const unsigned char *raw, size_t raw_len) {
    bom_result r = {NULL, 0};
    if (raw_len >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
        r.encoding = "UTF-8";
        r.skip = 3;
    } else if (raw_len >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
        r.encoding = "UTF-16BE";
        r.skip = 2;
    } else if (raw_len >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        r.encoding = "UTF-16LE";
        r.skip = 2;
    }
    return r;
}

/* ========================================================================
 * Meta prescan (WHATWG HTML 13.2.3.2)
 * ======================================================================== */

static int prescan_is_space(unsigned char c) {
    return c == 0x09 || c == 0x0A || c == 0x0C || c == 0x0D || c == 0x20;
}

/* Extract charset value from a Content-Type string.
 * E.g. "text/html; charset=windows-1252" -> "windows-1252" */
static const char *extract_charset_from_content(const char *value,
                                                 size_t value_len,
                                                 char *out, size_t out_size) {
    /* Search for "charset" (case-insensitive) */
    for (size_t i = 0; i + 7 <= value_len; i++) {
        if ((value[i] == 'c' || value[i] == 'C') &&
            (value[i+1] == 'h' || value[i+1] == 'H') &&
            (value[i+2] == 'a' || value[i+2] == 'A') &&
            (value[i+3] == 'r' || value[i+3] == 'R') &&
            (value[i+4] == 's' || value[i+4] == 'S') &&
            (value[i+5] == 'e' || value[i+5] == 'E') &&
            (value[i+6] == 't' || value[i+6] == 'T')) {
            size_t j = i + 7;
            /* skip spaces */
            while (j < value_len && prescan_is_space((unsigned char)value[j]))
                j++;
            if (j >= value_len || value[j] != '=') continue;
            j++; /* skip '=' */
            /* skip spaces */
            while (j < value_len && prescan_is_space((unsigned char)value[j]))
                j++;
            if (j >= value_len) return NULL;
            size_t start, end;
            if (value[j] == '"') {
                j++;
                start = j;
                while (j < value_len && value[j] != '"') j++;
                end = j;
            } else if (value[j] == '\'') {
                j++;
                start = j;
                while (j < value_len && value[j] != '\'') j++;
                end = j;
            } else {
                start = j;
                while (j < value_len && value[j] != ';' &&
                       !prescan_is_space((unsigned char)value[j]))
                    j++;
                end = j;
            }
            size_t len = end - start;
            if (len == 0 || len >= out_size) return NULL;
            memcpy(out, value + start, len);
            out[len] = '\0';
            return out;
        }
    }
    return NULL;
}

/* Prescan a meta tag's attributes for charset info.
 * pos: points right after "<meta" (must be at a space or / or >).
 * Returns charset label or NULL. */
static const char *prescan_meta_tag(const unsigned char *raw, size_t raw_len,
                                     size_t *pos, char *buf, size_t buf_size) {
    int got_pragma = 0;
    int need_pragma = -1; /* -1 = unset */
    const char *charset_value = NULL;

    for (;;) {
        /* skip whitespace */
        while (*pos < raw_len && prescan_is_space(raw[*pos]))
            (*pos)++;
        if (*pos >= raw_len) return NULL;
        if (raw[*pos] == '>' || raw[*pos] == '/') return NULL;

        /* read attribute name */
        size_t name_start = *pos;
        while (*pos < raw_len && raw[*pos] != '=' &&
               !prescan_is_space(raw[*pos]) &&
               raw[*pos] != '>' && raw[*pos] != '/')
            (*pos)++;
        size_t name_len = *pos - name_start;
        if (name_len == 0) { (*pos)++; continue; }

        /* skip whitespace */
        while (*pos < raw_len && prescan_is_space(raw[*pos]))
            (*pos)++;

        /* check for '=' */
        if (*pos >= raw_len || raw[*pos] != '=') {
            /* attribute with no value — process name only */
            continue;
        }
        (*pos)++; /* skip '=' */

        /* skip whitespace */
        while (*pos < raw_len && prescan_is_space(raw[*pos]))
            (*pos)++;
        if (*pos >= raw_len) return NULL;

        /* read attribute value */
        size_t val_start, val_end;
        if (raw[*pos] == '"') {
            (*pos)++;
            val_start = *pos;
            while (*pos < raw_len && raw[*pos] != '"') (*pos)++;
            val_end = *pos;
            if (*pos < raw_len) (*pos)++;
        } else if (raw[*pos] == '\'') {
            (*pos)++;
            val_start = *pos;
            while (*pos < raw_len && raw[*pos] != '\'') (*pos)++;
            val_end = *pos;
            if (*pos < raw_len) (*pos)++;
        } else {
            val_start = *pos;
            while (*pos < raw_len && !prescan_is_space(raw[*pos]) &&
                   raw[*pos] != '>')
                (*pos)++;
            val_end = *pos;
        }

        size_t val_len = val_end - val_start;

        /* Check which attribute this is */
        if (name_len == 10 &&
            (raw[name_start]   == 'h' || raw[name_start]   == 'H') &&
            (raw[name_start+1] == 't' || raw[name_start+1] == 'T') &&
            (raw[name_start+2] == 't' || raw[name_start+2] == 'T') &&
            (raw[name_start+3] == 'p' || raw[name_start+3] == 'P') &&
            raw[name_start+4] == '-' &&
            (raw[name_start+5] == 'e' || raw[name_start+5] == 'E') &&
            (raw[name_start+6] == 'q' || raw[name_start+6] == 'Q') &&
            (raw[name_start+7] == 'u' || raw[name_start+7] == 'U') &&
            (raw[name_start+8] == 'i' || raw[name_start+8] == 'I') &&
            (raw[name_start+9] == 'v' || raw[name_start+9] == 'V')) {
            /* http-equiv: check if value is "content-type" */
            if (val_len == 12) {
                char tmp[13];
                for (size_t k = 0; k < 12; k++)
                    tmp[k] = (char)tolower(raw[val_start + k]);
                tmp[12] = '\0';
                if (strcmp(tmp, "content-type") == 0)
                    got_pragma = 1;
            }
        } else if (name_len == 7 &&
                   (raw[name_start]   == 'c' || raw[name_start]   == 'C') &&
                   (raw[name_start+1] == 'o' || raw[name_start+1] == 'O') &&
                   (raw[name_start+2] == 'n' || raw[name_start+2] == 'N') &&
                   (raw[name_start+3] == 't' || raw[name_start+3] == 'T') &&
                   (raw[name_start+4] == 'e' || raw[name_start+4] == 'E') &&
                   (raw[name_start+5] == 'n' || raw[name_start+5] == 'N') &&
                   (raw[name_start+6] == 't' || raw[name_start+6] == 'T')) {
            /* content attribute — extract charset from Content-Type value */
            char content_val[256];
            if (val_len < sizeof(content_val)) {
                memcpy(content_val, raw + val_start, val_len);
                content_val[val_len] = '\0';
                const char *cs = extract_charset_from_content(
                    content_val, val_len, buf, buf_size);
                if (cs) {
                    charset_value = cs;
                    if (need_pragma == -1) need_pragma = 1;
                }
            }
        } else if (name_len == 7 &&
                   (raw[name_start]   == 'c' || raw[name_start]   == 'C') &&
                   (raw[name_start+1] == 'h' || raw[name_start+1] == 'H') &&
                   (raw[name_start+2] == 'a' || raw[name_start+2] == 'A') &&
                   (raw[name_start+3] == 'r' || raw[name_start+3] == 'R') &&
                   (raw[name_start+4] == 's' || raw[name_start+4] == 'S') &&
                   (raw[name_start+5] == 'e' || raw[name_start+5] == 'E') &&
                   (raw[name_start+6] == 't' || raw[name_start+6] == 'T')) {
            /* charset attribute */
            if (val_len < buf_size) {
                memcpy(buf, raw + val_start, val_len);
                buf[val_len] = '\0';
                charset_value = buf;
                need_pragma = 0;
            }
        }

        /* Check termination */
        if (*pos < raw_len && raw[*pos] == '>') {
            (*pos)++;
            break;
        }
    }

    if (!charset_value) return NULL;
    if (need_pragma == 1 && !got_pragma) return NULL;
    if (need_pragma == -1) return NULL;
    return charset_value;
}

static const char *meta_prescan(const unsigned char *raw, size_t raw_len) {
    size_t scan_len = raw_len < 1024 ? raw_len : 1024;
    size_t pos = 0;
    static char charset_buf[128];

    while (pos < scan_len) {
        if (raw[pos] != '<') { pos++; continue; }
        pos++; /* skip '<' */
        if (pos >= scan_len) break;

        /* Check for <!--...--> */
        if (pos + 2 < scan_len && raw[pos] == '!' &&
            raw[pos+1] == '-' && raw[pos+2] == '-') {
            pos += 3;
            while (pos + 2 < scan_len) {
                if (raw[pos] == '-' && raw[pos+1] == '-' && raw[pos+2] == '>') {
                    pos += 3;
                    break;
                }
                pos++;
            }
            continue;
        }

        /* Check for <meta */
        if (pos + 4 < scan_len &&
            (raw[pos]   == 'm' || raw[pos]   == 'M') &&
            (raw[pos+1] == 'e' || raw[pos+1] == 'E') &&
            (raw[pos+2] == 't' || raw[pos+2] == 'T') &&
            (raw[pos+3] == 'a' || raw[pos+3] == 'A') &&
            (prescan_is_space(raw[pos+4]) || raw[pos+4] == '/' || raw[pos+4] == '>')) {
            pos += 4;
            const char *result = prescan_meta_tag(
                raw, scan_len, &pos, charset_buf, sizeof(charset_buf));
            if (result) {
                const char *resolved = encoding_resolve_label(result);
                if (resolved) return resolved;
            }
            continue;
        }

        /* Skip <! / </ / <? constructs */
        if (raw[pos] == '!' || raw[pos] == '/' || raw[pos] == '?') {
            while (pos < scan_len && raw[pos] != '>') pos++;
            if (pos < scan_len) pos++;
            continue;
        }

        /* Skip other tags */
        if ((raw[pos] >= 'A' && raw[pos] <= 'Z') ||
            (raw[pos] >= 'a' && raw[pos] <= 'z')) {
            while (pos < scan_len && raw[pos] != '>') pos++;
            if (pos < scan_len) pos++;
            continue;
        }
    }
    return NULL;
}

/* ========================================================================
 * Encoding conversion
 * ======================================================================== */

/* Built-in UTF-16 to UTF-8 converter (no iconv needed) */
static char *convert_utf16_to_utf8(const unsigned char *raw, size_t raw_len,
                                    int big_endian, size_t *out_len) {
    size_t cap = raw_len * 2 + 4;
    char *out = (char *)malloc(cap);
    if (!out) return NULL;
    size_t olen = 0;
    size_t i = 0;

    while (i + 1 < raw_len) {
        /* ensure space for max UTF-8 char (4 bytes) + NUL */
        if (olen + 5 > cap) {
            cap *= 2;
            char *tmp = (char *)realloc(out, cap);
            if (!tmp) { free(out); return NULL; }
            out = tmp;
        }

        unsigned int w1;
        if (big_endian)
            w1 = ((unsigned int)raw[i] << 8) | raw[i+1];
        else
            w1 = raw[i] | ((unsigned int)raw[i+1] << 8);
        i += 2;

        unsigned int cp;
        if (w1 >= 0xD800 && w1 <= 0xDBFF) {
            /* high surrogate — need low surrogate */
            if (i + 1 < raw_len) {
                unsigned int w2;
                if (big_endian)
                    w2 = ((unsigned int)raw[i] << 8) | raw[i+1];
                else
                    w2 = raw[i] | ((unsigned int)raw[i+1] << 8);
                if (w2 >= 0xDC00 && w2 <= 0xDFFF) {
                    cp = 0x10000 + ((w1 - 0xD800) << 10) + (w2 - 0xDC00);
                    i += 2;
                } else {
                    cp = 0xFFFD; /* unpaired high surrogate */
                }
            } else {
                cp = 0xFFFD;
            }
        } else if (w1 >= 0xDC00 && w1 <= 0xDFFF) {
            cp = 0xFFFD; /* unpaired low surrogate */
        } else {
            cp = w1;
        }

        /* encode as UTF-8 */
        if (cp < 0x80) {
            out[olen++] = (char)cp;
        } else if (cp < 0x800) {
            out[olen++] = (char)(0xC0 | (cp >> 6));
            out[olen++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out[olen++] = (char)(0xE0 | (cp >> 12));
            out[olen++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[olen++] = (char)(0x80 | (cp & 0x3F));
        } else {
            out[olen++] = (char)(0xF0 | (cp >> 18));
            out[olen++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            out[olen++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[olen++] = (char)(0x80 | (cp & 0x3F));
        }
    }

    /* handle trailing byte for odd-length input */
    if (i < raw_len) {
        if (olen + 4 > cap) {
            cap += 4;
            char *tmp = (char *)realloc(out, cap);
            if (!tmp) { free(out); return NULL; }
            out = tmp;
        }
        /* U+FFFD */
        out[olen++] = (char)0xEF;
        out[olen++] = (char)0xBF;
        out[olen++] = (char)0xBD;
    }

    out[olen] = '\0';
    *out_len = olen;
    return out;
}

/* x-user-defined: 0x00-0x7F unchanged, 0x80-0xFF -> U+F780-U+F7FF */
static char *convert_x_user_defined(const unsigned char *raw, size_t raw_len,
                                     size_t *out_len) {
    /* worst case: each byte -> 3 UTF-8 bytes */
    size_t cap = raw_len * 3 + 1;
    char *out = (char *)malloc(cap);
    if (!out) return NULL;
    size_t olen = 0;

    for (size_t i = 0; i < raw_len; i++) {
        if (raw[i] < 0x80) {
            out[olen++] = (char)raw[i];
        } else {
            unsigned int cp = 0xF780 + (raw[i] - 0x80);
            out[olen++] = (char)(0xE0 | (cp >> 12));
            out[olen++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[olen++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    out[olen] = '\0';
    *out_len = olen;
    return out;
}

/* ========================================================================
 * Built-in ISO-2022-JP decoder (WHATWG Encoding Standard §15.2)
 * State machine: ASCII, Roman, Katakana, Lead byte, Trail byte, Escape
 * ======================================================================== */

/* Helper: append a Unicode codepoint as UTF-8 to output buffer.
 * Grows buffer if needed. Returns 0 on success, -1 on alloc failure. */
static int iso2022jp_emit_cp(char **out, size_t *olen, size_t *cap,
                              unsigned int cp) {
    /* ensure space for max 4 UTF-8 bytes */
    if (*olen + 4 > *cap) {
        *cap *= 2;
        char *tmp = (char *)realloc(*out, *cap);
        if (!tmp) return -1;
        *out = tmp;
    }
    if (cp < 0x80) {
        (*out)[(*olen)++] = (char)cp;
    } else if (cp < 0x800) {
        (*out)[(*olen)++] = (char)(0xC0 | (cp >> 6));
        (*out)[(*olen)++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        (*out)[(*olen)++] = (char)(0xE0 | (cp >> 12));
        (*out)[(*olen)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        (*out)[(*olen)++] = (char)(0x80 | (cp & 0x3F));
    } else {
        (*out)[(*olen)++] = (char)(0xF0 | (cp >> 18));
        (*out)[(*olen)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        (*out)[(*olen)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        (*out)[(*olen)++] = (char)(0x80 | (cp & 0x3F));
    }
    return 0;
}

static char *convert_iso2022jp_to_utf8(const unsigned char *raw, size_t raw_len,
                                        size_t *out_len) {
    enum {
        ISO2022_ASCII,
        ISO2022_ROMAN,
        ISO2022_KATAKANA,
        ISO2022_LEAD,
        ISO2022_TRAIL,
        ISO2022_ESCAPE_START,
        ISO2022_ESCAPE
    } state = ISO2022_ASCII, output_state = ISO2022_ASCII;

    size_t cap = raw_len * 3 + 4;
    char *out = (char *)malloc(cap);
    if (!out) return NULL;
    size_t olen = 0;

    int output_flag = 0;
    unsigned char lead = 0;
    size_t i = 0;

    while (i <= raw_len) {
        /* At i == raw_len, we process EOF (byte = -1 sentinel) */
        int is_eof = (i == raw_len);
        unsigned char byte = is_eof ? 0 : raw[i];

        switch (state) {
        case ISO2022_ASCII:
            if (is_eof) goto done;
            if (byte == 0x1B) {
                state = ISO2022_ESCAPE_START;
                i++;
                break;
            }
            if (byte <= 0x7F && byte != 0x0E && byte != 0x0F) {
                output_flag = 1;
                if (iso2022jp_emit_cp(&out, &olen, &cap, byte) < 0)
                    goto fail;
                i++;
            } else {
                output_flag = 0;
                if (iso2022jp_emit_cp(&out, &olen, &cap, 0xFFFD) < 0)
                    goto fail;
                i++;
            }
            break;

        case ISO2022_ROMAN:
            if (is_eof) goto done;
            if (byte == 0x1B) {
                state = ISO2022_ESCAPE_START;
                i++;
                break;
            }
            if (byte == 0x5C) {
                output_flag = 1;
                if (iso2022jp_emit_cp(&out, &olen, &cap, 0x00A5) < 0)
                    goto fail;
                i++;
            } else if (byte == 0x7E) {
                output_flag = 1;
                if (iso2022jp_emit_cp(&out, &olen, &cap, 0x203E) < 0)
                    goto fail;
                i++;
            } else if (byte <= 0x7F && byte != 0x0E && byte != 0x0F) {
                output_flag = 1;
                if (iso2022jp_emit_cp(&out, &olen, &cap, byte) < 0)
                    goto fail;
                i++;
            } else {
                output_flag = 0;
                if (iso2022jp_emit_cp(&out, &olen, &cap, 0xFFFD) < 0)
                    goto fail;
                i++;
            }
            break;

        case ISO2022_KATAKANA:
            if (is_eof) goto done;
            if (byte == 0x1B) {
                state = ISO2022_ESCAPE_START;
                i++;
                break;
            }
            if (byte >= 0x21 && byte <= 0x5F) {
                output_flag = 1;
                if (iso2022jp_emit_cp(&out, &olen, &cap, 0xFF61 - 0x21 + byte) < 0)
                    goto fail;
                i++;
            } else {
                output_flag = 0;
                if (iso2022jp_emit_cp(&out, &olen, &cap, 0xFFFD) < 0)
                    goto fail;
                i++;
            }
            break;

        case ISO2022_LEAD:
            if (is_eof) goto done;
            if (byte == 0x1B) {
                state = ISO2022_ESCAPE_START;
                i++;
                break;
            }
            if (byte >= 0x21 && byte <= 0x7E) {
                output_flag = 0;
                lead = byte;
                state = ISO2022_TRAIL;
                i++;
            } else {
                output_flag = 0;
                if (iso2022jp_emit_cp(&out, &olen, &cap, 0xFFFD) < 0)
                    goto fail;
                i++;
            }
            break;

        case ISO2022_TRAIL:
            if (is_eof) {
                /* Incomplete two-byte sequence at EOF */
                if (iso2022jp_emit_cp(&out, &olen, &cap, 0xFFFD) < 0)
                    goto fail;
                goto done;
            }
            if (byte == 0x1B) {
                /* ESC interrupts trail byte — emit error for lead */
                if (iso2022jp_emit_cp(&out, &olen, &cap, 0xFFFD) < 0)
                    goto fail;
                state = ISO2022_ESCAPE_START;
                i++;
                break;
            }
            if (byte >= 0x21 && byte <= 0x7E) {
                unsigned int pointer = (unsigned int)(lead - 0x21) * 94 + (byte - 0x21);
                unsigned int cp = 0xFFFD;
                if (pointer < JIS0208_TABLE_SIZE && jis0208_table[pointer] != 0) {
                    cp = jis0208_table[pointer];
                }
                if (iso2022jp_emit_cp(&out, &olen, &cap, cp) < 0)
                    goto fail;
                state = ISO2022_LEAD;
                output_flag = (cp != 0xFFFD) ? 1 : 0;
                i++;
            } else {
                if (iso2022jp_emit_cp(&out, &olen, &cap, 0xFFFD) < 0)
                    goto fail;
                state = ISO2022_LEAD;
                output_flag = 0;
                i++;
            }
            break;

        case ISO2022_ESCAPE_START: {
            if (is_eof) {
                /* ESC at EOF: emit error, done */
                output_flag = 0;
                if (iso2022jp_emit_cp(&out, &olen, &cap, 0xFFFD) < 0)
                    goto fail;
                goto done;
            }
            if (byte == 0x24 || byte == 0x28) {
                lead = byte;
                state = ISO2022_ESCAPE;
                i++;
            } else {
                /* Not a recognized escape — output error, re-process byte
                 * in output_state */
                output_flag = 0;
                state = output_state;
                if (iso2022jp_emit_cp(&out, &olen, &cap, 0xFFFD) < 0)
                    goto fail;
                /* Don't advance i — re-process this byte */
            }
            break;
        }

        case ISO2022_ESCAPE: {
            if (is_eof) {
                /* Incomplete escape at EOF */
                output_flag = 0;
                if (iso2022jp_emit_cp(&out, &olen, &cap, 0xFFFD) < 0)
                    goto fail;
                goto done;
            }
            int recognized = 0;
            enum { T_NONE, T_ASCII, T_ROMAN, T_KATAKANA, T_LEAD } target = T_NONE;

            if (lead == 0x28 && byte == 0x42) {
                target = T_ASCII;
                recognized = 1;
            } else if (lead == 0x28 && byte == 0x4A) {
                target = T_ROMAN;
                recognized = 1;
            } else if (lead == 0x28 && byte == 0x49) {
                target = T_KATAKANA;
                recognized = 1;
            } else if (lead == 0x24 && (byte == 0x40 || byte == 0x42)) {
                target = T_LEAD;
                recognized = 1;
            }

            if (recognized) {
                switch (target) {
                case T_ASCII:    state = ISO2022_ASCII;    break;
                case T_ROMAN:    state = ISO2022_ROMAN;    break;
                case T_KATAKANA: state = ISO2022_KATAKANA; break;
                case T_LEAD:     state = ISO2022_LEAD;     break;
                default: break;
                }
                output_state = state;
                /* If output_flag is set, emit U+FFFD per spec
                 * (security measure for state transitions) */
                if (output_flag) {
                    if (iso2022jp_emit_cp(&out, &olen, &cap, 0xFFFD) < 0)
                        goto fail;
                    output_flag = 0;
                }
                i++;
            } else {
                /* Unrecognized escape sequence — error, prepend lead+byte
                 * back to stream. Actually per WHATWG spec: set output_flag = 0,
                 * restore state to output_state, and re-process the byte
                 * after the ESC (which is lead) and then this byte.
                 * We handle by emitting U+FFFD and backing up to re-process
                 * lead and byte in output_state. */
                output_flag = 0;
                state = output_state;
                if (iso2022jp_emit_cp(&out, &olen, &cap, 0xFFFD) < 0)
                    goto fail;
                /* Back up: re-process 'lead' byte and current byte.
                 * We need to re-process two bytes. Since we consumed the
                 * ESC and the lead already, we back i by 1 to re-process
                 * from lead. Actually lead was consumed when entering
                 * ESCAPE state, and byte hasn't been consumed yet.
                 * We need to re-process lead byte (i-1) and current byte (i).
                 * So back up i by 1. */
                i--; /* re-process from lead byte */
            }
            break;
        }
        } /* switch */
    } /* while */

done:
    out[olen] = '\0';
    *out_len = olen;
    return out;

fail:
    free(out);
    return NULL;
}

#ifdef HAVE_ICONV
static char *convert_with_iconv(const unsigned char *raw, size_t raw_len,
                                 const char *iconv_name, size_t *out_len) {
    iconv_t cd = iconv_open("UTF-8", iconv_name);
    if (cd == (iconv_t)-1) return NULL;

    size_t cap = raw_len * 4 + 4;
    char *out = (char *)malloc(cap);
    if (!out) { iconv_close(cd); return NULL; }

    char *in_ptr = (char *)raw;
    size_t in_left = raw_len;
    char *out_ptr = out;
    size_t out_left = cap;

    while (in_left > 0) {
        size_t rc = iconv(cd, &in_ptr, &in_left, &out_ptr, &out_left);
        if (rc == (size_t)-1) {
            if (errno == E2BIG) {
                size_t used = (size_t)(out_ptr - out);
                cap *= 2;
                char *tmp = (char *)realloc(out, cap);
                if (!tmp) { free(out); iconv_close(cd); return NULL; }
                out = tmp;
                out_ptr = out + used;
                out_left = cap - used;
            } else {
                /* EILSEQ or EINVAL: insert U+FFFD, skip 1 byte */
                size_t used = (size_t)(out_ptr - out);
                if (out_left < 3) {
                    cap *= 2;
                    char *tmp = (char *)realloc(out, cap);
                    if (!tmp) { free(out); iconv_close(cd); return NULL; }
                    out = tmp;
                    out_ptr = out + used;
                    out_left = cap - used;
                }
                *out_ptr++ = (char)0xEF;
                *out_ptr++ = (char)0xBF;
                *out_ptr++ = (char)0xBD;
                out_left -= 3;
                in_ptr++;
                in_left--;
                /* reset iconv state after error */
                iconv(cd, NULL, NULL, NULL, NULL);
            }
        }
    }

    *out_ptr = '\0';
    *out_len = (size_t)(out_ptr - out);
    iconv_close(cd);
    return out;
}
#endif

static char *convert_to_utf8(const unsigned char *raw, size_t raw_len,
                              const char *canonical, size_t *out_len) {
    /* replacement encoding: return a single U+FFFD */
    if (strcmp(canonical, "replacement") == 0) {
        char *out = (char *)malloc(4);
        if (!out) return NULL;
        out[0] = (char)0xEF;
        out[1] = (char)0xBF;
        out[2] = (char)0xBD;
        out[3] = '\0';
        *out_len = 3;
        return out;
    }

    /* x-user-defined: built-in converter */
    if (strcmp(canonical, "x-user-defined") == 0)
        return convert_x_user_defined(raw, raw_len, out_len);

    /* UTF-16: built-in converter (works without iconv) */
    if (strcmp(canonical, "UTF-16BE") == 0)
        return convert_utf16_to_utf8(raw, raw_len, 1, out_len);
    if (strcmp(canonical, "UTF-16LE") == 0)
        return convert_utf16_to_utf8(raw, raw_len, 0, out_len);

    /* ISO-2022-JP: built-in state machine decoder */
    if (strcmp(canonical, "ISO-2022-JP") == 0)
        return convert_iso2022jp_to_utf8(raw, raw_len, out_len);

#ifdef HAVE_ICONV
    {
        /* find iconv name */
        const encoding_entry *ent = lookup_entry(canonical);
        const char *iconv_name = ent ? ent->iconv_name : canonical;
        if (!iconv_name) return NULL;
        return convert_with_iconv(raw, raw_len, iconv_name, out_len);
    }
#else
    (void)raw; (void)raw_len; (void)canonical; (void)out_len;
    return NULL;
#endif
}

/* ========================================================================
 * Main entry: encoding_sniff_and_convert()
 * ======================================================================== */

encoding_result encoding_sniff_and_convert(const unsigned char *raw,
                                           size_t raw_len,
                                           const char *hint) {
    encoding_result result = {NULL, 0, NULL, ENC_CONFIDENCE_TENTATIVE};

    if (!raw || raw_len == 0) {
        result.data = (char *)calloc(1, 1);
        result.len = 0;
        result.encoding = "UTF-8";
        result.confidence = ENC_CONFIDENCE_IRRELEVANT;
        return result;
    }

    const unsigned char *data = raw;
    size_t data_len = raw_len;
    const char *encoding = NULL;
    encoding_confidence confidence = ENC_CONFIDENCE_TENTATIVE;

    /* Step 1: BOM detection */
    bom_result bom = detect_bom(raw, raw_len);
    if (bom.encoding) {
        encoding = bom.encoding;
        confidence = ENC_CONFIDENCE_CERTAIN;
        data = raw + bom.skip;
        data_len = raw_len - bom.skip;
    }

    /* Step 2: Transport-layer hint */
    if (!encoding && hint) {
        const char *resolved = encoding_resolve_label(hint);
        if (resolved) {
            encoding = resolved;
            confidence = ENC_CONFIDENCE_CERTAIN;
        }
    }

    /* Step 3: Meta prescan */
    if (!encoding) {
        const char *meta_enc = meta_prescan(raw, raw_len);
        if (meta_enc) {
            encoding = meta_enc;
            confidence = ENC_CONFIDENCE_TENTATIVE;
        }
    }

    /* Step 4: Default to UTF-8 */
    if (!encoding) {
        encoding = "UTF-8";
        confidence = ENC_CONFIDENCE_TENTATIVE;
    }

    /* WHATWG: certain encodings get overridden */
    /* UTF-16LE/BE from non-BOM sources get mapped to UTF-8 per spec
     * (only BOM should trigger UTF-16 decoding). */

    /* UTF-8 fast path: memcpy, no conversion */
    if (strcmp(encoding, "UTF-8") == 0) {
        result.data = (char *)malloc(data_len + 1);
        if (!result.data) return result;
        memcpy(result.data, data, data_len);
        result.data[data_len] = '\0';
        result.len = data_len;
        result.encoding = "UTF-8";
        result.confidence = confidence;
        return result;
    }

    /* Convert to UTF-8 */
    size_t out_len = 0;
    char *converted = convert_to_utf8(data, data_len, encoding, &out_len);
    if (!converted) {
        /* Conversion failed — fallback: treat as UTF-8 */
        result.data = (char *)malloc(data_len + 1);
        if (!result.data) return result;
        memcpy(result.data, data, data_len);
        result.data[data_len] = '\0';
        result.len = data_len;
        result.encoding = "UTF-8";
        result.confidence = ENC_CONFIDENCE_TENTATIVE;
        return result;
    }

    result.data = converted;
    result.len = out_len;
    result.encoding = encoding;
    result.confidence = confidence;
    return result;
}
