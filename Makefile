CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -g -DHAVE_ICONV

SRC = src/token.c src/tokenizer.c src/tree.c src/tree_builder.c src/encoding.c src/foreign.c

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
	./parse_html tests/implied_end_tags.html
	./parse_html tests/quirks_table.html
	./parse_html tests/quirks_table_quirks.html
	./parse_html tests/null_replacement.html
	./parse_html tests/charref_attr_vs_text.html
	./parse_html tests/svg_basic.html
	./parse_html tests/svg_case_correction.html
	./parse_html tests/svg_attr_correction.html
	./parse_html tests/svg_breakout.html
	./parse_html tests/svg_font_breakout.html
	./parse_html tests/svg_integration_point.html
	./parse_html tests/mathml_basic.html
	./parse_html tests/svg_cdata.html
	./parse_html tests/svg_nested.html
	./parse_html tests/template_document.html
	./parse_html tests/heading_autoclose.html
	./parse_html tests/applet_marker.html
	./parse_html tests/crlf_normalization.html

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

test-encoding: parse_html
	@echo "=== Encoding: UTF-8 BOM ==="
	./parse_html tests/encoding_utf8_bom.html
	@echo "=== Encoding: UTF-16 LE BOM ==="
	./parse_html tests/encoding_utf16le_bom.html
	@echo "=== Encoding: UTF-16 BE BOM ==="
	./parse_html tests/encoding_utf16be_bom.html
	@echo "=== Encoding: meta charset (windows-1252) ==="
	./parse_html tests/encoding_meta_charset.html
	@echo "=== Encoding: meta http-equiv (windows-1252) ==="
	./parse_html tests/encoding_meta_httpequiv.html
	@echo "=== Encoding: Shift_JIS ==="
	./parse_html tests/encoding_shift_jis.html
	@echo "=== Encoding: GBK ==="
	./parse_html tests/encoding_gbk.html
	@echo "=== Encoding: default UTF-8 ==="
	./parse_html tests/encoding_default_utf8.html

test-all: test-html test-fragment test-encoding

clean:
	rm -f parse_html parse_fragment_demo serialize_demo
