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
    if (argc - arg_idx < 2) {
        fprintf(stderr, "usage: %s [--charset ENC] <context-tag> <file>\n", argv[0]);
        return 1;
    }
    const char *context_tag = argv[arg_idx];
    const char *path = argv[arg_idx + 1];

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
    free(raw);
    if (!enc.data) {
        fprintf(stderr, "encoding conversion failed for %s\n", path);
        return 1;
    }

    char *input = tokenizer_replace_nulls(enc.data, enc.len);
    free(enc.data);
    const char *encoding = enc.encoding;
    encoding_confidence confidence = enc.confidence;

    /* Fragment parsing inherits encoding from context document.
     * Re-encoding is not applicable for fragments (encoding comes
     * from context element's document), so pass NULL for change_encoding. */
    node *doc = build_fragment_from_input(input, context_tag, encoding,
                                          confidence, NULL);
    if (!doc) {
        fprintf(stderr, "failed to build fragment\n");
        free(input);
        return 1;
    }
    tree_dump_ascii(doc, "ASCII Tree (Fragment)");
    node_free(doc);
    free(input);
    return 0;
}
