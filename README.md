# html_parser（C）

這是一個用 C 實作的 **HTML tokenizer + 最小 Tree Construction** 專案：目標是把 `text/html` 轉成可用的 DOM-like tree，並輸出 **ASCII tree** 方便人類檢查與迭代。

本專案刻意走「可逐步補齊」路線：先把最常見的 HTML 行為做起來（容錯、table、格式化元素、script/rcdata），再往 WHATWG HTML Standard 的完整實作靠近。

---

## 特色

- Tokenizer
- 支援 doctype / start-end tag / attributes / comment / text / EOF
- 支援 RCDATA/RAWTEXT/script data（`title/textarea/style/script`）
- Character references
- 支援 numeric refs（十進位/十六進位，含無 `;` 容錯）
- 支援 named entities（從 `entities.tsv` 載入完整 WHATWG 列表）
- Parse errors
- 直接輸出到 stdout，包含 `line/col`
- Tree construction
- DOM-like node tree（document/doctype/element/text/comment）
- 最小 insertion modes：`before html`, `in head`, `in body`, `in table`, `in row`, `in cell`
- Auto-close（最小集合）：`p/li/dt/dd/thead/tbody/tfoot/tr/td/th`
- Active formatting（簡化版）：`b/i/em/strong` 的最小重建，避免錯誤巢狀造成 DOM 斷裂
- 測試方式
- 全部用 `.html` 檔跑 end-to-end，輸出 ASCII tree 供比對（`make test-html`）

---

## 快速開始

### Build

```bash
make  html_parser
```

會產生可執行檔：`parse_html`

### 解析單一 HTML（輸出 ASCII tree）

```bash
.parse_html html_parser/tests/sample.html
```

### 跑全部 HTML 測試

```bash
make  html_parser test-html
```

> `test-html` 會依序解析 `html_parser/tests/*.html` 並輸出 ASCII tree。若你要「自動比對 expected」，可再加 `.expected` 檔機制（目前尚未做）。

---

## 測試資料（tests/）

測試檔都放在 `html_parser/tests/`，每個檔案都在測一組特定能力：

- `sample.html`：基本結構、attributes、table
- `autoclose.html`：`p/li` 自動關閉
- `autoclose_extended.html`：`dt/dd` + table 相關 auto-close
- `charrefs.html`：基本 entity / numeric refs
- `charrefs_more.html`：更多 entity + 無 `;` 容錯
- `rcdata_rawtext_script.html`：RCDATA/RAWTEXT/script data 行為
- `script_escaped.html`：script escaped / double escaped（最小支援）
- `table_full.html`：完整 table 最小集合（caption/colgroup/thead/tbody/tfoot/tr/td/th）
- `parse_errors.html`：parse error 行為與容錯輸出示範

新增測試的建議流程：

1. 在 `html_parser/tests/` 新增 `xxx.html`
2. 在 `html_parser/Makefile` 的 `test-html` 目標新增一行 `./parse_html tests/xxx.html`
3. 跑 `make -C html_parser test-html`，肉眼檢查 ASCII tree 是否符合預期

---

## 輸出格式（ASCII tree）

`parse_html` 會印出：

1. （可選）parse error：`[parse error] line=... col=...: ...`
2. `ASCII Tree (File)` + 樹狀結構

節點會以：

- `DOCUMENT`
- `DOCTYPE name="..."`
- `ELEMENT name="..."`
- `TEXT data="..."`
- `COMMENT data="..."`

呈現。

---

## Named Entities（entities.tsv）

`html_parser/entities.tsv` 為 `name<TAB>value` 格式。

- 專案啟動時會載入 `entities.tsv`（目前已填入完整 WHATWG 列表）
- 若檔案不存在，會回退到內建的常用 entity 集合（仍可運作，但解碼不完整）

---

## 專案結構

- `html_parser/src/token.{h,c}`：token 結構與釋放
- `html_parser/src/tokenizer.{h,c}`：tokenizer（含 states、char refs、parse errors）
- `html_parser/src/tree.{h,c}`：node 結構與 ASCII dump
- `html_parser/src/tree_builder.{h,c}`：tree construction（含 insertion modes、auto-close、active formatting 簡化）
- `html_parser/src/parse_file_demo.c`：`parse_html` 主程式（讀檔 -> parse -> dump）
- `html_parser/tests/`：HTML 測試檔
- `html_parser/SPEC.md`：Implementation spec（以目前實作為準）
- `html_parser/know.md`：給人吸收的筆記（架構、心智模型、擴充方向）
- `html_parser/list.md`：待補清單（依優先序）

---

## 已知限制（這不是瀏覽器級完整實作）

- Tokenizer / tree construction 都是「最小可用 + 漸進補齊」，尚未完整覆蓋 WHATWG 所有狀態與 insertion modes
- table 的 foster parenting、template insertion mode stack、完整 active formatting rules（Noah’s Ark clause）尚未實作
- foreign content（SVG/MathML）、quirks/limited-quirks、fragment parsing 尚未實作
- entities.tsv 是外部資料表；目前採「載入後線性掃描」比對，尚未做 trie/hash 最佳化

---

## Roadmap

下一步建議直接看：

- `html_parser/list.md`（依 P0/P1/P2/P3 排序）

---

## 參考

- WHATWG HTML Standard（Living Standard）— entities / tokenization / tree construction
