# html_parser (C)

這是一個用 C 語言從零實作的 HTML5 Parser，目標是符合 WHATWG HTML Living Standard 的核心規範。
目前已具備完整的詞法分析能力（Tokenizer）與核心的樹建構演算法（Tree Construction），能處理複雜的 HTML 結構與容錯。

---

##  目前已實現功能 (Current Features)

### 1. 完整的詞法分析 (Tokenizer)
- **狀態機**: 完整實作 WHATWG 定義的 Tokenizer 狀態機，包含 `RCDATA` (`<textarea>`, `<title>`)、`RAWTEXT` (`<style>`) 與 `Script Data` (`<script>`) 的複雜狀態。
- **Entity 解碼**: 支援 Named Character References（完整 `entities.tsv` 對照表）、十進位/十六進位編碼，以及無分號 (`;`) 的容錯解析。
- **Script 處理**: 正確處理 `<script>` 內的如 `<!--` 等特殊跳脫序列（Escaped/Double Escaped states）。

### 2. 樹建構演算法 (Tree Construction)
從 Token 串流建構 DOM Tree，實作了以下關鍵演算法：
- **Insertion Modes**: 完整支援 `in head`, `in body`, `in table` (含 `row`, `cell`, `caption`), `in select`, `after body` 等核心模式。
- **Auto-close**: 自動閉合 `p`, `li`, `dt`, `dd` 等區塊級元素。
- **Foster Parenting**: 當在 `table` 結構中出現非表格內容時，自動將其「領養」至表格之前（符合瀏覽器行為）。
- **Active Formatting Elements**: 
    - **Reconstruction**: 在區塊邊界重建 `b`, `i`, `strong` 等格式化元素。
    - **Adoption Agency Algorithm (AAA)**: 處理格式化元素與區塊元素的錯誤巢狀（例如 `<b><div>...</b>`），完整實作了 Noah's Ark 條款與 Element 收養機制。
- **Implied End Tags**: 智慧推斷並生成缺失的結束標籤。

### 3. 進階功能
- **Fragment Parsing**: 支援類似 `innerHTML` 的片段解析，能根據 Context Element 正確設定初始 Tokenizer 狀態與 Insertion Mode。
- **Quirks Mode**: 根據 DOCTYPE 正確切換 `Quirks`、`Limited Quirks` 或 `No Quirks` 模式。
- **Serialization**: 支援將解析後的 DOM Tree 序列化回標準 HTML 字串（含 Entity Escape 處理）。

---

##  與專業 HTML Parser (如 Blink, WebKit) 的差距

雖然本專案已能正確解析絕大多數網頁，但與工業級瀏覽器引擎相比，仍有以下差距：

### 1. 外來內容 (Foreign Content)
- **缺口**: 尚未支援 `SVG` 與 `MathML` 命名空間。
- **影響**: SVG 與 MathML 標籤會被視為普通的 HTML 元素，可能導致屬性解析或結構錯誤（例如 SVG 內的自閉合標籤處理）。

### 2. 編碼偵測 (Encoding Sniffing)
- **缺口**: 本專案假設輸入皆為 UTF-8。
- **影響**: 瀏覽器會檢查 BOM、`<meta>` charset 或 Content-Type header 來決定編碼，本 Parser 則無此機制。

### 3. 完整的錯誤復原 (Full Error Recovery)
- **缺口**: 雖然所有的 Parse Error 都會被報告，但部分罕見錯誤的復原行為可能未完全符合 Spec 定義的 "stop parsing" 或特定修正邏輯。
- **影響**: 在極端畸形的 HTML 輸入下，產生的 DOM 可能與瀏覽器稍有不同。

### 4. HTML Template 元素
- **缺口**: 對 `<template>` 的支援僅止於基本解析，尚未完整實作 Template Content 的 Document Fragment 隔離機制。

### 5. Scripting & Re-entrant Parsing
- **缺口**: 這是純 Parser，不執行 JavaScript。
- **影響**: 無法處理 `document.write()` 這種會在中途改變 InputStream 的行為（這是瀏覽器 Parser 最複雜的部分之一）。

### 6. 效能優化
- **缺口**: Entity 查找目前使用線性搜尋，DOM 節點配置較為分散。
- **影響**: 解析超大型文件時，效能不如使用 Trie 或 Hash Map 的工業級 Parser。

---

##  建置與使用

### Build
```bash
make
```
會產生可執行檔：`parse_html`

### 解析單一 HTML（輸出 ASCII tree）
```bash
./parse_html tests/sample.html
```

### 跑全部 HTML 測試
```bash
make test-html
```
> `test-html` 會依序解析 `tests/*.html` 並輸出 ASCII tree。

---

##  測試資料 (tests/)
測試檔位於 `tests/`，涵蓋各類場景：
- `sample.html`: 基本結構
- `autoclose.html`: `p`/`li` 自動關閉
- `foster_parenting.html`: Table foster parenting 測試
- `formatting_misnest.html`: AAA 演算法測試 (Adoption Agency)
- `fragment_basic.html`: 片段解析測試

---

## 📖 參考資料
- **WHATWG HTML Standard**: 核心參考標準。
