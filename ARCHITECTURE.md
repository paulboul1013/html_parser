# html_parser — ARCHITECTURE（現況版）

本文件描述 `html_parser/` 目前的實作架構與進度：包含模組切分、資料結構、主要演算法、CLI 入口與測試方式。內容以「目前程式碼實際行為」為準。

## 1. 現況總覽

已完成（可用且有測資覆蓋）：

- Tokenizer：doctype / start-end tag / attributes / comment / text / EOF / CDATA
- Character references：numeric + named（讀 `entities.tsv`），含部分無 `;` 容錯、numeric 範圍修正表
- RCDATA/RAWTEXT/script：`title/textarea/style/script` 的最小相容狀態
- Tree construction：15 種 insertion modes（含 table 系列 + in table text + in template）+ auto-close + foster parenting
- Active formatting elements（AFE）：reconstruct + Noah's Ark（含 attribute 比對）+ marker（td/th/caption/applet/marquee/object/template）+ scope
- Adoption Agency Algorithm（AAA）：完整 outer/inner loop
- Foreign Content（SVG / MathML）：命名空間切換、breakout tags、Integration Points、元素/屬性大小寫修正、CDATA
- `<form>` element pointer：form-associated 元素自動關聯至所屬 form
- Quirks / Limited-quirks：由 DOCTYPE 推算 `doc_mode`，Quirks 下 `<table>` 不自動關閉 `<p>`
- Fragment parsing：context 影響 tokenizer 初始狀態 + insertion mode；context element **不輸出**
- Node attrs：DOM 節點保留屬性；ASCII dump 會顯示 attrs 和 form_owner
- Serialization：`tree_serialize_html()`（void/rawtext/rcdata/escaping/foreign/template）
- Encoding sniffing：BOM → hint → meta prescan → default UTF-8；39 種編碼、~220 個 label
- CR/LF 正規化、NULL 字元替換

仍缺（進階/完整相容）：

- 完整 parser error recovery（目前偏「盡量產生 token/tree」）
- `<frameset>` 模式（已淘汰，極低優先）
- `<input>` type=hidden 在 table 中的特殊處理
- Select scope

更細的拆解請見 `list.md`。

## 2. 高階資料流

```text
input bytes
  → Encoding Sniffing (encoding.c)
  → CR/LF normalize + NULL replace
  → Tokenizer (tokenizer_next) — 含 CDATA（allow_cdata flag）
  → token stream
  → Tree builder (insertion modes)
      → Foreign content check (foreign.c) — SVG/MathML namespace
      → form_element_pointer tracking
  → DOM-like node tree
  → (optional) ASCII dump / serialize back to HTML
```

核心除錯心智模型：先分辨是「token 切錯」還是「消費 token 建樹錯」。

## 3. 專案結構與入口（CLI）

核心程式碼：

- `src/token.{h,c}`：token 結構與生命週期（`token_init`/`token_free`）
- `src/tokenizer.{h,c}`：tokenization + entity decode + doctype parse + CDATA + line/col error output
- `src/tree.{h,c}`：node tree（含命名空間、form_owner）+ ASCII dump + serializer + tree mutation helpers（AAA 用）
- `src/tree_builder.{h,c}`：tree construction（document + fragment），含 foreign content 整合
- `src/foreign.{h,c}`：Foreign Content 查找表、Integration Points、命名空間感知 scope/special
- `src/encoding.{h,c}`：WHATWG 編碼嗅探、39 種編碼、BOM/meta prescan、iconv/UTF-16

CLI：

- `src/parse_file_demo.c` → `parse_html`：解析「完整文件」，輸出 ASCII tree
- `src/parse_fragment_demo.c` → `parse_fragment_demo`：解析「fragment」，輸出 ASCII tree
- `src/serialize_demo.c` → `serialize_demo`：解析文件後再序列化回 HTML

Makefile 目標：

- `make` / `make parse_html`
- `make parse_fragment_demo`
- `make serialize_demo`
- `make test-html`
- `make test-fragment`
- `make test-serialize`
- `make test-encoding`
- `make test-all`

## 4. 資料結構

### 4.1 Token（`src/token.h`）

- `TOKEN_START_TAG`：`name`、`self_closing`、`attrs[]`
- `TOKEN_END_TAG`：`name`
- `TOKEN_DOCTYPE`：`name`、`public_id`、`system_id`、`force_quirks`
- `TOKEN_COMMENT`：`data`
- `TOKEN_CHARACTER`：`data`
- `TOKEN_EOF`

備註：

- token 的字串皆為 heap 配置
- 每次取到 token 後（例如在 tree builder loop）需要呼叫 `token_free()`

### 4.2 Node（`src/tree.h`）

- Node 類型：`NODE_DOCUMENT/NODE_DOCTYPE/NODE_ELEMENT/NODE_TEXT/NODE_COMMENT`
- 命名空間：`node_namespace` enum（`NS_HTML=0`、`NS_SVG`、`NS_MATHML`）；`NS_HTML=0` 表示 calloc 零初始化即為 HTML
- Element attrs：`node_attr *attrs` + `attr_count`
- Tree 表現：`first_child/last_child/next_sibling/parent`
- Form 關聯：`form_owner`（non-owning pointer to form element）

重要 helper：

- `node_create()` / `node_create_ns()`：建立節點（後者指定命名空間）
- `node_append_child()`
- `node_insert_before()`：foster parenting 在 table 前插入
- `node_remove_child()`、`node_reparent_children()`：AAA 搬移子節點
- `node_free()`：遞迴釋放整棵子樹
- `node_free_shallow()`：fragment context element 本體釋放（children 已移交）

## 5. Tokenizer（`src/tokenizer.c`）

### 5.1 基本 tokenization

- tag name / attr name 轉小寫
- attribute value：雙引號 / 單引號 / 無引號 / boolean attr
- comment：支援 `<!-- ... -->`（未閉合讀到 EOF）
- doctype：支援 `<!DOCTYPE ...>`，並解析 `PUBLIC` / `SYSTEM` id

### 5.2 RCDATA/RAWTEXT/script

- `title/textarea` → `TOKENIZE_RCDATA`
- `style` → `TOKENIZE_RAWTEXT`
- `script` → `TOKENIZE_SCRIPT_DATA`（含 escaped/double-escaped 偵測）

### 5.3 CDATA 區段

- `allow_cdata` flag 由 tree builder 在每次 `tokenizer_next()` 前設定
- Foreign Content（SVG/MathML）中啟用，遇 `<![CDATA[` 解析至 `]]>`
- 輸出為 `TOKEN_CHARACTER`

### 5.4 Parse error 輸出（診斷）

- tokenizer 會直接輸出：`[parse error] line=... col=...: ...`
- 這是診斷輸出，不是 WHATWG 完整 error taxonomy

### 5.5 Named entities

- 來源：`entities.tsv`（`name<TAB>value`）
- lazy load；找不到檔案則回退到內建常用子集
- 查找目前為線性搜尋
- 無分號容錯：下一字元不是英數、也不是 `=` 才解碼

注意：`entities.tsv` 以相對路徑開啟，執行 binary 時的 working directory 會影響是否能載入完整表。

## 6. Tree Construction（`src/tree_builder.c`）

### 6.1 解析狀態

- open elements stack（`node_stack`，固定 256）
- active formatting list（`formatting_list`，固定 64）
- insertion mode（`MODE_*`，15 種）
- `doc_mode`（no-quirks/limited/quirks）：由 DOCTYPE 推算
- `form_element_pointer`：追蹤當前開放的 `<form>` 元素
- `template_mode_stack`：template insertion modes（固定 64）
- `table_text` buffer：in table text 模式的文字收集

### 6.2 insertion modes（已支援）

- `initial` / `before html` / `in head` / `in body`
- `in table` / `in table text` / `in table body` / `in row` / `in cell` / `in caption`
- `in select` / `in select in table`
- `in template`
- `after body` / `after after body`

### 6.3 Auto-close（implied end tags）

- `<p>`：遇到 block-like start tag / 新 `<p>` 會先隱式關閉舊 `<p>`
- `<li>`：新 `<li>` 會關閉舊 `<li>`
- `<dt>/<dd>`：互斥關閉
- `<h1>`-`<h6>`：heading 遇到 heading 自動關閉
- `<option>` / `<optgroup>`：同類開始時先關閉
- table sections/row/cell：同型元素開始時先關閉前一個

### 6.4 Foster parenting

處於 table 相關 mode 時遇到非 table 內容：

- element/text 會插到 `<table>` 的 parent 中、且位於 `<table>` 之前
- in table text 模式：緩衝文字，非空白時整批 foster parent

### 6.5 AFE 與 AAA

支援 14 種 formatting elements：`a`, `b`, `big`, `code`, `em`, `font`, `i`, `nobr`, `s`, `small`, `strike`, `strong`, `tt`, `u`

- start tag：加入 AFE；必要時 reconstruct
- character / 非 formatting start tag：插入前 reconstruct AFE
- Noah's Ark：同 tag + 同 attributes 最多 3 筆，精確比對
- marker：在 `td/th/caption/applet/marquee/object/template` 邊界隔離 AFE
- end tag：若是 formatting element，優先走 AAA（`adoption_agency()`），完整 outer/inner loop

### 6.6 Foreign Content

- `process_in_foreign_content()`：在主 switch 前攔截，處理 foreign content 中的所有 token
- Breakout：~90 個 HTML 元素名令 parser 離開 foreign content
- `<font>` 有 `color`/`face`/`size` 屬性時也 breakout
- SVG 元素名/屬性名大小寫修正（`foreign.c` 查找表 + bsearch）
- MathML 屬性名修正（`definitionurl` → `definitionURL`）
- Integration Points：MathML text（`mi`/`mo`/`mn`/`ms`/`mtext`）、HTML（`foreignObject`/`desc`/`title`）
- SVG `<title>` tokenizer 修正：foreign content 消費後重置 tokenizer 狀態

### 6.7 `<form>` element pointer

- `form_element_pointer` 在 `<form>` start tag 時設定，`</form>` end tag 時清除
- 非 template context 時，form-associated 元素（`input`/`button`/`select`/`textarea`/`fieldset`/`output`/`object`/`img`）的 `form_owner` 自動指向當前 form
- Template context 中不設定 form element pointer（規範行為）
- 巢套 `<form>` 被忽略（parse error）

### 6.8 三個 builder 函式

tree_builder.c 中有三個近乎相同的主迴圈：

1. `build_tree_from_tokens(tokens, count)`：消費預先產生的 token 陣列（`t->name`）
2. `build_tree_from_input(input)`：邊 tokenize 邊建樹（`t.name`），context = NULL
3. `build_fragment_from_input(input, context_tag)`：片段解析（`t.name`），有 context element

**重要**：修改 tree building 邏輯時必須同步更新三個函式。

## 7. Foreign Content（`src/foreign.c`）

獨立模組，提供：

- `is_foreign_breakout_tag(name)`：排序陣列 + bsearch
- `font_has_breakout_attr(attrs, count)`：檢查 color/face/size
- `svg_adjust_element_name(lowered)` / `svg_adjust_attr_name(lowered)`：SVG 大小寫修正
- `mathml_adjust_attr_name(lowered)`：MathML 屬性修正
- `is_mathml_text_integration_point(name)` / `is_html_integration_point(...)`
- `is_special_element_ns(name, ns)` / `is_scoping_element_ns(name, ns)`：命名空間感知

## 8. Fragment parsing

API：`build_fragment_from_input(input, context_tag)`

- context element 只用來決定 tokenizer 初始狀態與 insertion mode
- **context element 本身不會出現在輸出中**；輸出的是它的 children（DocumentFragment 容器）
- `<template>` context 使用 template_mode_stack
- form_element_pointer 在 fragment 中也正確追蹤

## 9. Serialization（`tree_serialize_html`）

- void elements 不輸出 end tag
- `script/style`（raw text）子文字不 escape
- `title/textarea`（rcdata）escape `& < >`
- 一般文字 escape `& < >`
- attribute value escape `&` 與 `"`
- `<template>` 跳過 `content` wrapper
- Foreign 元素無子節點時用 ` />` 自閉合

## 10. Encoding（`src/encoding.c`）

- WHATWG §13.2.3 完整流程：BOM → hint → meta prescan → default UTF-8
- 39 種標準編碼、~220 個 label alias（排序陣列 + bsearch）
- 內建 UTF-16 LE/BE → UTF-8 轉換器（含 surrogate pair）
- iconv fallback（`#ifdef HAVE_ICONV`）
- `replacement` 編碼 → U+FFFD、`x-user-defined` → U+F780-U+F7FF

## 11. 測試策略

- `test-html`：逐一跑 `./parse_html tests/*.html`（50+ 個測試案例）
- `test-fragment`：`tests/run_fragment_tests.sh` 逐案比對 ASCII Tree（PASS/FAIL/KNOWN）
- `test-serialize`：跑 `serialize_demo` 檢查序列化輸出
- `test-encoding`：跑 8 個編碼嗅探測試

## 12. 已知限制（架構層面）

- formatting/scoping elements 清單已完整，包含命名空間感知
- entity lookup 目前是線性搜尋（可優化為 Trie 或 Hash Map）
- `<frameset>` 模式未實作（已淘汰，極低優先）
- `<input>` type=hidden 在 table 中未做特殊處理
- Select scope 未實作
