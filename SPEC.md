# html_parser 規格書（Implementation Spec）

這份文件是「目前專案實作」的規格書：以 C 實作 **Tokenizer + 最小 Tree Construction**，用於把 HTML 輸入轉成簡單 DOM tree，並可輸出 ASCII tree 方便驗證。

## TL;DR

- [x] Tokenizer：支援 `<!DOCTYPE ...>`、start/end tag、attributes、`<!-- -->`、文字、EOF
- [x] Tree：以 node tree 表示 `document/doctype/element/text/comment`
- [x] 最小 insertion modes（擴充）：`before html`、`in head`、`in body`、`in table`、`in table body`、`in row`、`in cell`、`in caption`、`in select`、`in select in table`、`after body`、`after after body`
- [x] 視覺化：ASCII tree dump
- [x] 測試：`make test`

## Scope（本版有做的）

- 輸入：UTF-8 bytes（本版**不做** encoding sniffing；直接當作 `char*` 字串）
- Tokenizer：
  - DOCTYPE：支援 `<!DOCTYPE`（大小寫不敏感），讀取 name，若 name 缺失則 `force_quirks=1`
  - Comment：支援 `<!-- ... -->`，未閉合時讀到 EOF
  - Tag：
    - Start tag：`<tag ...>`、`<tag .../>`（self-closing）
    - End tag：`</tag>`
    - Tag name：ASCII alnum，會轉小寫
  - Attributes：
    - name：`[A-Za-z0-9_-:]`（會轉小寫）
    - value：支援 `name=value`（雙引號/單引號/無引號），以及 boolean attr（只有 name，value 視為空字串）
- Tree construction（最小可用）：
  - 以 `stack of open elements` 決定 current node
  - 基本 push/pop 規則 + 最小 insertion modes（見下）
  - Text node：**過濾純空白**（只保留含非空白字元的文字）

## Non-goals（本版刻意不做）

- 完整 WHATWG Tree Construction（含 active formatting elements、重建規則、Noah's Ark clause 等）
- 完整 Tokenizer 狀態（RCDATA/RAWTEXT/script data、character references `&amp;` 解碼等）
- Foreign content（SVG/MathML）的命名空間與整合點規則
- HTML quirks/limited-quirks 的完整判定
- Fragment parsing 的 context-dependent 初始化

## Build & Run

### 單元測試

```bash
make -C html_parser test
```

Tokenizer 測試可選 checkpoint：

```bash
cd html_parser
HTMLPARSER_CHECKPOINT=1 ./tokenizer_tests
```

### 解析 .html 檔並輸出 ASCII tree

```bash
make -C html_parser parse_file_demo
./html_parser/parse_file_demo html_parser/sample.html
```

## Tokenizer 規格（實作行為）

### Token types

- `TOKEN_DOCTYPE`：`name`, `force_quirks`
- `TOKEN_START_TAG`：`name`, `self_closing`, `attrs[]`
- `TOKEN_END_TAG`：`name`
- `TOKEN_COMMENT`：`data`
- `TOKEN_CHARACTER`：`data`（可能包含換行與空白）
- `TOKEN_EOF`

### 容錯（本版）

- `<` 後面不是合法 tag 起始時，會輸出 `TOKEN_CHARACTER` 的字面 `<`
- comment 未閉合：會輸出到 EOF 的 comment data
- 其他錯誤：以「盡量產生 token」為主，未追求瀏覽器級相容
- 可選的 parse error 輸出：設定 `HTMLPARSER_PARSE_ERRORS=1` 會在 stderr 輸出錯誤位置與訊息（例如：doctype name 缺失、非法 end tag、start tag 中 attr name 缺失等）

### Character References（擴充版）

- Named entities 來源：`entities.tsv`（同目錄，已填入完整 WHATWG 列表）
- 檔案格式：`name<TAB>value`
- 無分號容錯解碼：只有在「下一字元不是英數也不是 `=`」時才會解碼

## Tree Construction 規格（實作行為）

### Node types

- `NODE_DOCUMENT`
- `NODE_DOCTYPE`
- `NODE_ELEMENT`（`name`）
- `NODE_TEXT`（`data`）
- `NODE_COMMENT`（`data`）

### 基本規則（所有 modes 共用）

- current node：`stack top`，若 stack 空則為 `document`
- Start tag：
  - 建立 `ELEMENT(tag)`
  - append 到 current node
  - 若非 self-closing：push 到 stack
- End tag：從 stack top 往下 pop，直到遇到同名 element 為止（或清空）
- Comment：建立 `COMMENT(data)`，append 到 current node
- Character：
  - 若字串為「純空白」：忽略（不建 text node）
  - 否則建立 `TEXT(data)`，append 到 current node
- Doctype：建立 `DOCTYPE(name)`，append 到 `document`

### 自動補 tag（Auto-close / implied end tags，最小集合）

本專案用「自動關閉 open element」的方式，達到最小的容錯建樹（不建立額外 token，而是直接調整 open-elements stack）。

- 在 `in body` 遇到新的 start tag 時：
  - 若 stack 中存在尚未關閉的 `p`：
    - 當新 start tag 是 `p`，或是「block-like 元素」（例如 `div`, `table`, `ul`, `ol`, `h1`...）時，會先隱式關閉前一個 `p`。
  - 若新 start tag 是 `li` 且 stack 中存在尚未關閉的 `li`：會先隱式關閉前一個 `li`。
  - 若新 start tag 是 `dt`/`dd` 且對應元素尚未關閉：會先隱式關閉前一個 `dt`/`dd`。
  - 若新 start tag 是 `thead`/`tbody`/`tfoot`/`tr`/`td`/`th` 且同型元素尚未關閉：會先隱式關閉前一個。

### Active Formatting Elements（簡化版）

本版只實作最小「重建」規則，目標是避免 `b/i/em/strong` 的錯誤巢狀造成 DOM 斷裂。

- 支援元素：`b`, `i`, `em`, `strong`
- 行為：
  - start tag：若遇到格式化元素，記錄到 active list；插入前會嘗試重建（若 active list 中的元素不在 open stack，則重新插入）。
  - end tag：若 stack 中不存在該格式化元素，忽略該 end tag；否則關閉到該元素為止。

### 最小 insertion modes

#### `MODE_BEFORE_HTML`（before html）

- 若尚未建立 `html` element：遇到任何 token 需要插入 element 時，會自動建立 `html` 並 push。

#### `MODE_IN_HEAD`（in head）

- head 未存在但需要使用時：會自動建立 `<head>` 並 push。
- `in head` 可接受的元素（本版）：`base`, `link`, `meta`, `style`, `title`, `script`
- 在 `in head` 遇到「非 head 元素」的 start tag：
  - 隱式關閉 head（pop 到 `head`）
  - 切換到 `in body`
  - 重新處理該 token
- 在 `in head` 遇到 `</head>`：隱式關閉 head，切到 `in body`

#### `MODE_IN_BODY`（in body）

- body 未存在但需要使用時：會自動建立 `<body>` 並 push。
- 在 `in body` 遇到 `<html>`：忽略該 start tag（不建立巢狀 html）
- 在 `in body` 遇到 `<body>` 且 body 已存在：忽略該 start tag（避免巢狀 body）
- 在 `in body` 遇到 `<table>`：建立 table、push，切換到 `in table`

#### `MODE_IN_TABLE`（in table，擴充）

- 支援的 table 相關元素（最小集合）：`table`, `caption`, `colgroup`, `col`, `tbody`, `thead`, `tfoot`, `tr`, `td`, `th`
- 在 `in table` 遇到：
  - `<caption>`：建立並 push，切到 `in caption`
  - `<colgroup>`：建立並 push
  - `<col>`：建立（不 push）
  - `<tbody>/<thead>/<tfoot>`：建立並 push，切到 `in table body`
  - `<tr>`：建立並 push，切到 `in row`
  - `<td>/<th>`：建立並 push，切到 `in cell`
  - `<select>`：建立並 push，切到 `in select in table`
  - `</table>`：pop 到 `table`，切回 `in body`
- 在 `in table` 遇到非 table 元素：切回 `in body` 並重新處理

#### `MODE_IN_TABLE_BODY`（in table body）

- 目標：處理 `tbody/thead/tfoot` 內的 `tr/td/th`。
- 遇到 `<tr>`：建立並 push，切到 `in row`
- 遇到 `<td>/<th>`：自動補 `<tr>`，再插入 cell，切到 `in cell`
- 遇到新的 `<tbody>/<thead>/<tfoot>`：關閉目前 section，切回 `in table` 並重新處理
- 遇到非 table 元素：切回 `in body` 並重新處理

#### `MODE_IN_ROW`（in row）

- 遇到 `<td>/<th>`：建立並 push，切到 `in cell`
- 遇到 `<tbody>/<thead>/<tfoot>`：先關閉 `tr`，切回 `in table body` 並重新處理
- 遇到非 table 元素：切回 `in body` 並重新處理

#### `MODE_IN_CELL`（in cell）

- 遇到新的 `<td>/<th>`：先關閉目前 cell，切回 `in row` 並重新處理
- 遇到 `<tr>` 或 `<tbody>/<thead>/<tfoot>`：先關閉 cell，切回 `in table body` 並重新處理

#### `MODE_IN_CAPTION`（in caption）

- 遇到 `<table>/<tr>/<tbody>/<thead>/<tfoot>`：關閉 caption，切回 `in table` 並重新處理
- 其他 start tag：照一般元素插入（可巢狀）

#### `MODE_IN_SELECT` / `MODE_IN_SELECT_IN_TABLE`

- 只允許 `<option>/<optgroup>` 作為子元素（最小規則）
- 遇到巢狀 `<select>`：忽略
- `in select in table` 遇到 table 元素：先關閉 select，切回 `in table` 並重新處理
- 遇到 `</select>`：關閉 select，回到 `in body` 或 `in table`

#### `MODE_AFTER_BODY` / `MODE_AFTER_AFTER_BODY`

- `</body>` 之後進入 `after body`
- `</html>` 之後進入 `after after body`
- 若仍有文字/元素：切回 `in body` 重新插入（確保產出 DOM）

## ASCII Tree 輸出格式

- 由 `tree_dump_ascii()` 輸出
- 根節點固定顯示 `DOCUMENT`，其下以 `|--` / `\\--` 顯示 child 順序
- 節點會顯示 `type`，並視情況加上 `name="..."` 或 `data="..."`

## 檔案與模組

- `html_parser/src/token.{h,c}`：token 結構與釋放
- `html_parser/src/tokenizer.{h,c}`：tokenizer（字串 -> token）
- `html_parser/src/tree.{h,c}`：DOM node + ASCII dump
- `html_parser/src/tree_builder.{h,c}`：tree construction（token -> tree / input -> tree）
- `html_parser/tests/tokenizer_tests.c`：tokenizer 測試（含 checkpoint）
- `html_parser/tests/tree_tests.c`：tree construction 測試
- `html_parser/tests/parse_file_demo.c`：讀檔 -> parse -> dump
- `html_parser/sample.html`：範例輸入
