#ifndef HTML_PARSER_ENCODING_H
#define HTML_PARSER_ENCODING_H

#include <stddef.h>

typedef enum {
    ENC_CONFIDENCE_CERTAIN,    /* BOM or transport-layer hint */
    ENC_CONFIDENCE_TENTATIVE,  /* meta prescan or default */
    ENC_CONFIDENCE_IRRELEVANT  /* already UTF-8 */
} encoding_confidence;

typedef struct {
    char *data;                /* UTF-8 output (heap-allocated, caller must free) */
    size_t len;                /* byte length of data (excluding NUL terminator) */
    const char *encoding;      /* canonical encoding name (static string, do NOT free) */
    encoding_confidence confidence;
} encoding_result;

/* Sniff the encoding of raw bytes and convert to UTF-8.
 * raw: input buffer (may contain any encoding).
 * raw_len: exact byte length.
 * hint: optional transport-layer charset hint (e.g. from HTTP Content-Type), or NULL.
 * Returns: encoding_result with UTF-8 data. data is NULL on failure. */
encoding_result encoding_sniff_and_convert(const unsigned char *raw,
                                           size_t raw_len,
                                           const char *hint);

/* Resolve a charset label to its canonical WHATWG encoding name.
 * Returns canonical name (static string) or NULL if not recognized. */
const char *encoding_resolve_label(const char *label);

#endif
