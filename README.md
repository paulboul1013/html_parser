# html_parser

一個用純 C 語言從零實作的 HTML5 解析器，目標是符合 [WHATWG HTML Living Standard](https://html.spec.whatwg.org/multipage/parsing.html) 的核心規範。

專案以教學與深入理解瀏覽器核心為目的，不依賴任何第三方函式庫（僅可選用 glibc `iconv` 處理編碼轉換），完整實作了 Tokenizer、Tree Construction、Fragment Parsing、Serialization 與 Encoding Sniffing 五大模組。

---

## 專案特色

- **純 C11 實作**：零依賴、單一 `make` 即可編譯，可在任何 POSIX 環境執行
- **兩階段管道架構**：Tokenizer（詞法分析）→ Tree Builder（樹構建），清晰分離
- **高規範覆蓋率**：已實作 WHATWG HTML Standard 中絕大多數 Tokenizer 狀態與 13 種 Tree Construction 插入模式
- **完整 Adoption Agency Algorithm**：正確處理 `<b><div></b>` 等格式化元素的錯誤巢套
- **39 種 WHATWG 編碼支援**：BOM 偵測 → 傳輸層 hint → meta prescan → 預設 UTF-8
- **約 5,600 行 C 程式碼**（不含測試與資料檔）

---

## 架構總覽

```
輸入位元組
  → Encoding Sniffing（encoding.c）— 偵測 BOM / meta charset / hint → 轉換為 UTF-8
  → NULL 預處理（U+0000 → U+FFFD）
  → Tokenizer（tokenizer.c）— 狀態機產生 Token 串流
  → Tree Builder（tree_builder.c）— 消費 Token，依 Insertion Mode 建構節點樹
  → 輸出：ASCII Tree Dump（tree.c）或 HTML Serialization（tree.c）
```

### 模組職責

| 模組 | 檔案 | 行數 | 職責 |
|------|------|------|------|
| Token | `token.h/c` | ~70 | Token 結構定義（6 種類型）、生命週期管理 |
| Tokenizer | `tokenizer.h/c` | ~1,330 | 狀態機、Character Reference 解碼（完整 `entities.tsv`）、Comment/DOCTYPE 解析 |
| Tree | `tree.h/c` | ~390 | Node 結構、子節點操作、ASCII Dump、HTML Serialization |
| Tree Builder | `tree_builder.h/c` | ~2,700 | 13 種 Insertion Mode、Auto-close、Foster Parenting、AFE/AAA、Quirks 判定 |
| Encoding | `encoding.h/c` | ~910 | WHATWG 編碼嗅探、39 種編碼查找表、BOM/meta prescan、iconv/內建 UTF-16 轉換 |
| CLI | `parse_file_demo.c` | ~70 | 完整文件解析入口 |
| CLI | `parse_fragment_demo.c` | ~60 | Fragment 解析入口 |
| CLI | `serialize_demo.c` | ~65 | 序列化示範入口 |

---

## 已實現功能

### Tokenizer（詞法分析）

| 功能 | 狀態 |
|------|------|
| 完整 Tag Open / End Tag / Tag Name 狀態機 | ✅ |
| Attribute 解析（雙引號 / 單引號 / 無引號 / Boolean） | ✅ |
| Comment 完整狀態機（10 種 Comment 狀態，含 `<!-->` / `<!--->` 邊緣情況） | ✅ |
| DOCTYPE 解析（PUBLIC / SYSTEM identifier） | ✅ |
| RCDATA（`<title>`, `<textarea>`）/ RAWTEXT（`<style>`）/ Script Data（`<script>`） | ✅ |
| Script Data Escaped / Double Escaped 狀態（`<!--` / `-->` 偵測） | ✅ |
| Named Character References（完整 WHATWG 2,231 實體，`entities.tsv`） | ✅ |
| Numeric Character References（十進位 / 十六進位，含無分號容錯） | ✅ |
| Attribute Context 中的 Character Reference 解碼（legacy 實體 `=`/alnum 後不解碼） | ✅ |
| NULL 字元替換（U+0000 → U+FFFD） | ✅ |
| Parse Error 報告（line:col 定位） | ✅ |

### Tree Construction（樹構建）

| 功能 | 狀態 |
|------|------|
| 13 種 Insertion Mode：`initial` → `before html` → `in head` → `in body` → `in table` / `in table body` / `in row` / `in cell` / `in caption` / `in select` / `in select in table` → `after body` → `after after body` | ✅ |
| Auto-close：`<p>` 遇到 block-like 元素自動關閉；`<li>` / `<dt>` / `<dd>` 同類互斥關閉；表格 section/row/cell 自動關閉 | ✅ |
| Generate Implied End Tags（WHATWG §13.2.6.3）：`dd`, `dt`, `li`, `optgroup`, `option`, `p`, `rb`, `rp`, `rt`, `rtc` | ✅ |
| Foster Parenting：表格模式下非表格內容插入到 `<table>` 前方 | ✅ |
| Active Formatting Elements 重建（含 Noah's Ark 限制 3 筆） | ✅ |
| FMT_MARKER 隔離（`<td>` / `<th>` / `<caption>` 邊界） | ✅ |
| Adoption Agency Algorithm（WHATWG §13.2.6.4 完整 outer/inner loop） | ✅ |
| 全 14 種 Formatting Elements：`a`, `b`, `big`, `code`, `em`, `font`, `i`, `nobr`, `s`, `small`, `strike`, `strong`, `tt`, `u` | ✅ |
| 4 種 Scope 類型：General / List Item / Button / Table，各附 `has_element_in_*_scope()` | ✅ |
| Special Elements 完整列表（WHATWG §13.2.6.1，含 `address` ~ `xmp` 共 ~70 個元素） | ✅ |
| Quirks / Limited-Quirks / No-Quirks 判定（依 DOCTYPE public/system ID） | ✅ |
| Quirks 模式對樹構建的實際套用（`<table>` 開始時不自動關閉 `<p>`） | ✅ |
| `<option>` / `<optgroup>` Start Tag Auto-close | ✅ |
| Node 屬性（`attrs[]`）保留至 DOM 節點 | ✅ |

### Fragment Parsing（片段解析）

| 功能 | 狀態 |
|------|------|
| `build_fragment_from_input(input, context_tag)` API | ✅ |
| Context Element 決定 Tokenizer 初始狀態（RCDATA / RAWTEXT / Script Data） | ✅ |
| Context Element 決定初始 Insertion Mode（table / tr / td / select / head 等） | ✅ |
| Context Element 不出現在輸出中 | ✅ |

### Encoding Sniffing（編碼嗅探，WHATWG §13.2.3）

| 功能 | 狀態 |
|------|------|
| BOM 偵測（UTF-8 / UTF-16 LE / UTF-16 BE） | ✅ |
| Transport Layer Hint（`--charset` 命令列參數） | ✅ |
| Meta Prescan（`<meta charset>` / `<meta http-equiv="Content-Type">`） | ✅ |
| 預設 UTF-8 Fallback | ✅ |
| 39 種 WHATWG 標準編碼（~220 個 label alias），`bsearch()` 查找 | ✅ |
| glibc `iconv` 編碼轉換（`#ifdef HAVE_ICONV` 保護） | ✅ |
| 內建 UTF-16 LE/BE → UTF-8 轉換器（含 Surrogate Pair） | ✅ |
| `replacement` 編碼（回傳 U+FFFD）、`x-user-defined`（0x80-0xFF → U+F780-U+F7FF） | ✅ |

### Serialization（序列化）

| 功能 | 狀態 |
|------|------|
| `tree_serialize_html()` 將 DOM Tree 序列化回 HTML 字串 | ✅ |
| Void Elements 不輸出 End Tag | ✅ |
| Raw Text Elements（`<script>` / `<style>`）內容不做 Entity 轉換 | ✅ |
| RCDATA Elements（`<textarea>` / `<title>`）內容做 Entity 轉換 | ✅ |
| 一般文字 `&amp;` / `&lt;` / `&gt;` 轉換 | ✅ |
| 屬性值 `&quot;` / `&amp;` 轉換 | ✅ |

---

## 與完整 WHATWG 規範的差距

雖然本專案已能正確解析絕大多數真實網頁，但與工業級瀏覽器引擎（Blink, WebKit, Gecko）相比仍有差距：

| 缺口 | 影響範圍 | 說明 |
|------|---------|------|
| **Foreign Content（SVG / MathML）** | 中 | 缺少命名空間切換、Integration Points、外國元素自閉合。SVG/MathML 被視為普通 HTML 元素。 |
| **`<template>` 的 Document Fragment** | 低 | ✅ 已支援 `content` wrapper（序列化時跳過 wrapper）。 |
| **完整 Parser Error Recovery** | 低 | 大部分 Error Case 已隱式處理（忽略/容錯），但未系統性驗證每一個 WHATWG 定義的 parse error 分支。 |
| **Scripting / `document.write()`** | N/A | 這是純 Parser，不執行 JavaScript，無法處理 Re-entrant Parsing。 |
| **`<form>` 元素指標** | 低 | 未維護 WHATWG 定義的 "form element pointer"，不影響大多數場景。 |
| **`<frameset>` 模式** | 低 | 未實作 `in frameset` / `after frameset` 模式。現代網頁幾乎不使用。 |
| **Marker 補充** | 低 | `applet` / `marquee` / `object` 開啟時應推入 FMT_MARKER，目前僅在 `td` / `th` / `caption` 推入。 |
| **Entity 查找效能** | 低 | 目前使用線性搜尋，可優化為 Trie 或 Hash Map。 |

完整的功能差距分析請參見 [list.md](list.md)。

---

## 建置與使用

### 編譯

```bash
make                    # 產生 ./parse_html
make parse_fragment_demo  # 產生 ./parse_fragment_demo
make serialize_demo       # 產生 ./serialize_demo
```

### 解析完整 HTML 文件（輸出 ASCII Tree）

```bash
./parse_html tests/sample.html
```

輸出範例：
```
--- tests/sample.html ---
DOCUMENT
|-- DOCTYPE name="html"
\-- ELEMENT name="html"
    |-- ELEMENT name="head"
    |   \-- ELEMENT name="title"
    |       \-- TEXT data="Test"
    \-- ELEMENT name="body"
        |-- ELEMENT name="h1"
        |   \-- TEXT data="Hello"
        \-- ELEMENT name="p"
            \-- TEXT data="World"
```

### 指定傳輸層編碼提示

```bash
./parse_html --charset windows-1252 tests/encoding_meta_charset.html
```

### 片段解析（類似 `innerHTML`）

```bash
./parse_fragment_demo div tests/fragment_basic.html
```

### 序列化（DOM Tree → HTML）

```bash
make serialize_demo
./serialize_demo tests/attrs_basic.html
```

### 啟用 Parse Error 輸出

Parse Error 預設輸出到 stdout（格式：`[parse error] line=N col=N: message`）。

---

## 測試

```bash
make test-html       # 執行 30 個完整文件解析測試
make test-fragment   # 執行 12 個片段解析測試（shell script 驗證）
make test-serialize  # 執行序列化測試
make test-encoding   # 執行 8 個編碼嗅探測試
make test-all        # 全部執行
```

測試檔案位於 `tests/` 目錄，涵蓋：

| 類別 | 測試檔案數 | 涵蓋場景 |
|------|-----------|---------|
| 基本結構 | 3 | `sample.html`, `attrs_basic.html`, `attrs_void.html` |
| 自動關閉 | 3 | `autoclose.html`, `autoclose_extended.html`, `implied_end_tags.html` |
| Character References | 3 | `charrefs.html`, `charrefs_more.html`, `charref_attr_vs_text.html` |
| 表格 | 4 | `table_full.html`, `table_caption.html`, `foster_parenting.html`, `select_in_table.html` |
| 格式化 / AAA | 4 | `formatting_rebuild.html`, `formatting_misnest.html`, `formatting_scope.html`, `aaa_basic.html` |
| Quirks 模式 | 6 | `quirks_p_table.html`, `no_quirks_p_table.html`, `doctype_modes_*.html`, `quirks_table*.html` |
| Script / RCDATA | 2 | `rcdata_rawtext_script.html`, `script_escaped.html` |
| 片段解析 | 12 | `frag_01` ~ `frag_12`, `fragment_basic.html` |
| 編碼 | 8 | `encoding_utf8_bom.html` ~ `encoding_default_utf8.html` |
| 其他 | 5 | `parse_errors.html`, `null_replacement.html`, `scoping.html`, `big_test.html`, `after_body.html` |

---

## 關鍵檔案

| 檔案 | 說明 |
|------|------|
| `src/token.h/c` | Token 結構與生命週期（init / free） |
| `src/tokenizer.h/c` | 有狀態詞法分析器、Entity 解碼、Character Reference 處理 |
| `src/tree.h/c` | Node 結構、子節點操作、ASCII Dump、HTML Serialization |
| `src/tree_builder.h/c` | 13 種 Insertion Mode、Auto-close、AAA、Foster Parenting、Quirks |
| `src/encoding.h/c` | WHATWG 編碼嗅探、39 種編碼支援、BOM/Meta Prescan |
| `src/parse_file_demo.c` | 完整文件解析 CLI 入口 |
| `src/parse_fragment_demo.c` | 片段解析 CLI 入口 |
| `src/serialize_demo.c` | 序列化示範 CLI 入口 |
| `entities.tsv` | WHATWG 完整命名字元參考表（2,231 條，Tab 分隔） |
| `ARCHITECTURE.md` | 詳細架構文件（模組設計、資料結構、演算法） |
| `list.md` | 功能完成進度與 WHATWG 差距分析 |
| `CLAUDE.md` | AI 開發輔助指南 |

---

## 注意事項

- `entities.tsv` 必須從執行時的工作目錄可訪問。若從其他目錄執行，Entity 解析會回退至內建 ~30 個常用實體。
- 僅含空白的文字節點會在 Tree Construction 時被捨棄（`is_all_whitespace` 過濾），這符合瀏覽器行為。
- Encoding 模組需要 `iconv`（glibc 提供）。編譯時以 `-DHAVE_ICONV` 啟用，不啟用則僅支援 UTF-8 和 UTF-16。
- 本專案不執行 JavaScript，不支援 `document.write()` 等 Re-entrant Parsing。
