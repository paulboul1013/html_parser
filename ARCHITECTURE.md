# html_parser — ARCHITECTURE

本文件描述 `html_parser/` 的實作架構：模組切分、資料結構、主要演算法、CLI 入口與測試方式。內容以程式碼實際行為為準。

## 1. 現況總覽

本專案是一個完整的 WHATWG HTML5 Parser，功能覆蓋與主流 parser（html5lib、html5ever、Gumbo）齊平。已實作：

- **Tokenizer**：完整 80 種狀態機，含 DOCTYPE / start-end tag / attributes / comment / text / EOF / CDATA / PLAINTEXT / Script Data Escaped & Double Escaped / RCDATA & RAWTEXT 完整子狀態
- **Character References**：numeric + named（完整 2,231 實體 `entities.tsv`），含無分號容錯、numeric 範圍修正表、noncharacter/surrogate/control 偵測
- **Tree Construction**：20 種 insertion mode（含 in table text、in head noscript），auto-close、foster parenting、generate implied end tags（含 thoroughly 版本）、stop parsing
- **Active Formatting Elements（AFE）**：reconstruct + Noah's Ark（含 attribute 比對）+ marker（td/th/caption/applet/marquee/object/template）+ 5 種 scope
- **Adoption Agency Algorithm（AAA）**：完整 outer/inner loop（各 8 次上限）
- **Foreign Content（SVG / MathML）**：命名空間切換、breakout tags、Integration Points、元素/屬性大小寫修正、CDATA
- **`<form>` element pointer**：form-associated 元素自動關聯至所屬 form
- **`<body>` / `<html>` 重複屬性合併**（WHATWG §13.2.6.4.7）
- **Quirks / Limited-quirks / No-quirks**：由 DOCTYPE 推算，已套用於 `<table>` 不自動關閉 `<p>` 規則
- **Fragment parsing**：context 影響 tokenizer 初始狀態 + insertion mode；context element 不輸出；encoding 繼承
- **Serialization**：`tree_serialize_html()`（void/rawtext/rcdata/escaping/foreign/template）
- **Encoding sniffing**：BOM → hint → meta prescan → default UTF-8；39 種編碼、~220 個 label；內建 UTF-16 和 ISO-2022-JP 解碼器；re-encoding 機制
- **CR/LF 正規化、NULL 字元替換**

唯一未實作：`<frameset>` 模式（3 種，已淘汰）。

更細的拆解請見 `list.md`。

## 2. 高階資料流

```text
input bytes
  → Encoding Sniffing (encoding.c)
      BOM → transport hint → meta prescan → default UTF-8
      內建: UTF-16, ISO-2022-JP | iconv fallback: 其他編碼
      → re-encoding check (TENTATIVE 時偵測 meta charset 衝突)
  → CR/LF normalize + NULL replace (U+0000 → U+FFFD)
  → Tokenizer (tokenizer_next) — 80 種狀態，含 CDATA（allow_cdata flag）
  → token stream
  → Tree builder (insertion modes)
      → Foreign content check (foreign.c) — SVG/MathML namespace
      → form_element_pointer tracking
      → merge_attrs (duplicate <html>/<body>)
  → DOM-like node tree
  → (optional) ASCII dump / serialize back to HTML
```

核心除錯心智模型：先分辨是「token 切錯」還是「消費 token 建樹錯」。

## 3. 專案結構與入口（CLI）

核心程式碼（~8,800 行）：

- `src/token.{h,c}`：token 結構與生命週期（`token_init`/`token_free`）
- `src/tokenizer.{h,c}`：tokenization + entity decode + doctype parse + CDATA + line/col error output
- `src/tree.{h,c}`：node tree（含命名空間、form_owner）+ ASCII dump + serializer + tree mutation helpers（AAA 用）
- `src/tree_builder.{h,c}`：tree construction（document + fragment），含 foreign content 整合
- `src/foreign.{h,c}`：Foreign Content 查找表、Integration Points、命名空間感知 scope/special
- `src/encoding.{h,c}`：WHATWG 編碼嗅探、39 種編碼、BOM/meta prescan、iconv/UTF-16/ISO-2022-JP
- `src/jis0208_table.h`：JIS X 0208 pointer → Unicode codepoint 查找表

CLI：

- `src/parse_file_demo.c` → `parse_html`：解析完整文件，輸出 ASCII tree
- `src/parse_fragment_demo.c` → `parse_fragment_demo`：解析 fragment，輸出 ASCII tree
- `src/serialize_demo.c` → `serialize_demo`：解析文件後再序列化回 HTML

Makefile 目標：

- `make` / `make parse_html`
- `make parse_fragment_demo`
- `make serialize_demo`
- `make test-html` / `make test-fragment` / `make test-serialize` / `make test-encoding`
- `make test-all`

## 4. 資料結構

### 4.1 Token（`src/token.h`）

- `TOKEN_START_TAG`：`name`、`self_closing`、`attrs[]`
- `TOKEN_END_TAG`：`name`
- `TOKEN_DOCTYPE`：`name`、`public_id`、`system_id`、`force_quirks`
- `TOKEN_COMMENT`：`data`
- `TOKEN_CHARACTER`：`data`
- `TOKEN_EOF`

備註：token 的字串皆為 heap 配置，每次取到 token 後需呼叫 `token_free()`。

### 4.2 Node（`include/tree.h`）

- Node 類型：`NODE_DOCUMENT` / `NODE_DOCTYPE` / `NODE_ELEMENT` / `NODE_TEXT` / `NODE_COMMENT`
- 命名空間：`node_namespace` enum（`NS_HTML=0`、`NS_SVG`、`NS_MATHML`）；`NS_HTML=0` 表示 calloc 零初始化即為 HTML
- Element attrs：`node_attr *attrs` + `attr_count`
- Tree 表現：`first_child` / `last_child` / `next_sibling` / `parent`
- Form 關聯：`form_owner`（non-owning pointer to form element）

重要 helper：

- `node_create()` / `node_create_ns()`：建立節點（後者指定命名空間）
- `node_append_child()`
- `node_insert_before()`：foster parenting 在 table 前插入
- `node_remove_child()`、`node_reparent_children()`：AAA 搬移子節點
- `node_free()`：遞迴釋放整棵子樹
- `node_free_shallow()`：fragment context element 本體釋放（children 已移交）

## 5. Tokenizer（`src/tokenizer.c`）

### 5.1 狀態機

完整實作 WHATWG 定義的 80 種狀態，包含：

- 基本：Data / Tag Open / End Tag / Tag Name / Attribute（Before/Name/After/Value） / Self-closing
- Comment：10 種子狀態（含 less-than sign bang 系列）
- DOCTYPE：12 種子狀態（含 PUBLIC/SYSTEM identifier）
- Character Reference：9 種子狀態（Named / Numeric / Hex / Decimal）
- CDATA：3 種子狀態
- Bogus Comment / Bogus DOCTYPE

### 5.2 RCDATA / RAWTEXT / Script Data / PLAINTEXT

- `title` / `textarea` → RCDATA（6 個子狀態，`process_rcdata_rawtext()` 逐字元狀態機）
- `style` → RAWTEXT（6 個子狀態，共用 `process_rcdata_rawtext()`）
- `script` → Script Data（18 個子狀態，`process_script_data()` 逐字元狀態機，含 Escaped / Double Escaped）
- `plaintext` → PLAINTEXT（進入後永不離開）

### 5.3 CDATA 區段

- `allow_cdata` flag 由 tree builder 在每次 `tokenizer_next()` 前設定
- Foreign Content（SVG/MathML）中啟用，遇 `<![CDATA[` 解析至 `]]>`
- 輸出為 `TOKEN_CHARACTER`

### 5.4 Named entities

- 來源：`entities.tsv`（完整 WHATWG 2,231 條）
- Lazy load；找不到檔案則回退到內建 ~30 個常用實體
- 無分號容錯：下一字元不是英數且不是 `=` 才解碼
- Attribute context 差異處理

### 5.5 Parse error 輸出

- `[parse error] line=... col=...: ...` 格式
- Tokenizer 端：~20 種 parse error
- Tree builder 端：`tree_parse_error()` ~40 種
- 以 `HTMLPARSER_PARSE_ERRORS=1` 環境變數啟用

## 6. Tree Construction（`src/tree_builder.c`）

### 6.1 解析狀態

- open elements stack（`node_stack`，固定 256）
- active formatting list（`formatting_list`，固定 64）
- insertion mode（`MODE_*`，20 種，含 frameset 已淘汰的 3 種外）
- `doc_mode`（no-quirks / limited / quirks）：由 DOCTYPE 推算
- `form_element_pointer`：追蹤當前開放的 `<form>` 元素
- `template_mode_stack`：template insertion modes（固定 64）
- `table_text` buffer：in table text 模式的文字收集

### 6.2 Insertion Modes（已支援 20 種）

- `initial` / `before html` / `in head` / `in head noscript` / `in body`
- `text`（generic RCDATA/RAWTEXT content mode）
- `in table` / `in table text` / `in table body` / `in row` / `in cell` / `in caption`
- `in select` / `in select in table`
- `in template`
- `after body` / `after after body`

合併處理（功能等效）：`before head` → `in head`、`after head` → `in head` → `in body`、`in column group` → `in table`

### 6.3 Auto-close（implied end tags）

- `<p>`：遇到 block-like start tag / 新 `<p>` 會先隱式關閉舊 `<p>`
- `<li>`：新 `<li>` 會關閉舊 `<li>`
- `<dt>` / `<dd>`：互斥關閉
- `<h1>`-`<h6>`：heading 遇到 heading 自動關閉
- `<option>` / `<optgroup>`：同類開始時先關閉
- Table sections / row / cell：同型元素開始時先關閉前一個
- Generate Implied End Tags：`dd`, `dt`, `li`, `optgroup`, `option`, `p`, `rb`, `rp`, `rt`, `rtc`
- Generate All Implied End Tags Thoroughly：額外含 `caption`, `colgroup`, `tbody`, `td`, `tfoot`, `th`, `thead`, `tr`

### 6.4 Foster parenting

處於 table 相關 mode 時遇到非 table 內容：

- element/text 會插到 `<table>` 的 parent 中、且位於 `<table>` 之前
- In table text 模式：緩衝文字，非空白時整批 foster parent
- `<input type=hidden>`：直接插入 table，不 foster parent

### 6.5 AFE 與 AAA

支援 14 種 formatting elements：`a`, `b`, `big`, `code`, `em`, `font`, `i`, `nobr`, `s`, `small`, `strike`, `strong`, `tt`, `u`

- start tag：加入 AFE；必要時 reconstruct
- character / 非 formatting start tag：插入前 reconstruct AFE
- Noah's Ark：同 tag + 同 attributes 最多 3 筆，精確比對
- Marker：在 `td` / `th` / `caption` / `applet` / `marquee` / `object` / `template` 邊界隔離 AFE
- End tag：若是 formatting element，走 AAA（`adoption_agency()`），完整 outer/inner loop（各 8 次上限）

### 6.6 Foreign Content

- `process_in_foreign_content()`：在主 switch 前攔截，處理 foreign content 中的所有 token
- Breakout：~90 個 HTML 元素名令 parser 離開 foreign content
- `<font>` 有 `color` / `face` / `size` 屬性時也 breakout
- SVG 元素名/屬性名大小寫修正（`foreign.c` 查找表 + bsearch）
- MathML 屬性名修正（`definitionurl` → `definitionURL`）
- Integration Points：MathML text（`mi`/`mo`/`mn`/`ms`/`mtext`）、HTML（`foreignObject`/`desc`/`title`）
- SVG `<title>` tokenizer 修正：foreign content 消費後重置 tokenizer 狀態

### 6.7 `<form>` element pointer

- `form_element_pointer` 在 `<form>` start tag 時設定，`</form>` end tag 時清除
- 非 template context 時，form-associated 元素（`input`/`button`/`select`/`textarea`/`fieldset`/`output`/`object`/`img`）的 `form_owner` 自動指向當前 form
- Template context 中不設定 form element pointer（規範行為）
- 巢套 `<form>` 被忽略（parse error）

### 6.8 `<body>` / `<html>` 屬性合併

- 重複 `<html>` start tag：`merge_attrs()` 將 token 屬性合併至既有 `<html>` 元素（不覆蓋已存在的屬性）
- 重複 `<body>` start tag：同理合併至既有 `<body>` 元素
- Fragment parsing 中不執行合併（規範行為）

### 6.9 三個 builder 函式

tree_builder.c 中有三個近乎相同的主迴圈：

1. `build_tree_from_tokens(tokens, count)`：消費預先產生的 token 陣列（`t->name`）
2. `build_tree_from_input(input)`：邊 tokenize 邊建樹（`t.name`），context = NULL
3. `build_fragment_from_input(input, context_tag, encoding, confidence, change_encoding)`：片段解析（`t.name`），有 context element

**重要**：修改 tree building 邏輯時必須同步更新三個函式。

## 7. Foreign Content（`src/foreign.c`）

獨立模組，提供：

- `is_foreign_breakout_tag(name)`：排序陣列 + bsearch
- `font_has_breakout_attr(attrs, count)`：檢查 color/face/size
- `svg_adjust_element_name(lowered)` / `svg_adjust_attr_name(lowered)`：SVG 大小寫修正
- `mathml_adjust_attr_name(lowered)`：MathML 屬性修正
- `is_mathml_text_integration_point(name)` / `is_html_integration_point(...)`
- `is_special_element_ns(name, ns)` / `is_scoping_element_ns(name, ns)`：命名空間感知

## 8. Fragment Parsing

API：`build_fragment_from_input(input, context_tag, encoding, confidence, change_encoding)`

- Context element 決定 tokenizer 初始狀態與 insertion mode
- Context element 本身不出現在輸出中；輸出的是它的 children（DocumentFragment 容器）
- `<template>` context 使用 template_mode_stack
- form_element_pointer 在 fragment 中也正確追蹤
- Encoding 繼承：fragment 接受 context document 的 encoding + confidence（WHATWG §14.4 step 5）

## 9. Serialization（`tree_serialize_html`）

- Void elements 不輸出 end tag（14 個：`area`, `base`, `br`, `col`, `embed`, `hr`, `img`, `input`, `link`, `meta`, `param`, `source`, `track`, `wbr`）
- `script` / `style`（raw text）子文字不 escape
- `title` / `textarea`（RCDATA）escape `& < >`
- 一般文字 escape `& < >`
- Attribute value escape `&` 與 `"`
- `<template>` 跳過 `content` wrapper
- Foreign 元素無子節點時用 ` />` 自閉合

## 10. Encoding（`src/encoding.c`）

- WHATWG §13.2.3 完整流程：BOM → hint → meta prescan → default UTF-8
- 39 種標準編碼、~220 個 label alias（排序陣列 + bsearch）
- 內建轉換器（無需 iconv）：
  - UTF-16 LE/BE → UTF-8（含 surrogate pair）
  - ISO-2022-JP → UTF-8（WHATWG §15.2 狀態機：ASCII/Roman/Katakana/Lead/Trail/Escape，含 JIS X 0208 查找表、output flag 安全機制）
  - `x-user-defined`（0x80-0xFF → U+F780-U+F7FF）
  - `replacement`（→ U+FFFD）
- iconv fallback（`#ifdef HAVE_ICONV`）：其他編碼
- Encoding confidence：certain / tentative / irrelevant
- Re-encoding（§13.2.3.5）：TENTATIVE 時偵測 meta charset 與初始編碼衝突，觸發重新解碼 + 重新解析

## 11. 測試策略

93 個測試 HTML 檔案，涵蓋所有主要功能：

- `test-html`：完整文件解析測試
- `test-fragment`：`tests/run_fragment_tests.sh` 逐案比對 ASCII Tree（14 個測試，PASS/FAIL/KNOWN）
- `test-serialize`：序列化輸出驗證
- `test-encoding`：11 個編碼嗅探測試（UTF-8 BOM、UTF-16 LE/BE、meta charset、Shift_JIS、GBK、ISO-2022-JP、re-encoding、BOM vs meta）

## 12. 已知限制（架構層面）

- Entity lookup 目前是線性搜尋（可優化為 Trie 或 Hash Map）
- `<frameset>` 模式未實作（已淘汰，現代網頁不使用）
- Open elements stack 固定 256、formatting list 固定 64（足夠處理所有真實網頁）
