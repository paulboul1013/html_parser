CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -g

SRC = src/token.c src/tokenizer.c src/tree.c src/tree_builder.c

all: parse_html

parse_html: $(SRC) src/parse_file_demo.c
	$(CC) $(CFLAGS) -Isrc $(SRC) src/parse_file_demo.c -o $@

parse_fragment_demo: $(SRC) src/parse_fragment_demo.c
	$(CC) $(CFLAGS) -Isrc $(SRC) src/parse_fragment_demo.c -o $@

serialize_demo: $(SRC) src/serialize_demo.c
	$(CC) $(CFLAGS) -Isrc $(SRC) src/serialize_demo.c -o $@

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
	./parse_html tests/table_caption.html
	./parse_html tests/select_in_table.html
	./parse_html tests/after_body.html
	./parse_html tests/foster_parenting.html
	./parse_html tests/formatting_scope.html
	./parse_html tests/formatting_misnest.html
	./parse_html tests/quirks_p_table.html
	./parse_html tests/no_quirks_p_table.html
	./parse_html tests/doctype_modes_quirks.html
	./parse_html tests/doctype_modes_standards.html
	./parse_html tests/doctype_modes_limited.html
	./parse_html tests/attrs_basic.html
	./parse_html tests/attrs_void.html
	./parse_html tests/attrs_edge.html
	./parse_html tests/aaa_basic.html
	./parse_html tests/big_test.html
	./parse_html tests/scoping.html

test-fragment: parse_fragment_demo
	bash tests/run_fragment_tests.sh ./parse_fragment_demo

test-serialize: serialize_demo
	@echo "=== Serialization: attrs_basic.html ==="
	@./serialize_demo tests/attrs_basic.html
	@echo ""
	@echo "=== Serialization: attrs_void.html ==="
	@./serialize_demo tests/attrs_void.html
	@echo ""
	@echo "=== Serialization: rcdata_rawtext_script.html ==="
	@./serialize_demo tests/rcdata_rawtext_script.html

test-all: test-html test-fragment

clean:
	rm -f parse_html parse_fragment_demo serialize_demo
