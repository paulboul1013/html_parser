#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tree_builder.h"
#include "tokenizer.h"
#include "encoding.h"

static char *read_file(const char *path, const char *charset_hint,
                       const char **out_encoding) {
    FILE *fp = fopen(path, "rb");
    long len;
    size_t read_len;
    char *raw;
    char *buf;
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (len < 0) {
        fclose(fp);
        return NULL;
    }
    raw = (char *)malloc((size_t)len + 1);
    if (!raw) {
        fclose(fp);
        return NULL;
    }
    read_len = fread(raw, 1, (size_t)len, fp);
    fclose(fp);
    /* Encoding sniffing and conversion to UTF-8 */
    encoding_result enc = encoding_sniff_and_convert(
        (const unsigned char *)raw, read_len, charset_hint);
    free(raw);
    if (!enc.data) return NULL;
    /* Return the detected encoding to the caller */
    if (out_encoding)
        *out_encoding = enc.encoding;
    /* Replace U+0000 NULL bytes with U+FFFD before tokenization */
    buf = tokenizer_replace_nulls(enc.data, enc.len);
    free(enc.data);
    return buf;
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
    const char *context = argv[arg_idx];
    const char *path = argv[arg_idx + 1];
    const char *encoding = NULL;
    char *input = read_file(path, charset_hint, &encoding);
    if (!input) {
        fprintf(stderr, "failed to read %s\n", path);
        return 1;
    }
    node *doc = build_fragment_from_input(input, context, encoding);
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
