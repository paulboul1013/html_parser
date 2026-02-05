# html_parser — 系統架構與技術細節

本文件統一記錄此專案的所有詳細知識、架構設計、資料結構、演算法行為，以及開發者需知的實作慣例。

---

## 1. 專案現況（功能概攏）

- [x] Tokenizer：把 HTML 字串切成 token（doctype / tag / comment / text / EOF）
- [x] Tree builder：以插入模式將 token 構建為 DOM-like tree
- [x] ASCII tree dump：將樹以文字畫出來，方便人工檢查
- [x] 完整 insertion modes：`before html`, `in head`, `in body`, `in table`, `in table body`, `in row`, `in cell`, `in caption`, `in select`, `in select in table`, `after body`, `after after body`
- [x] Foster parenting、Active formatting elements（含 Noah's Ark clause）、Adoption Agency 算法
- [x] Quirks / Limited-quirks 模式判定（由 DOCTYPE 推算，判定結果已儲存，尚未套用於樹構建規則）
- [x] 片段解析（context element 初始化 + 特殊插入規則）
- [ ] Foreign content（SVG / MathML）
- [ ] Quirks 模式對樹構建的實際套用
- [ ] 完整 parser error recovery

進度追蹤細節請參照 `list.md`。

---

## 2. 兩階段管道（核心心智模型）

```
輸入字串  →  詞法分析器  →  令牌流  →  樹構建器  →  節點樹
             (tokenizer.c)            (tree_builder.c)
```

- **Stage A（Tokenizer）**：輸入字元串 → 輸出 token stream
- **Stage B（Tree construction）**：輸入 token stream → 輸出 DOM-like tree

除錯時先問：是「切 token」錯，還是「消費 token 建樹」錯。

---

## 3. 資料結構

### 3.1 Token（`src/token.h`）

| Type | 欄位 |
|------|------|
| `TOKEN_START_TAG` | `name`, `self_closing`, `attrs[]` |
| `TOKEN_END_TAG` | `name` |
| `TOKEN_DOCTYPE` | `name`, `force_quirks`, `public_id`, `system_id` |
| `TOKEN_COMMENT` | `data` |
| `TOKEN_CHARACTER` | `data`（可能包含換行與空白） |
| `TOKEN_EOF` | — |

所有字串欄位均為堆配置。每個 token 消費後**必須**調用 `token_free()`。

### 3.2 Node（`src/tree.h`）

| Type | 欄位 |
|------|------|
| `NODE_DOCUMENT` | — |
| `NODE_DOCTYPE` | `name` |
| `NODE_ELEMENT` | `name` |
| `NODE_TEXT` | `data` |
| `NODE_COMMENT` | `data` |

樹結構為單向 sibling + first/last child，便於 append。釋放時使用 `node_free()`（遞迴釋放整棵子樹）。

---

## 4. Tokenizer 詳細行為（`src/tokenizer.h/c`）

### 4.1 詞法分析器狀態

`tokenizer` 為有狀態的結構，消耗一個 `const char *input`。反覆調用 `tokenizer_next()` 逐個拉取 token。

六種詞法分析器狀態：

| 狀態 | 觸發條件 |
|------|----------|
| `DATA` | 預設初始狀態 |
| `RCDATA` | `<title>`, `<textarea>` |
| `RAWTEXT` | `<style>` |
| `SCRIPT_DATA` | `<script>` |
| `SCRIPT_DATA_ESCAPED` | `<script>` 內偵測到 `<!--` |
| `SCRIPT_DATA_DOUBLE_ESCAPED` | script escaped 內再遇到 `<script>` |

樹構建器在遇到對應元素時，透過 `raw_tag` 切換狀態。

### 4.2 Tag 與 Attribute 解析

- **Tag name**：ASCII alnum，會轉小寫。
- **Attribute name**：`[A-Za-z0-9_-:]`，會轉小寫。
- **Attribute value**：支援雙引號 / 單引號 / 無引號，以及 boolean attr（只有 name，value 視為空字串）。
- **Self-closing**：支援 `<tag ... />`。

### 4.3 容錯策略

- `<` 後面不是合法 tag 起始時，輸出 `TOKEN_CHARACTER` 的字面 `<`。
- Comment 未閉合：讀到 EOF 為止。
- 以「盡量產生 token」為主，未追求瀏覽器級相容。
- 可選的 parse error 輸出：設定環境變數 `HTMLPARSER_PARSE_ERRORS=1`，會在 stderr 輸出錯誤位置與訊息（例如：doctype name 缺失、非法 end tag、start tag 中 attr name 缺失等）。

### 4.4 Character References

- **Named entities** 來源：`entities.tsv`（同目錄，已填入完整 WHATWG 列表）。
- 檔案格式：`name<TAB>value`。首次使用時裝載；若該文件遺漏，則回退至約 24 個內建常用實體。查找方式為線性搜尋。
- **無分號容錯解碼**：只有在「下一字元不是英數也不是 `=`」時才會解碼。

> **注意**：`entities.tsv` 必須從執行時的工作目錄中可以訪問。若二元程序從不同目錄執行，實體解析將回退至內建子集。

---

## 5. Tree Construction 詳細行為（`src/tree_builder.h/c`）

### 5.1 核心可變狀態

- **開放元素棧**（`node_stack`）：維持「目前正在哪裡插入」。current node 為 stack top；若 stack 空則為 `document`。
- **活躍格式化列表**（`formatting_list`）：追蹤 `b`/`i`/`em`/`strong` 等格式化元素，用於重建算法。
- **插入模式**（`insertion_mode`）：13 種模式之一，決定哪些 token 會被接受及如何處理。

### 5.2 基本插入規則（所有 modes 共用）

| Token | 行為 |
|-------|------|
| Start tag | 建立 `ELEMENT`，append 到 current node；若非 self-closing 則 push 到棧 |
| End tag | 從 stack top 往下 pop，直到遇到同名 element（或清空） |
| Comment | 建立 `COMMENT(data)`，append 到 current node |
| Character | 若為「純空白」則忽略；否則建立 `TEXT(data)` 並 append |
| Doctype | 建立 `DOCTYPE(name)`，append 到 `document` |

> **纖細細節**：含空白的文本節點會在樹構建時被捨棄（`is_all_whitespace` 過濾）。符合瀏覽器行為，但不重要的空白不會出現在輸出中。

### 5.3 Insertion Modes（逐一說明）

#### `MODE_BEFORE_HTML`
- 若尚未建立 `html` element：遇到需要插入 element 的 token 時，會自動建立 `<html>` 並 push。

#### `MODE_IN_HEAD`
- head 未存在但需要使用時：自動建立 `<head>` 並 push。
- 可接受的元素：`base`, `link`, `meta`, `style`, `title`, `script`。
- 遇到「非 head 元素」的 start tag：隱式關閉 head → 切換到 `in body` → 重新處理該 token。
- 遇到 `</head>`：隱式關閉 head，切到 `in body`。

#### `MODE_IN_BODY`
- body 未存在但需要使用時：自動建立 `<body>` 並 push。
- 遇到 `<html>`：忽略（不建立巢狀 html）。
- 遇到 `<body>` 且 body 已存在：忽略（避免巢狀 body）。
- 遇到 `<table>`：建立 table、push，切到 `in table`。
- 自動補 tag 規則見 §5.4。

#### `MODE_IN_TABLE`
- 支援元素：`table`, `caption`, `colgroup`, `col`, `tbody`, `thead`, `tfoot`, `tr`, `td`, `th`。
- `<caption>` → push，切到 `in caption`。
- `<colgroup>` → push；`<col>` → 建立但不 push。
- `<tbody>/<thead>/<tfoot>` → push，切到 `in table body`。
- `<tr>` → push，切到 `in row`。
- `<td>/<th>` → push，切到 `in cell`。
- `<select>` → push，切到 `in select in table`。
- `</table>` → pop 到 `table`，切回 `in body`。
- 遇到非 table 元素：**Foster parenting**（見 §5.7）。

#### `MODE_IN_TABLE_BODY`
- 遇到 `<tr>`：建立並 push，切到 `in row`。
- 遇到 `<td>/<th>`：自動補 `<tr>`，插入 cell，切到 `in cell`。
- 遇到新的 `<tbody>/<thead>/<tfoot>`：關閉目前 section，切回 `in table` 並重新處理。
- 遇到非 table 元素：切回 `in body` 並重新處理。

#### `MODE_IN_ROW`
- 遇到 `<td>/<th>`：建立並 push，切到 `in cell`。
- 遇到 `<tbody>/<thead>/<tfoot>`：先關閉 `tr`，切回 `in table body` 並重新處理。
- 遇到非 table 元素：切回 `in body` 並重新處理。

#### `MODE_IN_CELL`
- 遇到新的 `<td>/<th>`：先關閉目前 cell，切回 `in row` 並重新處理。
- 遇到 `<tr>` 或 `<tbody>/<thead>/<tfoot>`：先關閉 cell，切回相應 mode 並重新處理。

#### `MODE_IN_CAPTION`
- 遇到 `<table>/<tr>/<tbody>/<thead>/<tfoot>`：關閉 caption，切回 `in table` 並重新處理。
- 其他 start tag：照一般元素插入（可巢狀）。

#### `MODE_IN_SELECT` / `MODE_IN_SELECT_IN_TABLE`
- 只允許 `<option>/<optgroup>` 作為子元素。
- 遇到巢狀 `<select>`：忽略。
- `in select in table` 遇到 table 元素：先關閉 select，切回 `in table` 並重新處理。
- 遇到 `</select>`：關閉 select，回到 `in body` 或 `in table`。

#### `MODE_AFTER_BODY` / `MODE_AFTER_AFTER_BODY`
- `</body>` 之後進入 `after body`。
- `</html>` 之後進入 `after after body`。
- 若仍有文字 / 元素：切回 `in body` 重新插入（確保產出 DOM）。

> 當某個模式處理程序判定當前令牌應在不同模式下重新評估時，采用令牌重新處理迴圈（`goto reprocess`），避免遞迴調用。

### 5.4 自動補 tag（Auto-close / Implied end tags）

在 `in body` 遇到新的 start tag 時：

| 條件 | 行為 |
|------|------|
| stack 中有未關閉的 `<p>` + 新 tag 為 `<p>` 或 block-like 元素（`div`, `table`, `ul`, `ol`, `h1`…） | 隱式關閉前一個 `<p>` |
| 新 tag 為 `<li>` + stack 中有未關閉的 `<li>` | 隱式關閉前一個 `<li>` |
| 新 tag 為 `<dt>`/`<dd>` + 對應元素未關閉 | 隱式關閉前一個 `<dt>`/`<dd>` |
| 新 tag 為 `thead`/`tbody`/`tfoot`/`tr`/`td`/`th` + 同型元素未關閉 | 隱式關閉前一個 |

### 5.5 Active Formatting Elements（活躍格式化元素）

支援元素：`b`, `i`, `em`, `strong`。

- **Start tag**：記錄到 active list；插入前會嘗試重建（若 active list 中的元素不在 open stack，則重新插入）。
- **Character / 非 formatting start tag**：插入前先重建 active formatting elements。
- **Noah's Ark clause**：active list 中相同元素最多保留 3 筆，超過會移除最早的那筆。
- **End tag**：若目標元素不在 scope 中則忽略；否則關閉到該元素為止，並同步移除 active list 中對應項目。

### 5.6 Adoption Agency 算法

處理 `b`/`i`/`em`/`strong` 等格式化元素的錯誤巢狀情況。當 end tag 對應的 open element 不是 current node 時，算法會將內容正確地從巢狀結構中脫離並重新組織，確保輸出樹的語義正確。

### 5.7 Foster Parenting（收養父化）

當解析處於 table 相關模式，但遇到「非 table 元素」或文字內容時：

- 該節點插入到 **`<table>` 前面**（table 的 parent 之下、table 之前），而非插入 table 內部。
- 若目前 stack top 是非 table 元素，後續 token 以 `in body` 規則插入該元素內。

### 5.8 Quirks / Limited-quirks 模式

判定依據：

| 條件 | 結果 |
|------|------|
| `force_quirks=1` 或 doctype name 不是 `html` | `QUIRKS` |
| public / system id 命中 legacy 列表 | `QUIRKS` |
| public id 命中 XHTML1.0 frameset / transitional | `LIMITED_QUIRKS` |
| public id 為 HTML4.01 frameset / transitional 且 system id 不缺 | `LIMITED_QUIRKS` |
| 其他 | `NO_QUIRKS` |

`doc_mode` 已被正確設定；Quirks / Limited-quirks 對應的樹構建規則變更列為後續工作（P3）。

---

## 6. 片段解析（Fragment Parsing）

**API**：`build_fragment_from_input(input, context_tag)`

- 建立一個合成的「context element」作為容器，用來決定 tokenizer 初始狀態與插入模式。
- **Context element 本身不會出現在輸出中**，輸出的是 context 的 children。

### Tokenizer 初始狀態依 context：

| Context tag | 初始狀態 |
|-------------|----------|
| `title`, `textarea` | `RCDATA` |
| `script` | `SCRIPT_DATA` |
| `style` | `RAWTEXT` |
| 其他 | `DATA` |

### Insertion mode 依 context：

`table`/`tbody`/`thead`/`tfoot`/`tr`/`td`/`th`/`caption`/`select` 等會直接進入對應 mode；一般元素則進入 `in body`。

### 範例

輸入片段（context=`div`）：

```html
<div>One <b>Two</b></div>
```

輸出：

```
DOCUMENT
\-- ELEMENT name="div"
    |-- TEXT data="One "
    \-- ELEMENT name="b"
        \-- TEXT data="Two"
```

---

## 7. ASCII Tree 輸出格式

由 `tree_dump_ascii()` 輸出：

- 根節點固定顯示 `DOCUMENT`，其下以 `|--` / `\--` 顯示 child 順序。
- 每個節點顯示其類型，並視情況加上 `name="..."` 或 `data="..."`。

---

## 8. 模組對應表

| 文件 | 職責 |
|------|------|
| `src/token.h/c` | Token 結構及生命週期管理（init / free） |
| `src/tokenizer.h/c` | 有狀態詞法分析器；實體裝載；字元參考解碼 |
| `src/tree.h/c` | Node 結構、子節點連接、遞迴釋放、ASCII 輸出 |
| `src/tree_builder.h/c` | 插入模式、自動關閉、格式化處理、收養父化、Quirks 檢測 |
| `src/parse_file_demo.c` | 完整文件解析的 CLI 入口點 |
| `src/parse_fragment_demo.c` | 片段解析的 CLI 入口點 |
| `entities.tsv` | WHATWG 命名字元參考表（Tab 分隔） |
| `list.md` | 功能完成進度清單（P0–P3） |
| `CLAUDE.md` | Claude Code 操作參考（建立、測試、快速索引） |

---

## 9. C 實作慣例與踩雷清單

- C11 不保證有 `strdup`，建議用專案內的 `dup_string()`。
- 每次取到 token 後要 `token_free()`，避免 memory leak。
- Tree 釋放要用 `node_free()`（會遞迴釋放）。
- 輸入為 UTF-8 bytes，本版不做 encoding sniffing，直接當作 `char*` 字串處理。
- 不支持外國內容：SVG 與 MathML 元素未被特別處理，視為普通 HTML 元素。
