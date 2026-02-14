#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tree_builder.h"
#include "tokenizer.h"
#include "encoding.h"

/* Read raw file bytes. Caller must free *out_buf. */
static size_t read_file_raw(const char *path, char **out_buf) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (len < 0) { fclose(fp); return 0; }
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(fp); return 0; }
    size_t read_len = fread(buf, 1, (size_t)len, fp);
    fclose(fp);
    buf[read_len] = '\0';
    *out_buf = buf;
    return read_len;
}

int main(int argc, char **argv) {
    const char *charset_hint = NULL;
    int arg_idx = 1;
    /* Parse --charset option */
    if (argc > 2 && strcmp(argv[1], "--charset") == 0) {
        charset_hint = argv[2];
        arg_idx = 3;
    }
    const char *path = (argc > arg_idx) ? argv[arg_idx] : "tests/sample.html";

    /* Read raw bytes */
    char *raw = NULL;
    size_t raw_len = read_file_raw(path, &raw);
    if (!raw) {
        fprintf(stderr, "failed to read %s\n", path);
        return 1;
    }

    /* Encoding sniff and convert */
    encoding_result enc = encoding_sniff_and_convert(
        (const unsigned char *)raw, raw_len, charset_hint);
    if (!enc.data) {
        fprintf(stderr, "encoding conversion failed for %s\n", path);
        free(raw);
        return 1;
    }

    char *input = tokenizer_replace_nulls(enc.data, enc.len);
    free(enc.data);
    const char *encoding = enc.encoding;
    encoding_confidence confidence = enc.confidence;

    /* Build tree — may request re-encoding */
    const char *change_enc = NULL;
    node *doc = build_tree_from_input(input, encoding, confidence, &change_enc);

    if (!doc && change_enc) {
        /* WHATWG §13.2.3.5: re-encode and re-parse with new encoding */
        free(input);
        encoding_result enc2 = encoding_sniff_and_convert(
            (const unsigned char *)raw, raw_len, change_enc);
        free(raw);
        raw = NULL;
        if (!enc2.data) {
            fprintf(stderr, "re-encoding failed for %s\n", path);
            return 1;
        }
        input = tokenizer_replace_nulls(enc2.data, enc2.len);
        free(enc2.data);
        encoding = enc2.encoding;
        doc = build_tree_from_input(input, encoding, ENC_CONFIDENCE_CERTAIN, NULL);
    } else {
        free(raw);
        raw = NULL;
    }

    if (!doc) {
        fprintf(stderr, "failed to build tree\n");
        free(input);
        return 1;
    }

    char title[512];
    snprintf(title, sizeof(title), "--- %s ---", path);
    tree_dump_ascii(doc, title);
    printf("\n");
    node_free(doc);
    free(input);
    return 0;
}
