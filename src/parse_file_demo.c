#include <stdio.h>
#include <stdlib.h>

#include "tree_builder.h"

static char *read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    long len;
    size_t read_len;
    char *buf;
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (len < 0) {
        fclose(fp);
        return NULL;
    }
    buf = (char *)malloc((size_t)len + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    read_len = fread(buf, 1, (size_t)len, fp);
    buf[read_len] = '\0';
    fclose(fp);
    return buf;
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "tests/sample.html";
    char *input = read_file(path);
    if (!input) {
        fprintf(stderr, "failed to read %s\n", path);
        return 1;
    }
    node *doc = build_tree_from_input(input);
    if (!doc) {
        fprintf(stderr, "failed to build tree\n");
        free(input);
        return 1;
    }
    tree_dump_ascii(doc, "ASCII Tree (File)");
    node_free(doc);
    free(input);
    return 0;
}
