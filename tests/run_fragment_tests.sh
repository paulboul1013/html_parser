#!/bin/bash
# tests/run_fragment_tests.sh
# ---------------------------------------------------------------
# Fragment-parsing regression suite.
# Compares parse_fragment_demo output against WHATWG-spec-correct
# expected trees.
#
#   PASS  – output matches expected
#   FAIL  – output differs (bug to fix)
#   KNOWN – feature not yet implemented; failure is noted but
#           does NOT count as a suite failure
#
# Usage:  bash tests/run_fragment_tests.sh [binary]
# Default binary: ./parse_fragment_demo
# ---------------------------------------------------------------
set -uo pipefail

BINARY="${1:-./parse_fragment_demo}"
PASS=0; FAIL=0; KNOWN=0

if [ ! -x "$BINARY" ]; then
    echo "ERROR: $BINARY not found or not executable"
    echo "       run 'make parse_fragment_demo' first"
    exit 1
fi

# run <label> <context> <file> <kind>
#   kind  = "pass"  -> failure increments FAIL
#   kind  = "known" -> failure increments KNOWN only
#   Expected tree is read from stdin via heredoc.
run() {
    local label="$1" context="$2" file="$3" kind="$4"
    local expected actual

    expected=$(cat)                                       # heredoc stdin
    actual=$("$BINARY" "$context" "$file" 2>/dev/null) || true

    if [ "$actual" = "$expected" ]; then
        printf "  PASS  %s\n" "$label"
        PASS=$((PASS + 1))
    elif [ "$kind" = "known" ]; then
        printf "  KNOWN %s\n" "$label"
        KNOWN=$((KNOWN + 1))
    else
        printf "  FAIL  %s\n" "$label"
        FAIL=$((FAIL + 1))
        diff <(printf '%s\n' "$expected") <(printf '%s\n' "$actual") | sed 's/^/          /' || true
    fi
}

echo
echo "  Fragment parsing tests"
echo "  =============================="

# ----------------------------------------------------------------
# 01  <tr>/<td> in "in body" mode
#     Per WHATWG spec these tags are parse-errors and must be
#     ignored.  Only the bare text survives.
# ----------------------------------------------------------------
run "01  tr/td in body -> ignored" \
    div tests/frag_01_table_in_div.html pass <<'EOF'
ASCII Tree (Fragment)
DOCUMENT
\-- TEXT data="A"
EOF

# ----------------------------------------------------------------
# 02a <td> directly in "in table" context
#     Spec requires implicit <tbody> then implicit <tr> before
#     the cell is inserted.
# ----------------------------------------------------------------
run "02a <td> in table context -> tbody+tr implied" \
    table tests/frag_02_td_x.html pass <<'EOF'
ASCII Tree (Fragment)
DOCUMENT
\-- ELEMENT name="tbody"
    \-- ELEMENT name="tr"
        \-- ELEMENT name="td"
            \-- TEXT data="X"
EOF

# ----------------------------------------------------------------
# 02b <td> in "in row" context — direct insert, no wrapping
# ----------------------------------------------------------------
run "02b <td> in tr context -> direct" \
    tr tests/frag_02_td_x.html pass <<'EOF'
ASCII Tree (Fragment)
DOCUMENT
\-- ELEMENT name="td"
    \-- TEXT data="X"
EOF

# ----------------------------------------------------------------
# 03  Foster parenting
#     <table> must switch insertion mode to "in table".
#     Text "1" and element <p> are then foster-parented before
#     the <table>.  Text "2" goes inside the foster-parented <p>.
# ----------------------------------------------------------------
run "03  foster parenting (text + p inside table)" \
    div tests/frag_03_foster_in_div.html pass <<'EOF'
ASCII Tree (Fragment)
DOCUMENT
|-- TEXT data="1"
|-- ELEMENT name="p"
|   \-- TEXT data="2"
|-- ELEMENT name="table"
\-- TEXT data="3"
EOF

# ----------------------------------------------------------------
# 04  Formatting misnest: <b><i>X</b>Y</i>
#     </b> triggers Adoption Agency Algorithm.  No furthest block
#     is found, so formatting elements are popped and the active
#     list is adjusted.  "Y" then triggers reconstruction of <i>.
# ----------------------------------------------------------------
run "04  formatting misnest <b><i>X</b>Y</i>" \
    div tests/frag_04_formatting_misnest.html pass <<'EOF'
ASCII Tree (Fragment)
DOCUMENT
|-- ELEMENT name="b"
|   \-- ELEMENT name="i"
|       \-- TEXT data="X"
\-- ELEMENT name="i"
    \-- TEXT data="Y"
EOF

# ----------------------------------------------------------------
# 05  <p> closed by <table>; implicit tbody wraps tr/td
# ----------------------------------------------------------------
run "05  <p> closed by <table>, tbody implied" \
    div tests/frag_05_p_table_close.html pass <<'EOF'
ASCII Tree (Fragment)
DOCUMENT
|-- ELEMENT name="p"
|   \-- TEXT data="A"
|-- ELEMENT name="table"
|   \-- ELEMENT name="tbody"
|       \-- ELEMENT name="tr"
|           \-- ELEMENT name="td"
|               \-- TEXT data="B"
\-- TEXT data="C"
EOF

# ----------------------------------------------------------------
# 06  <option> auto-close
#     In "in select" mode a second <option> must close the first.
# ----------------------------------------------------------------
run "06  <option> auto-close in select" \
    select tests/frag_06_option_autoclose.html pass <<'EOF'
ASCII Tree (Fragment)
DOCUMENT
|-- ELEMENT name="option"
|   \-- TEXT data="A"
\-- ELEMENT name="option"
    \-- TEXT data="B"
EOF

# ----------------------------------------------------------------
# 07  <p> inside <button>
#     <p> is not allowed inside <button> per the content model,
#     but tree construction does not validate content models —
#     it simply inserts <p> as a child of the current node.
#     </button> implicitly closes <p> via generated end tags.
# ----------------------------------------------------------------
run "07  <p> inside <button> (valid per spec)" \
    div tests/frag_07_button_p.html pass <<'EOF'
ASCII Tree (Fragment)
DOCUMENT
|-- ELEMENT name="button"
|   \-- ELEMENT name="p"
|       \-- TEXT data="X"
\-- TEXT data="Y"
EOF

# ----------------------------------------------------------------
# 08  Script data state
#     Tokenizer must enter SCRIPT_DATA on <script>.
#     HTML is NOT string-aware: the first </script> always closes
#     the element regardless of surrounding JS quotes.
#     NOTE: tree_dump_ascii does not escape quotes in data, so the
#     output contains unescaped " characters inside data="...".
# ----------------------------------------------------------------
run "08  script data state" \
    div tests/frag_08_script_data.html pass <<'EOF'
ASCII Tree (Fragment)
DOCUMENT
|-- ELEMENT name="script"
|   \-- TEXT data="var x=""
\-- TEXT data="";"
EOF

# ----------------------------------------------------------------
# 09  Textarea RCDATA
#     Tags inside <textarea> are NOT parsed; they appear as
#     literal text.  Character references ARE decoded (none here).
# ----------------------------------------------------------------
run "09  textarea RCDATA (tags become text)" \
    div tests/frag_09_textarea_rcdata.html pass <<'EOF'
ASCII Tree (Fragment)
DOCUMENT
\-- ELEMENT name="textarea"
    \-- TEXT data="<b>X</b>"
EOF

# ----------------------------------------------------------------
# 10  <template> content DocumentFragment
#     Per spec the children of <template> live inside a
#     DocumentFragment accessed via .content.
#     This parser does not yet model template content ->  KNOWN.
# ----------------------------------------------------------------
run "10  template content DocumentFragment" \
    div tests/frag_10_template.html pass <<'EOF'
ASCII Tree (Fragment)
DOCUMENT
|-- ELEMENT name="template"
|   \-- ELEMENT name="content"
|       \-- ELEMENT name="p"
|           \-- TEXT data="X"
\-- TEXT data="Y"
EOF

# ----------------------------------------------------------------
# 11  Void element <meta>
#     <meta> is void and must NOT be pushed onto the open-element
#     stack.  <title> is therefore a sibling, not a child.
# ----------------------------------------------------------------
run "11  void element <meta> not pushed to stack" \
    div tests/frag_11_head_in_body.html pass <<'EOF'
ASCII Tree (Fragment)
DOCUMENT
|-- ELEMENT name="meta" [charset="utf-8"]
\-- ELEMENT name="title"
    \-- TEXT data="X"
EOF

# ----------------------------------------------------------------
# 12  Mixed scope: table + formatting + p
#     <b> is opened inside <td>.  </table> while still in
#     IN_CELL triggers close_cell, which clears active formatting
#     back to the td-level marker.  "Z" therefore appears as bare
#     text — the <b> does NOT leak out of the table.
#     <p> stays inside <td><b> because IN_CELL delegates to IN_BODY
#     (no foster parenting for <p>).
# ----------------------------------------------------------------
run "12  mixed scope (table + fmt + p)" \
    div tests/frag_12_mixed_scope.html pass <<'EOF'
ASCII Tree (Fragment)
DOCUMENT
|-- ELEMENT name="table"
|   \-- ELEMENT name="tbody"
|       \-- ELEMENT name="tr"
|           \-- ELEMENT name="td"
|               \-- ELEMENT name="b"
|                   |-- TEXT data="X"
|                   \-- ELEMENT name="p"
|                       \-- TEXT data="Y"
\-- TEXT data="Z"
EOF

# ----------------------------------------------------------------
# summary
# ----------------------------------------------------------------
echo "  =============================="
printf "  Total: %d   Pass: %d   Fail: %d   Known: %d\n" \
    $((PASS + FAIL + KNOWN)) "$PASS" "$FAIL" "$KNOWN"
echo "  =============================="
echo

[ "$FAIL" -eq 0 ] && exit 0 || exit 1
