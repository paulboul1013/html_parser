# CLAUDE.md

本文件為 Claude Code (claude.ai/code) 操作本儲庫時的參考指南。

---

## 建立與執行

```sh
make            # 建立 ./parse_html（預設目標）
make clean      # 移除 ./parse_html

# 解析單個 HTML 文件，將 ASCII 樹輸出到標準輸出
./parse_html tests/sample.html

# 啟用解析錯誤輸出到標準錯誤
HTMLPARSER_PARSE_ERRORS=1 ./parse_html tests/parse_errors.html

# 片段解析（context-tag 決定詞法分析器的初始狀態與插入模式）
make parse_fragment_demo
./parse_fragment_demo div tests/fragment_basic.html
```

---

## 測試方式

測試為放置於 `tests/` 目錄下的 HTML 文件。目前沒有自動化的斷言框架 — 每個文件經解析後，手動檢視其 ASCII 樹輸出。Makefile 目標可一次執行所有已登記的測試文件：

```sh
make test-html
```

單獨測試某個文件：

```sh
./parse_html tests/<name>.html
```

新增測試：在 `tests/` 中建立一個 `.html` 文件，並在 Makefile 的 `test-html` 目標中增加對應的 `./parse_html tests/<name>.html` 行。

---

## 架構

解析器為以 C11 實現的兩階段管道：

```
輸入字串  →  詞法分析器  →  令牌流  →  樹構建器  →  節點樹
             (tokenizer.c)            (tree_builder.c)
```

### 第一階段：詞法分析（`token.h/c`、`tokenizer.h/c`）

- `tokenizer` 為有狀態的結構，消耗一個 `const char *input`。反覆調用 `tokenizer_next()` 逐個拉取 `token`。
- 六種詞法分析器狀態：DATA、RCDATA、RAWTEXT、SCRIPT_DATA、SCRIPT_DATA_ESCAPED、SCRIPT_DATA_DOUBLE_ESCAPED。樹構建器在遇到 `<script>`、`<style>`、`<title>`、`<textarea>` 等元素時，透過 `raw_tag` 切換狀態。
- 命名字元參考從 `entities.tsv`（WHATWG 完整列表）在首次使用時裝載；若該文件遺漏，則回退至約 24 個內建常用實體。查找方式為線性搜尋。
- `token_free()` 必須在每個令牌被消耗後調用 — 所有字串欄位均為堆記憶體配置。

### 第二階段：樹構建（`tree.h/c`、`tree_builder.h/c`）

- 維護一個 **開放元素棧**（`node_stack`）與一個 **活躍格式化列表**（`formatting_list`）作為核心可變狀態。
- 一個 `insertion_mode` 列舉（15 種模式）決定哪些令牌會被接受及如何處理 — 緊密對應 WHATWG 規範的插入模式算法。
- 已實現的關鍵算法：
  - **自動關閉**：`p` 在遇到塊級元素開始時被關閉；`li`/`dt`/`dd` 在遇到同類兄弟元素時被關閉；表格sections（`thead`/`tbody`/`tfoot`/`tr`/`td`/`th`）在遇到新的section/行/儲元格時自動關閉。
  - **Generate implied end tags（WHATWG §13.2.6.3）**：集中函數 `generate_implied_end_tags()` / `generate_implied_end_tags_except()` 在 `</p>`、`</li>`、`</dd>`/`</dt>`、`</body>` end tag 處理前，從棧頂反覆彈出 implied-closable 元素（`dd`, `dt`, `li`, `optgroup`, `option`, `p`, `rb`, `rp`, `rt`, `rtc`）。`<option>` / `<optgroup>` start tag 也有對應的 auto-close 邏輯。
  - **收養代理 / 活躍格式化**：`b`/`i`/`em`/`strong` 被追蹤；重建程序在中斷後重新插入它們。Noah's Ark 條款將活躍列表中每個標記限制為最多 3 條。
  - **收養父化（Foster parenting）**：在表格模式下遇到的非表格內容，將插入到 `<table>` 元素的直接前方而非內部。
  - **Foreign Content（SVG/MathML）**：`<svg>` 和 `<math>` 進入對應命名空間（`NS_SVG`、`NS_MATHML`）。包含 breakout 偵測、SVG/MathML 元素/屬性大小寫修正、Integration Points、CDATA 區段。
  - **`<form>` element pointer**：WHATWG 定義的 form element pointer，自動將 form-associated 元素（`input`/`button`/`select`/`textarea`/`fieldset`/`output`/`object`/`img`）關聯至所屬 `<form>`。
  - **Quirks 模式**：由 DOCTYPE 的 public/system ID 判定。模式已儲存（`NO_QUIRKS`、`LIMITED_QUIRKS`、`QUIRKS`），已套用於 `<table>` 不自動關閉 `<p>` 的規則。
- 當某個模式處理程序判定當前令牌應在不同模式下重新評估時，采用令牌重新處理迴圈（`goto reprocess`），避免遞迴調用。

### 片段解析

`build_fragment_from_input(input, context_tag)` 建立一個合成的上下文元素，由它為詞法分析器種植初始狀態與插入模式，構建樹後再剥離上下文包裹層。上下文元素本身不會出現在輸出中。

### 樹輸出

`tree.c` 中的 `tree_dump_ascii()` 使用 `|--` / `\--` 分支標記列印節點樹。每個節點顯示其類型及相關屬性（名稱、數據）。此外，`tree_serialize_html()` 可將節點樹序列化回 HTML 字串（含 void element、raw text、RCDATA、屬性值 entity 轉換等規範化處理）。

---

## 關鍵文件

| 文件 | 職責 |
|------|------|
| `src/token.h/c` | Token 結構及生命週期管理（init/free） |
| `src/tokenizer.h/c` | 有狀態詞法分析器；實體裝載；字元參考解碼 |
| `src/tree.h/c` | 節點結構、子節點連接、遞迴釋放、ASCII 輸出 |
| `src/tree_builder.h/c` | 插入模式、自動關閉、generate implied end tags、格式化處理、收養父化、Quirks 檢測、Foreign Content 處理 |
| `src/foreign.h/c` | Foreign Content 查找表（breakout tags、SVG/MathML 名稱修正）、Integration Points、命名空間感知 scope/special |
| `src/encoding.h/c` | WHATWG 編碼嗅探、39 種編碼查找表、BOM/meta prescan、iconv/內建 UTF-16 轉換 |
| `src/parse_file_demo.c` | 完整文件解析的 CLI 入口點 |
| `src/parse_fragment_demo.c` | 片段解析的 CLI 入口點 |
| `entities.tsv` | WHATWG 命名字元參考表（定界符分隔） |
| `ARCHITECTURE.md` | 系統架構、資料結構、演算法、模組細節（know.md + SPEC.md 合併版） |
| `list.md` | 功能完成進度清單（P0–P3） |

---

## 注意事項
- **請你回答說繁體中文**
- **`entities.tsv` 必須從執行時的工作目錄中可以訪問。** 若二元程序從不同目錜執行，實體解析將回退至內建子集。
- **僅含空白的文本節點會在樹構建時被捨棄**（`is_all_whitespace` 過濾）。這符合瀏覽器行為，但不重要的空白不會出現在輸出中。
- **Quirks 模式已部分套用。** `doc_mode` 已被正確設定；Quirks 模式下 `<table>` 不自動關閉 `<p>` 的規則已實作。
- **支援 Foreign Content。** SVG 與 MathML 元素在各自命名空間中正確處理，包含 breakout、Integration Points、CDATA 區段。

---

## 已實現功能總結

以下根據 `list.md` 中的優先級分類，整理出目前已完成的功能：

### P0 — 核心詞法分析（✅ 全部完成）

| 功能 | 狀態 |
|------|------|
| 完整的詞法分析器狀態機（Tag open / End tag / Attribute 等） | ✅ |
| Comment 與 DOCTYPE 的完整狀態及容錯處理 | ✅ |
| Script data 特殊規則（`</script>` 偵測、escaped / double escaped） | ✅ |
| 完整命名字元參考（`entities.tsv` + WHATWG 容錯規則） | ✅ |

### P1 — 樹構建核心（✅ 全部完成）

| 功能 | 狀態 |
|------|------|
| 完整插入模式（`in table body`、`in row`、`in cell`、`in caption`、`in select`、`after body` 等） | ✅ |
| Foster parenting（非表格內容插入表格前方） | ✅ |
| 活躍格式化元素重建（含 Noah's Ark 條款） | ✅ |
| Adoption Agency 算法（`b`/`i`/`em`/`strong` 的正確巢套處理） | ✅ |

### P2 — 中期補完（✅ 全部完成）

| 功能 | 狀態 |
|------|------|
| Quirks / Limited-quirks 模式判定（由 DOCTYPE 推算） | ✅ |
| 片段解析（context element 初始化 + 特殊插入規則） | ✅ |

### P3 — 進階相容（大部分完成）

| 功能 | 狀態 |
|------|------|
| Generate implied end tags（WHATWG §13.2.6.3，含 `</p>`/`</li>`/`</dd>`/`</dt>`/`</body>` 專用處理、`<option>`/`<optgroup>` auto-close） | ✅ |
| Foreign Content 支援（SVG / MathML 命名空間、Integration Points、CDATA） | ✅ |
| `<h1>`-`<h6>` Heading auto-close | ✅ |
| CR/LF 正規化 | ✅ |
| Numeric reference 範圍修正（Windows-1252 映射） | ✅ |
| Noah's Ark attribute 比對（精確 tag+attrs 去重） | ✅ |
| `in table text` 模式 | ✅ |
| `<form>` element pointer（form-associated 元素自動關聯） | ✅ |
| Quirks 模式對樹構建規則的部分套用 | ✅ |
| 完整 parser error recovery（所有 parse errors 的標準化容錯行為） | ⏳ |