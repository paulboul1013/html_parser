CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -g

SRC = src/token.c src/tokenizer.c src/tree.c src/tree_builder.c

all: parse_html

parse_html: $(SRC) src/parse_file_demo.c
	$(CC) $(CFLAGS) -Isrc $(SRC) src/parse_file_demo.c -o $@

test-html: parse_html
	./parse_html tests/sample.html
	./parse_html tests/autoclose.html
	./parse_html tests/charrefs.html
	./parse_html tests/parse_errors.html
	./parse_html tests/rcdata_rawtext_script.html
	./parse_html tests/charrefs_more.html
	./parse_html tests/table_full.html
	./parse_html tests/autoclose_extended.html
	./parse_html tests/formatting_rebuild.html
	./parse_html tests/script_escaped.html

clean:
	rm -f parse_html
