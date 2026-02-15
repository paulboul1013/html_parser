# html_parser

一個用純 C 語言從零實作的 HTML5 解析器，目標是符合 [WHATWG HTML Living Standard](https://html.spec.whatwg.org/multipage/parsing.html) 的核心規範。

專案以教學與深入理解瀏覽器核心為目的，不依賴任何第三方函式庫（僅可選用 glibc `iconv` 處理編碼轉換），完整實作了 Tokenizer、Tree Construction、Fragment Parsing、Foreign Content、Serialization 與 Encoding Sniffing 六大模組。功能覆蓋與主流 HTML parser（html5lib、html5ever、Gumbo）齊平。

---

## 專案特色

- **純 C11 實作**：零依賴、單一 `make` 即可編譯，可在任何 POSIX 環境執行
- **兩階段管道架構**：Tokenizer（詞法分析）→ Tree Builder（樹構建），清晰分離
- **WHATWG 規範覆蓋率 ~99%**：80 種 Tokenizer 狀態全部實作、20 種 Tree Construction 插入模式（含 frameset 已淘汰的 3 種外）
- **完整 Adoption Agency Algorithm**：正確處理 `<b><div></b>` 等格式化元素的錯誤巢套
- **Foreign Content 支援**：SVG / MathML 命名空間切換、Integration Points、CDATA 區段、元素/屬性大小寫修正
- **39 種 WHATWG 編碼支援**：BOM 偵測 → 傳輸層 hint → meta prescan → 預設 UTF-8，含 ISO-2022-JP 內建狀態機解碼器、re-encoding 機制
- **HTML Serialization**：DOM Tree 序列化回 HTML 字串（含 void/raw text/RCDATA/foreign/template 處理）
- **`<form>` element pointer**：form-associated 元素自動關聯至所屬 `<form>`
- **約 8,800 行 C 程式碼**（不含測試與資料檔）

---

## 架構總覽

```
輸入位元組
  → Encoding Sniffing（encoding.c）— 偵測 BOM / meta charset / hint → 轉換為 UTF-8
  → CR/LF 正規化 + NULL 預處理（U+0000 → U+FFFD）
  → Tokenizer（tokenizer.c）— 狀態機產生 Token 串流（含 CDATA 區段）
  → Tree Builder（tree_builder.c）— 消費 Token，依 Insertion Mode 建構節點樹
  → Foreign Content（foreign.c）— SVG/MathML 命名空間處理
  → 輸出：ASCII Tree Dump（tree.c）或 HTML Serialization（tree.c）
```

### 模組職責

| 模組 | 檔案 | 行數 | 職責 |
|------|------|------|------|
| Token | `token.h/c` | ~70 | Token 結構定義（6 種類型）、生命週期管理 |
| Tokenizer | `tokenizer.h/c` | ~1,620 | 狀態機（80 種狀態）、Character Reference 解碼（完整 `entities.tsv`）、Comment/DOCTYPE 解析、CDATA、PLAINTEXT、Script Data Escaped/Double Escaped |
| Tree | `tree.h/c` | ~500 | Node 結構（含命名空間）、子節點操作、ASCII Dump、HTML Serialization |
| Tree Builder | `tree_builder.h/c` | ~4,700 | 20 種 Insertion Mode、Auto-close、Foster Parenting、AFE/AAA、Quirks、Foreign Content 整合、Form element pointer、Generate implied end tags、Stop parsing |
| Foreign | `foreign.h/c` | ~420 | Breakout tags、SVG/MathML 名稱修正、Integration Points、命名空間感知 scope/special |
| Encoding | `encoding.h/c` | ~1,170 | WHATWG 編碼嗅探、39 種編碼查找表、BOM/meta prescan、iconv/內建 UTF-16/ISO-2022-JP 轉換、re-encoding |
| JIS0208 | `jis0208_table.h` | ~710 | JIS X 0208 pointer → Unicode codepoint 查找表（WHATWG Encoding Standard） |
| CLI | `parse_file_demo.c` | ~95 | 完整文件解析入口 |
| CLI | `parse_fragment_demo.c` | ~77 | Fragment 解析入口 |
| CLI | `serialize_demo.c` | ~65 | 序列化示範入口 |

---

## 已實現功能

### Tokenizer（詞法分析）

| 功能 | 狀態 |
|------|------|
| 完整 80 種 Tokenizer 狀態機 | ✅ |
| Attribute 解析（雙引號 / 單引號 / 無引號 / Boolean） | ✅ |
| Comment 完整狀態機（10 種 Comment 狀態，含 `<!-->` / `<!--->` 邊緣情況） | ✅ |
| DOCTYPE 解析（PUBLIC / SYSTEM identifier） | ✅ |
| RCDATA / RAWTEXT / Script Data / PLAINTEXT 狀態 | ✅ |
| Script Data Escaped / Double Escaped（`<!--` / `-->` 偵測，18 個子狀態） | ✅ |
| RCDATA/RAWTEXT 完整子狀態（6 個子狀態，含 `</tag/>` 自閉合 end tag） | ✅ |
| Named Character References（完整 WHATWG 2,231 實體，`entities.tsv`） | ✅ |
| Numeric Character References（十進位 / 十六進位，含無分號容錯、範圍修正表） | ✅ |
| Noncharacter / Surrogate / Control 偵測（WHATWG §13.2.5.80） | ✅ |
| NULL 字元替換（U+0000 → U+FFFD）、CR/LF 正規化 | ✅ |
| CDATA 區段解析（`<![CDATA[...]]>`，Foreign Content 中啟用） | ✅ |
| Parse Error 報告（line:col 定位，`HTMLPARSER_PARSE_ERRORS=1` 啟用） | ✅ |

### Tree Construction（樹構建）

| 功能 | 狀態 |
|------|------|
| 20 種 Insertion Mode（含 in head noscript、in table text） | ✅ |
| Auto-close：`<p>` / `<li>` / `<dt>` / `<dd>` / `<h1>`-`<h6>` / 表格 / `<option>` / `<optgroup>` | ✅ |
| Generate Implied End Tags + Generate All Implied End Tags Thoroughly | ✅ |
| Foster Parenting：表格模式下非表格內容插入到 `<table>` 前方 | ✅ |
| In Table Text 收集模式：表格內文字緩衝 + 非空白 foster parent | ✅ |
| Active Formatting Elements 重建（含 Noah's Ark attribute 比對，限制 3 筆） | ✅ |
| FMT_MARKER 隔離（`<td>` / `<th>` / `<caption>` / `<applet>` / `<marquee>` / `<object>` / `<template>`） | ✅ |
| Adoption Agency Algorithm（WHATWG §13.2.6.4 完整 outer/inner loop） | ✅ |
| 全 14 種 Formatting Elements | ✅ |
| 5 種 Scope 類型：General / List Item / Button / Table / Select，命名空間感知 | ✅ |
| Quirks / Limited-Quirks / No-Quirks 判定與套用 | ✅ |
| `<form>` element pointer：form-associated 元素自動關聯 | ✅ |
| `<template>` Document Fragment（`content` wrapper，序列化時跳過） | ✅ |
| `<body>` / `<html>` 重複出現 → 合併屬性 | ✅ |
| `<input>` type=hidden 在 table 中直接插入（不 foster parent） | ✅ |
| `<noscript>` in head（scripting disabled 模式） | ✅ |
| Stop Parsing（§13.2.6.5）：per-mode EOF 處理、棧清理 | ✅ |

### Foreign Content（SVG / MathML，WHATWG §13.2.6.7）

| 功能 | 狀態 |
|------|------|
| SVG / MathML 命名空間進入 / 離開（breakout tags） | ✅ |
| SVG 元素名稱大小寫修正（37 條） | ✅ |
| SVG 屬性名稱大小寫修正（57 條） | ✅ |
| MathML 屬性名稱修正 | ✅ |
| Integration Points（HTML + MathML text） | ✅ |
| `<font>` with `color`/`face`/`size` 屬性 → 中斷外國內容 | ✅ |
| 外國元素自閉合行為 | ✅ |
| CDATA 區段（tokenizer `allow_cdata` flag） | ✅ |

### Fragment Parsing（片段解析）

| 功能 | 狀態 |
|------|------|
| `build_fragment_from_input(input, context_tag, encoding, confidence, change_encoding)` API | ✅ |
| Context Element 決定 Tokenizer 初始狀態與 Insertion Mode | ✅ |
| Context Element 不出現在輸出中 | ✅ |
| Context element encoding 繼承（WHATWG §14.4 step 5） | ✅ |
| `<template>` 作為 context：template insertion modes stack | ✅ |

### Encoding Sniffing（編碼嗅探，WHATWG §13.2.3）

| 功能 | 狀態 |
|------|------|
| BOM 偵測（UTF-8 / UTF-16 LE / UTF-16 BE） | ✅ |
| Transport Layer Hint（`--charset` 命令列參數） | ✅ |
| Meta Prescan（`<meta charset>` / `<meta http-equiv="Content-Type">`） | ✅ |
| 預設 UTF-8 Fallback | ✅ |
| 39 種 WHATWG 標準編碼（~220 個 label alias），`bsearch()` 查找 | ✅ |
| 內建 UTF-16 LE/BE → UTF-8 轉換器（含 Surrogate Pair） | ✅ |
| 內建 ISO-2022-JP → UTF-8 狀態機解碼器（含 JIS X 0208 查找表） | ✅ |
| glibc `iconv` 編碼轉換（`#ifdef HAVE_ICONV`） | ✅ |
| `replacement` 編碼 → U+FFFD、`x-user-defined` → U+F780-U+F7FF | ✅ |
| Encoding confidence（certain / tentative / irrelevant） | ✅ |
| Re-encoding（WHATWG §13.2.3.5：TENTATIVE 時 meta charset 觸發重新解碼） | ✅ |

### Serialization（序列化）

| 功能 | 狀態 |
|------|------|
| `tree_serialize_html()` 將 DOM Tree 序列化回 HTML 字串 | ✅ |
| Void Elements 不輸出 End Tag | ✅ |
| Raw Text / RCDATA / 一般文字 Entity 轉換 | ✅ |
| 屬性值 `&quot;` / `&amp;` 轉換 | ✅ |
| `<template>` content 序列化（跳過 wrapper） | ✅ |
| Foreign 元素自閉合（`<circle />`） | ✅ |

---

## 與主流 Parser 功能比較

| 功能 | html5lib (Python) | html5ever (Rust) | Gumbo (Google C) | 本專案 |
|------|-------------------|-------------------|-------------------|--------|
| Tokenizer（80 states） | ✅ | ✅ | ✅ | ✅ |
| Tree construction（AAA、Foster、Scope） | ✅ | ✅ | ✅ | ✅ |
| Foreign Content（SVG/MathML） | ✅ | ✅ | ✅ | ✅ |
| Fragment parsing | ✅ | ✅ | ✅ | ✅ |
| Encoding sniffing + re-encoding | ✅ | ✅ | ❌ | ✅ |
| HTML serialization | ✅ | ✅ | ❌ | ✅ |
| Quirks mode detection | ✅ | ✅ | ✅ | ✅ |
| Frameset modes（已淘汰） | ✅ | ✅ | ✅ | 🟥 |

唯一未實作的是已淘汰的 `<frameset>` 系列模式（3 種），現代網頁不使用。

完整的功能差距分析請參見 [list.md](list.md)。

---

## 建置與使用

### 編譯

```bash
make                      # 產生 ./parse_html
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

SVG 命名空間範例：
```
\-- ELEMENT name="body"
    \-- ELEMENT(svg) name="svg" [viewBox="0 0 100 100"]
        \-- ELEMENT(svg) name="circle" [cx="50" cy="50" r="40"]
```

Form 關聯範例：
```
\-- ELEMENT name="form" [id="f1"]
    \-- ELEMENT name="input" [type="text"] form="f1"
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

```bash
HTMLPARSER_PARSE_ERRORS=1 ./parse_html tests/parse_errors.html
```

---

## 測試

```bash
make test-html       # 執行完整文件解析測試
make test-fragment   # 執行 14 個片段解析測試（shell script 驗證）
make test-serialize  # 執行序列化測試
make test-encoding   # 執行 11 個編碼嗅探測試
make test-all        # 全部執行（test-html + test-fragment + test-encoding）
```

測試檔案位於 `tests/` 目錄（共 93 個 HTML 檔案），涵蓋：

| 類別 | 涵蓋場景 |
|------|---------|
| 基本結構 | 標籤、屬性（雙引號/單引號/無引號/Boolean/邊緣情況） |
| 自動關閉 | `<p>`/`<li>`/`<dt>`/`<dd>`/`<h1>`-`<h6>`/表格/`<option>` |
| Character References | Named/Numeric/屬性 context/noncharacter/surrogate |
| 表格 | 完整表格元素、foster parenting、in table text、caption、select in table |
| 格式化 / AAA | rebuild、misnest、scope、Noah's Ark（4 個壓力測試） |
| Quirks 模式 | quirks/limited-quirks/no-quirks 各種 DOCTYPE |
| Script / RCDATA / RAWTEXT | 完整狀態機、escaped/double-escaped |
| Foreign Content | SVG/MathML 基本、大小寫修正、breakout、integration point、CDATA、巢套 |
| Template | Document Fragment、content wrapper |
| 片段解析 | 13 個 fragment 測試（含 CR/LF、formatting、table、select） |
| 編碼 | UTF-8 BOM、UTF-16 LE/BE、meta charset、Shift_JIS、GBK、ISO-2022-JP、re-encoding |
| 其他 | NULL 替換、scoping、parse errors、stop parsing、noscript in head、屬性合併 |

---

## 關鍵檔案

| 檔案 | 說明 |
|------|------|
| `src/token.h/c` | Token 結構與生命週期（init / free） |
| `src/tokenizer.h/c` | 有狀態詞法分析器（80 種狀態）、Entity 解碼、CDATA 區段 |
| `src/tree.h/c` | Node 結構（含命名空間、form_owner）、子節點操作、ASCII Dump、HTML Serialization |
| `src/tree_builder.h/c` | 20 種 Insertion Mode、Auto-close、AAA、Foster Parenting、Quirks、Foreign Content 整合、Form element pointer |
| `src/foreign.h/c` | Foreign Content 查找表、Integration Points、命名空間感知 scope/special |
| `src/encoding.h/c` | WHATWG 編碼嗅探、39 種編碼支援、BOM/Meta Prescan、ISO-2022-JP 內建解碼器 |
| `src/jis0208_table.h` | JIS X 0208 查找表（WHATWG Encoding Standard） |
| `entities.tsv` | WHATWG 完整命名字元參考表（2,231 條，Tab 分隔） |
| `ARCHITECTURE.md` | 詳細架構文件（模組設計、資料結構、演算法） |
| `list.md` | 功能完成進度與 WHATWG 差距分析 |

---

## 注意事項

- `entities.tsv` 必須從執行時的工作目錄可訪問。若從其他目錄執行，Entity 解析會回退至內建 ~30 個常用實體。
- 僅含空白的文字節點會在 Tree Construction 時被捨棄（`is_all_whitespace` 過濾），這符合瀏覽器行為。
- Encoding 模組在無 iconv 環境下仍可處理 UTF-8、UTF-16 和 ISO-2022-JP。其他編碼需要 `iconv`（glibc 提供），編譯時以 `-DHAVE_ICONV` 啟用。
- 本專案不執行 JavaScript，不支援 `document.write()` 等 Re-entrant Parsing。這與所有同類純 parser（html5lib、html5ever、Gumbo）一致。
