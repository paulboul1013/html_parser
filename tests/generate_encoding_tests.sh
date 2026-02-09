#!/bin/bash
# Generate binary encoding test files for the HTML parser.
# Run from the repository root: bash tests/generate_encoding_tests.sh

set -e
DIR="$(dirname "$0")"

echo "Generating encoding test files..."

# 1. UTF-8 with BOM
printf '\xEF\xBB\xBF<!DOCTYPE html><html><head><title>UTF-8 BOM</title></head><body><p>Hello BOM</p></body></html>' \
    > "$DIR/encoding_utf8_bom.html"
echo "  Created encoding_utf8_bom.html"

# 2. UTF-16 LE with BOM
# Use iconv to convert, then prepend BOM
printf '<!DOCTYPE html><html><head><title>UTF-16LE</title></head><body><p>Hello UTF-16 LE</p></body></html>' | \
    iconv -f UTF-8 -t UTF-16LE > "$DIR/encoding_utf16le_bom.tmp"
printf '\xFF\xFE' > "$DIR/encoding_utf16le_bom.html"
cat "$DIR/encoding_utf16le_bom.tmp" >> "$DIR/encoding_utf16le_bom.html"
rm -f "$DIR/encoding_utf16le_bom.tmp"
echo "  Created encoding_utf16le_bom.html"

# 3. UTF-16 BE with BOM
printf '<!DOCTYPE html><html><head><title>UTF-16BE</title></head><body><p>Hello UTF-16 BE</p></body></html>' | \
    iconv -f UTF-8 -t UTF-16BE > "$DIR/encoding_utf16be_bom.tmp"
printf '\xFE\xFF' > "$DIR/encoding_utf16be_bom.html"
cat "$DIR/encoding_utf16be_bom.tmp" >> "$DIR/encoding_utf16be_bom.html"
rm -f "$DIR/encoding_utf16be_bom.tmp"
echo "  Created encoding_utf16be_bom.html"

# 4. windows-1252 with <meta charset>
# Use \xe9 for e-acute (U+00E9 in windows-1252)
printf '<html><head><meta charset="windows-1252"><title>Win1252</title></head><body><p>caf\xe9</p></body></html>' \
    > "$DIR/encoding_meta_charset.html"
echo "  Created encoding_meta_charset.html"

# 5. windows-1252 with <meta http-equiv="Content-Type">
printf '<html><head><meta http-equiv="Content-Type" content="text/html; charset=windows-1252"><title>Win1252 HE</title></head><body><p>r\xe9sum\xe9</p></body></html>' \
    > "$DIR/encoding_meta_httpequiv.html"
echo "  Created encoding_meta_httpequiv.html"

# 6. Shift_JIS with <meta charset>
# "Hello" in Japanese katakana: ハロー (0x83 0x6E 0x83 0x8D 0x81 0x5B in Shift_JIS)
printf '<html><head><meta charset="shift_jis"><title>SJIS</title></head><body><p>' > "$DIR/encoding_shift_jis.html"
printf '\x83\x6E\x83\x8D\x81\x5B' >> "$DIR/encoding_shift_jis.html"
printf '</p></body></html>' >> "$DIR/encoding_shift_jis.html"
echo "  Created encoding_shift_jis.html"

# 7. GBK with <meta charset>
# "Hello" in Chinese: 你好 (0xC4 0xE3 0xBA 0xC3 in GBK)
printf '<html><head><meta charset="gbk"><title>GBK</title></head><body><p>' > "$DIR/encoding_gbk.html"
printf '\xC4\xE3\xBA\xC3' >> "$DIR/encoding_gbk.html"
printf '</p></body></html>' >> "$DIR/encoding_gbk.html"
echo "  Created encoding_gbk.html"

# 8. Plain UTF-8 (no BOM, no meta) — default fallback
printf '<!DOCTYPE html><html><head><title>Default UTF-8</title></head><body><p>Simple ASCII</p></body></html>' \
    > "$DIR/encoding_default_utf8.html"
echo "  Created encoding_default_utf8.html"

echo "Done. All encoding test files generated."
