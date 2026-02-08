#include <stdio.h>
#include <stdlib.h>

#include "tree_builder.h"
#include "tokenizer.h"

static char *read_file(const char *path) {
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
    /* Replace U+0000 NULL bytes with U+FFFD before tokenization */
    buf = tokenizer_replace_nulls(raw, read_len);
    free(raw);
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <context-tag> <file>\n", argv[0]);
        return 1;
    }
    const char *context = argv[1];
    const char *path = argv[2];
    char *input = read_file(path);
    if (!input) {
        fprintf(stderr, "failed to read %s\n", path);
        return 1;
    }
    node *doc = build_fragment_from_input(input, context);
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
