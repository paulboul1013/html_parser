# html_parser — ARCHITECTURE（現況版）

本文件描述 `html_parser/` 目前的實作架構與進度：包含模組切分、資料結構、主要演算法、CLI 入口與測試方式。內容以「目前程式碼實際行為」為準。

## 1. 現況總覽

已完成（可用且有測資覆蓋）：

- Tokenizer：doctype / start-end tag / attributes / comment / text / EOF
- Character references：numeric + named（讀 `entities.tsv`），含部分無 `;` 容錯
- RCDATA/RAWTEXT/script：`title/textarea/style/script` 的最小相容狀態
- Tree construction：核心 insertion modes（含 table 系列）+ auto-close + foster parenting
- Active formatting elements（AFE）：reconstruct + Noah’s Ark + marker + scope（最小集合）
- Adoption Agency Algorithm（AAA）：針對 `b/i/em/strong` 的錯誤巢狀修正
- Quirks / Limited-quirks：由 DOCTYPE 推算 `doc_mode`（目前不影響建樹規則）
- Fragment parsing：context 影響 tokenizer 初始狀態 + insertion mode；context element **不輸出**
- Node attrs：DOM 節點保留屬性；ASCII dump 會顯示 attrs
- Serialization：`tree_serialize_html()`（void/rawtext/rcdata/escaping）

仍缺（進階/完整相容）：

- Foreign content（SVG/MathML）
- 完整 parser error recovery（目前偏「盡量產生 token/tree」）
- quirks mode 對 tree construction 的實際差異（目前只推算，不套用）

更細的拆解請見 `list.md`。

## 2. 高階資料流

```text
input bytes
  → Tokenizer (tokenizer_next)
  → token stream
  → Tree builder (insertion modes)
  → DOM-like node tree
  → (optional) ASCII dump / serialize back to HTML
```

核心除錯心智模型：先分辨是「token 切錯」還是「消費 token 建樹錯」。

## 3. 專案結構與入口（CLI）

核心程式碼：

- `src/token.{h,c}`：token 結構與生命週期（`token_init`/`token_free`）
- `src/tokenizer.{h,c}`：tokenization + entity decode + doctype parse + line/col error output
- `src/tree.{h,c}`：node tree + ASCII dump + serializer + tree mutation helpers（AAA 用）
- `src/tree_builder.{h,c}`：tree construction（document + fragment）

CLI：

- `src/parse_file_demo.c` → `parse_html`：解析「完整文件」，輸出 ASCII tree
- `src/parse_fragment_demo.c` → `parse_fragment_demo`：解析「fragment」，輸出 ASCII tree
- `src/serialize_demo.c` → `serialize_demo`：解析文件後再序列化回 HTML

Makefile 目標：

- `make -C html_parser parse_html`
- `make -C html_parser parse_fragment_demo`
- `make -C html_parser serialize_demo`
- `make -C html_parser test-html`
- `make -C html_parser test-fragment`
- `make -C html_parser test-serialize`
- `make -C html_parser test-all`

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
- Element attrs：`node_attr *attrs` + `attr_count`
- Tree 表現：`first_child/last_child/next_sibling/parent`

重要 helper：

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

### 5.2 RCDATA/RAWTEXT/script（最小集合）

- `title/textarea` → `TOKENIZE_RCDATA`
- `style` → `TOKENIZE_RAWTEXT`
- `script` → `TOKENIZE_SCRIPT_DATA`（含最小 escaped/double-escaped 偵測）

### 5.3 Parse error 輸出（診斷）

- 目前 tokenizer 會直接輸出：`[parse error] line=... col=...: ...`
- 這是診斷輸出，不是 WHATWG 完整 error taxonomy

### 5.4 Named entities

- 來源：`entities.tsv`（`name<TAB>value`）
- lazy load；找不到檔案則回退到內建常用子集
- 查找目前為線性搜尋
- 無分號容錯：下一字元不是英數、也不是 `=` 才解碼

注意：`entities.tsv` 以相對路徑開啟，執行 binary 時的 working directory 會影響是否能載入完整表。

## 6. Tree Construction（`src/tree_builder.c`）

### 6.1 解析狀態

- open elements stack（`node_stack`）
- active formatting list（`formatting_list`）
- insertion mode（`MODE_*`）
- `doc_mode`（no-quirks/limited/quirks）：由 DOCTYPE 推算（目前不改變插入規則）

### 6.2 insertion modes（已支援的最小集合）

- `before html` / `in head` / `in body`
- `in table` / `in table body` / `in row` / `in cell` / `in caption`
- `in select` / `in select in table`
- `after body` / `after after body`

### 6.3 Auto-close（implied end tags，實作版）

- `<p>`：遇到 block-like start tag / 新 `<p>` 會先隱式關閉舊 `<p>`
- `<li>`：新 `<li>` 會關閉舊 `<li>`
- `<dt>/<dd>`：互斥關閉
- table sections/row/cell：同型元素開始時先關閉前一個

### 6.4 Foster parenting

處於 table 相關 mode 時遇到非 table 內容：

- element/text 會插到 `<table>` 的 parent 中、且位於 `<table>` 之前

### 6.5 AFE 與 AAA（最小集合）

支援 formatting elements：`b`/`i`/`em`/`strong`

- start tag：加入 AFE；必要時 reconstruct
- character / 非 formatting start tag：插入前 reconstruct AFE
- Noah’s Ark：同 tag 最多 3 筆
- marker：在 `td/th/caption` 邊界隔離 AFE，避免 cell/caption 內格式元素外溢
- end tag：若是 formatting element，優先走 AAA（`adoption_agency()`）

## 7. Fragment parsing

API：`build_fragment_from_input(input, context_tag)`

- context element 只用來決定 tokenizer 初始狀態與 insertion mode
- **context element 本身不會出現在輸出中**；輸出的是它的 children（DocumentFragment 容器）

Tokenizer context 初始化：

- `tokenizer_init_with_context(&tz, input, context_tag)`

Insertion mode 依 context：

- `table/tbody/thead/tfoot/tr/td/th/caption/select/head` → 對應 mode
- 其他 → `in body`

## 8. Serialization（`tree_serialize_html`）

- void elements 不輸出 end tag
- `script/style`（raw text）子文字不 escape
- `title/textarea`（rcdata）escape `& < >`
- 一般文字 escape `& < >`
- attribute value escape `&` 與 `"`

## 9. 測試策略

- `test-html`：逐一跑 `./parse_html tests/*.html`
- `test-fragment`：`tests/run_fragment_tests.sh` 逐案比對 ASCII Tree（PASS/FAIL/KNOWN）
- `test-serialize`：跑 `serialize_demo` 檢查序列化輸出

## 10. 已知限制（架構層面）

- `doc_mode` 已可推算，但尚未對 tree construction 造成差異
- formatting/scoping elements 清單仍是「最小集合」（完整清單見 `list.md`）
- foreign content（SVG/MathML）尚未實作
- entity lookup 目前是線性搜尋