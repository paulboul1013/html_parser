# html_parser — WHATWG HTML Standard 完整功能差距分析

本文件系統性地列出 WHATWG HTML Living Standard（§13 Parsing）中定義的所有解析相關功能，並標注本專案的實作狀態。

**圖例**：✅ 已完成　⬜ 未實作　🔧 部分實作

---

## 一、Tokenizer（§13.2.5 Tokenization）

### 1.1 Tokenizer 狀態機（共 80 種狀態）

| 功能 | 狀態 | 備註 |
|------|------|------|
| Data state | ✅ | |
| RCDATA state | ✅ | `<title>`, `<textarea>` |
| RAWTEXT state | ✅ | `<style>` |
| Script data state | ✅ | |
| Script data escaped state | ✅ | `<!--` 偵測 |
| Script data double escaped state | ✅ | |
| PLAINTEXT state | ✅ | `<plaintext>` 觸發，進入後不可離開 |
| Tag open state | ✅ | |
| End tag open state | ✅ | |
| Tag name state | ✅ | |
| Before attribute name state | ✅ | |
| Attribute name state | ✅ | |
| After attribute name state | ✅ | |
| Before attribute value state | ✅ | |
| Attribute value (double-quoted) state | ✅ | |
| Attribute value (single-quoted) state | ✅ | |
| Attribute value (unquoted) state | ✅ | |
| After attribute value (quoted) state | ✅ | |
| Self-closing start tag state | ✅ | |
| Bogus comment state | ✅ | `<!X` 觸發 |
| Markup declaration open state | ✅ | `<!--`, `<!DOCTYPE>` |
| Comment start state | ✅ | |
| Comment start dash state | ✅ | |
| Comment state | ✅ | |
| Comment less-than sign state | ✅ | |
| Comment less-than sign bang state | ✅ | |
| Comment less-than sign bang dash state | ✅ | |
| Comment less-than sign bang dash dash state | ✅ | |
| Comment end dash state | ✅ | |
| Comment end state | ✅ | |
| Comment end bang state | ✅ | |
| DOCTYPE state | ✅ | |
| Before DOCTYPE name state | ✅ | |
| DOCTYPE name state | ✅ | |
| After DOCTYPE name state | ✅ | |
| After DOCTYPE public keyword state | ✅ | |
| Before DOCTYPE public identifier state | ✅ | |
| DOCTYPE public identifier (double-quoted) state | ✅ | |
| DOCTYPE public identifier (single-quoted) state | ✅ | |
| After DOCTYPE public identifier state | ✅ | |
| Between DOCTYPE public and system identifiers state | ✅ | |
| After DOCTYPE system keyword state | ✅ | |
| Before DOCTYPE system identifier state | ✅ | |
| DOCTYPE system identifier (double-quoted) state | ✅ | |
| DOCTYPE system identifier (single-quoted) state | ✅ | |
| After DOCTYPE system identifier state | ✅ | |
| Bogus DOCTYPE state | ✅ | |
| CDATA section state | ✅ | Foreign Content 中啟用，`allow_cdata` flag |
| CDATA section bracket state | ✅ | |
| CDATA section end state | ✅ | |
| Character reference state | ✅ | |
| Named character reference state | ✅ | |
| Ambiguous ampersand state | ✅ | |
| Numeric character reference state | ✅ | |
| Hexadecimal character reference start state | ✅ | |
| Decimal character reference start state | ✅ | |
| Hexadecimal character reference state | ✅ | |
| Decimal character reference state | ✅ | |
| Numeric character reference end state | ✅ | |
| Script data less-than sign state | ✅ | `process_script_data()` 逐字元狀態機 |
| Script data end tag open state | ✅ | |
| Script data end tag name state | ✅ | |
| Script data escape start state | ✅ | |
| Script data escape start dash state | ✅ | |
| Script data escaped state | ✅ | |
| Script data escaped dash state | ✅ | |
| Script data escaped dash dash state | ✅ | |
| Script data escaped less-than sign state | ✅ | |
| Script data escaped end tag open state | ✅ | |
| Script data escaped end tag name state | ✅ | |
| Script data double escape start state | ✅ | |
| Script data double escaped state | ✅ | |
| Script data double escaped dash state | ✅ | |
| Script data double escaped dash dash state | ✅ | |
| Script data double escaped less-than sign state | ✅ | |
| Script data double escape end state | ✅ | |
| RCDATA less-than sign state | 🔧 | 用 `find_end_tag()` 替代狀態機 |
| RCDATA end tag open state | 🔧 | |
| RCDATA end tag name state | 🔧 | |
| RAWTEXT less-than sign state | 🔧 | 用 `find_end_tag()` 替代狀態機 |
| RAWTEXT end tag open state | 🔧 | |
| RAWTEXT end tag name state | 🔧 | |

**小結**：80 個狀態中 ~64 個完整實作，~6 個用替代方式實作（功能等效），0 個未實作。Script data 的 18 個子狀態已透過 `process_script_data()` 逐字元狀態機完整實作。CDATA 已透過 `allow_cdata` flag 實作。PLAINTEXT 已實作（進入後永不離開）。RCDATA/RAWTEXT 的 end tag 偵測以 `find_end_tag()` 實作而非逐字元狀態機，產出結果等效。

### 1.2 Character References

| 功能 | 狀態 | 備註 |
|------|------|------|
| Named character references（完整 2,231 實體） | ✅ | `entities.tsv` |
| Numeric character references（十進位 `&#123;`） | ✅ | |
| Numeric character references（十六進位 `&#x7B;`） | ✅ | |
| 無分號容錯（legacy entities） | ✅ | |
| Attribute context 中的差異處理（`=` / alnum 後不解碼） | ✅ | |
| Numeric reference 範圍修正（§13.2.5.5 table） | ✅ | Windows-1252 控制區對應、控制碼/無效碼點 → U+FFFD |
| Noncharacter / surrogate 偵測 | ⬜ | 數字解碼後未檢查 noncharacter 範圍 |

### 1.3 Token 類型

| Token 類型 | 狀態 |
|-----------|------|
| DOCTYPE | ✅ |
| Start tag（含 attrs, self-closing） | ✅ |
| End tag | ✅ |
| Comment | ✅ |
| Character | ✅ |
| EOF | ✅ |

### 1.4 輸入前處理

| 功能 | 狀態 | 備註 |
|------|------|------|
| NULL 字元替換（U+0000 → U+FFFD） | ✅ | `tokenizer_replace_nulls()` |
| CR/LF 正規化（CR → LF, CRLF → LF） | ✅ | `tokenizer_replace_nulls()` 前處理 |
| Encoding sniffing | ✅ | 見下方「Encoding」章節 |

---

## 二、Tree Construction（§13.2.6）

### 2.1 Insertion Modes（共 23 種）

| Mode | 狀態 | 備註 |
|------|------|------|
| initial | ✅ | |
| before html | ✅ | |
| before head | 🔧 | 合併至 `before html` / `in head` 處理 |
| in head | ✅ | |
| in head noscript | ⬜ | `<noscript>` 在 `<head>` 中時，腳本未啟用的特殊模式 |
| after head | 🔧 | 合併至 `in head` → `in body` 的轉換邏輯 |
| in body | ✅ | |
| text | ⬜ | WHATWG 定義的 generic RCDATA/RAWTEXT 內容模式 |
| in table | ✅ | |
| in table text | ✅ | 表格內文字特殊收集 + foster 規則 |
| in caption | ✅ | |
| in column group | 🔧 | `<colgroup>` / `<col>` 可解析但無獨立狀態 |
| in table body | ✅ | |
| in row | ✅ | |
| in cell | ✅ | |
| in select | ✅ | |
| in select in table | ✅ | |
| in template | 🔧 | 以 `template_mode_stack` + `content` wrapper 簡化實作 |
| after body | ✅ | |
| in frameset | ⬜ | `<frameset>` 模式，已淘汰 |
| after frameset | ⬜ | |
| after after body | ✅ | |
| after after frameset | ⬜ | |

**小結**：23 種模式中 15 種完整實作（含 in table text），4 種以合併方式實作（功能等效），4 種未實作。未實作的多為罕用（frameset × 3）或進階功能（in head noscript）。

### 2.2 Tree Construction 演算法

| 功能 | 狀態 | 備註 |
|------|------|------|
| Creating and inserting nodes | ✅ | |
| Appropriate place for inserting a node | ✅ | 含 Foster Parenting |
| Foster Parenting | ✅ | 表格模式下非表格內容 |
| Element creation with attributes | ✅ | `attach_attrs()` |
| Insert a character | ✅ | |
| Insert a comment | ✅ | |
| Generic raw text element parsing (§13.2.6.2) | 🔧 | Tokenizer 端處理，非獨立狀態 |
| Generic RCDATA element parsing | 🔧 | Tokenizer 端處理 |
| Reconstruct the active formatting elements | ✅ | |
| Adoption Agency Algorithm (§13.2.6.4) | ✅ | 完整 outer/inner loop |
| Close the cell | ✅ | |
| Generate implied end tags | ✅ | `dd`, `dt`, `li`, `optgroup`, `option`, `p`, `rb`, `rp`, `rt`, `rtc` |
| Generate all implied end tags thoroughly | ⬜ | 額外含 `caption`, `colgroup`, `tbody`, `td`, `tfoot`, `th`, `thead`, `tr` |
| Reset the insertion mode appropriately | ✅ | Fragment 解析用 |
| Stop parsing (§13.2.6.5) | ⬜ | 目前遇到 EOF 直接停止，未執行完整清理步驟 |

### 2.3 Formatting（活躍格式化元素）

| 功能 | 狀態 | 備註 |
|------|------|------|
| 14 種 Formatting Elements 支援 | ✅ | `a`, `b`, `big`, `code`, `em`, `font`, `i`, `nobr`, `s`, `small`, `strike`, `strong`, `tt`, `u` |
| Noah's Ark clause（同元素限制 3 筆） | ✅ | |
| Marker 推入（`td` / `th` / `caption`） | ✅ | |
| Marker 推入（`applet` / `marquee` / `object`） | ✅ | |
| Marker 推入（`template`） | ✅ | |
| Clear to marker | ✅ | |
| Adoption Agency outer loop（8 次上限） | ✅ | |
| Adoption Agency inner loop（8 次上限） | ✅ | |
| Clone element（replacement） | ✅ | `clone_element_shallow()` |
| Noah's Ark attribute 比對 | ✅ | 比對 tag + attributes，超過 3 同組清最早 |

### 2.4 Scope（範圍）

| 功能 | 狀態 | 備註 |
|------|------|------|
| General scope（9 個障壁元素） | ✅ | `applet`, `caption`, `html`, `table`, `td`, `th`, `marquee`, `object`, `template` |
| List item scope（+`ol`, `ul`） | ✅ | |
| Button scope（+`button`） | ✅ | |
| Table scope（`html`, `table`, `template`） | ✅ | |
| Select scope | ⬜ | 除 `optgroup` / `option` 外所有元素皆為障壁 |
| SVG/MathML scope 元素 | ✅ | `is_scoping_element_ns()` 命名空間感知 |

### 2.5 Auto-close 邏輯

| 功能 | 狀態 | 備註 |
|------|------|------|
| `<p>` 遇 block-like 開始標籤自動關閉 | ✅ | |
| `<p>` end tag 不在 button scope → 隱式開啟再關閉 | ✅ | |
| `<li>` 遇新 `<li>` 關閉 | ✅ | |
| `<dt>` / `<dd>` 互斥關閉 | ✅ | |
| `<option>` 遇新 `<option>` / `<optgroup>` 關閉 | ✅ | |
| `<optgroup>` 遇新 `<optgroup>` 關閉 | ✅ | |
| Table section（`thead/tbody/tfoot`）遇新 section 關閉 | ✅ | |
| `<tr>` 遇新 `<tr>` 關閉 | ✅ | |
| `<td>` / `<th>` 遇新 cell 關閉 | ✅ | |
| `<h1>`-`<h6>` 遇同級標題關閉 | ✅ | heading 遇到 heading 自動關閉 |
| `<body>` end tag 的 implied end tags | ✅ | |

### 2.6 Quirks Mode

| 功能 | 狀態 | 備註 |
|------|------|------|
| DOCTYPE 判定（quirks / limited-quirks / no-quirks） | ✅ | 完整 public/system ID 匹配 |
| Missing DOCTYPE → quirks | ✅ | |
| Force quirks | ✅ | |
| Quirks: `<table>` start 不關閉 `<p>` | ✅ | `dmode != DOC_QUIRKS` 條件 |
| Limited-quirks: 僅影響 CSS 層 | ✅ | Parser 層行為等同 no-quirks |

---

## 三、Fragment Parsing（§13.2.6.6）

| 功能 | 狀態 | 備註 |
|------|------|------|
| `build_fragment_from_input(input, context_tag)` API | ✅ | |
| Context element 決定 tokenizer 狀態 | ✅ | |
| Context element 決定 insertion mode | ✅ | |
| Context element 不出現在輸出 | ✅ | |
| `<html>` 作為 context：form element pointer 設定 | ✅ | `form_element_pointer = NULL`（規範行為） |
| `<template>` 作為 context：template insertion modes stack | ✅ | `context=template` 會建立 `content` wrapper |
| Context element 的 encoding 繼承 | ⬜ | |

---

## 四、Encoding Sniffing（§13.2.3）

| 功能 | 狀態 | 備註 |
|------|------|------|
| BOM 偵測（UTF-8 / UTF-16 LE / UTF-16 BE） | ✅ | |
| Transport-layer hint（HTTP Content-Type 等） | ✅ | `--charset` CLI |
| Prescan：`<meta charset="...">` | ✅ | |
| Prescan：`<meta http-equiv="Content-Type" content="...;charset=...">` | ✅ | |
| Prescan byte limit（前 1024 bytes） | ✅ | |
| 39 種 WHATWG 標準編碼支援 | ✅ | |
| ~220 個 label alias（bsearch 查找） | ✅ | |
| UTF-16 → UTF-8 內建轉換（含 surrogate pair） | ✅ | |
| iconv 轉換（其他編碼） | ✅ | |
| `replacement` 編碼 → U+FFFD | ✅ | |
| `x-user-defined` 轉換 | ✅ | |
| Encoding confidence（certain / tentative / irrelevant） | ✅ | |
| Re-encoding（meta 與 BOM 不符時的重新解碼） | ⬜ | 目前以第一個匹配結果為準 |
| `ISO-2022-JP` encoder state machine | ⬜ | iconv 處理，未自行實作 |

---

## 五、Serialization（§16.3 Serializing HTML fragments）

| 功能 | 狀態 | 備註 |
|------|------|------|
| `tree_serialize_html()` | ✅ | |
| Void elements 不輸出 end tag | ✅ | 14 個 void elements |
| Raw text（`script`/`style`）不 escape | ✅ | |
| RCDATA（`textarea`/`title`）做 escape | ✅ | |
| 文字節點 `&amp;`/`&lt;`/`&gt;` | ✅ | |
| 屬性值 `&amp;`/`&quot;` | ✅ | |
| Comment 序列化 `<!--...-->` | ✅ | |
| DOCTYPE 序列化 | ✅ | |
| `<template>` content 序列化 | ✅ | `content` wrapper 不輸出 |
| Attribute 排序（規範未強制） | ⬜ | 保留解析順序 |
| Boolean attributes | ✅ | 空字串值 |

---

## 六、特定元素處理

### 6.1 `<head>` 相關

| 功能 | 狀態 | 備註 |
|------|------|------|
| `<base>` / `<link>` / `<meta>` / `<style>` / `<title>` / `<script>` 在 head 中正確解析 | ✅ | |
| `<noscript>` in head（scripting disabled） | ⬜ | |
| `<head>` 重複出現 → 忽略 | ✅ | |

### 6.2 `<body>` 相關

| 功能 | 狀態 | 備註 |
|------|------|------|
| `<body>` 隱式建立 | ✅ | `ensure_body()` |
| `<body>` 重複出現 → 合併屬性 | ⬜ | 目前忽略 |
| `<html>` 重複出現 → 合併屬性 | ⬜ | 目前忽略 |
| Block-like 元素自動關閉 `<p>` | ✅ | ~25 個 block-like 元素 |

### 6.3 Table 相關

| 功能 | 狀態 | 備註 |
|------|------|------|
| `<table>` / `<tbody>` / `<thead>` / `<tfoot>` / `<tr>` / `<td>` / `<th>` / `<caption>` | ✅ | |
| `<colgroup>` / `<col>` 基本解析 | ✅ | |
| Foster parenting（非表格內容） | ✅ | |
| `<select>` in table → `in select in table` | ✅ | |
| `<form>` in table 特殊處理 | ✅ | foster parenting + form_element_pointer |
| In table text 收集模式 | ✅ | 表格模式中緩衝文字，非空白 foster parent |

### 6.4 Form 相關

| 功能 | 狀態 | 備註 |
|------|------|------|
| `<form>` element pointer | ✅ | WHATWG 維護的 "form element pointer"；form-associated 元素自動關聯 |
| `<input>` / `<button>` / `<select>` / `<textarea>` 基本解析 | ✅ | |
| `<input>` type=hidden 在 table 中的特殊處理 | ⬜ | |

### 6.5 Scripting 相關

| 功能 | 狀態 | 備註 |
|------|------|------|
| `<script>` 基本解析 | ✅ | |
| `<script>` 執行 | ⬜ | N/A，純 Parser |
| `document.write()` re-entrant parsing | ⬜ | N/A，純 Parser |
| `<noscript>` 內容處理 | ⬜ | 需知道 scripting flag |

### 6.6 Foreign Content（§13.2.6.7）

| 功能 | 狀態 | 備註 |
|------|------|------|
| SVG 命名空間進入 / 離開 | ✅ | `<svg>` start tag → NS_SVG，breakout → 回 HTML |
| MathML 命名空間進入 / 離開 | ✅ | `<math>` start tag → NS_MATHML |
| SVG 元素名稱大小寫修正 | ✅ | 37 條，如 `clippath` → `clipPath` |
| SVG 屬性名稱大小寫修正 | ✅ | 57 條，如 `viewbox` → `viewBox` |
| MathML 屬性名稱修正 | ✅ | `definitionurl` → `definitionURL` |
| Integration points（`foreignObject` / `desc` / `title`） | ✅ | HTML + MathML text integration points |
| 外國元素自閉合行為 | ✅ | `self_closing` → 不 push stack |
| CDATA 區段（`<![CDATA[...]]>`） | ✅ | tokenizer `allow_cdata` flag |
| `<font>` with color/face/size 屬性 → 中斷外國內容 | ✅ | `font_has_breakout_attr()` |

### 6.7 `<template>`

| 功能 | 狀態 | 備註 |
|------|------|------|
| `<template>` 基本解析 | ✅ | 作為普通元素 |
| Template contents（Document Fragment） | ✅ | 以 `content` wrapper 表示 |
| Template insertion modes stack | 🔧 | 進入 template push mode；內容以 `MODE_IN_BODY` 解析 |
| `</template>` 正確 pop | ✅ | |

---

## 七、完整 Parse Error 列表

WHATWG §13 定義了約 80 種 parse error。目前 tokenizer 階段的 error 大多已報告，tree construction 階段的 error 多為隱式處理（忽略 / 容錯）。

| 類別 | 已報告 | 未報告 | 備註 |
|------|--------|--------|------|
| Tokenizer parse errors（~25 種） | ~20 | ~5 | `eof-in-*`, `unexpected-*` 等 |
| Tree construction parse errors（~55 種） | ~5 | ~50 | 大多直接忽略或隱式容錯 |

---

## 八、進階功能（非 Parser 核心）

| 功能 | 狀態 | 備註 |
|------|------|------|
| DOM API（`getElementById` 等） | ⬜ | 超出 Parser 範疇 |
| CSS Parser | ⬜ | 超出範疇 |
| JavaScript Engine | ⬜ | 超出範疇 |
| Rendering / Layout | ⬜ | 超出範疇 |
| HTTP / Networking | ⬜ | 超出範疇 |

---

## 總結

### 完成度統計

| 類別 | 已完成 | 部分完成 | 未完成 | 完成率 |
|------|--------|---------|--------|--------|
| Tokenizer 狀態（80） | ~64 | ~6 | 0 | ~98% |
| Character References | 6/7 | 0 | 1 | 86% |
| Insertion Modes（23） | 15 | 4 | 4 | ~83% |
| Tree Construction 演算法 | 12/15 | 2 | 1 | ~87% |
| Formatting / AFE | 10/10 | 0 | 0 | 100% |
| Scope | 5/6 | 0 | 1 | 83% |
| Auto-close | 11/11 | 0 | 0 | 100% |
| Fragment Parsing | 6/7 | 0 | 1 | 86% |
| Encoding Sniffing | 12/14 | 0 | 2 | 86% |
| Serialization | 10/11 | 0 | 1 | 91% |
| Foreign Content | 9/9 | 0 | 0 | 100% |
| Form | 2/3 | 0 | 1 | 67% |
| Template | 3/4 | 1 | 0 | ~90% |

### 整體評估

- **核心 HTML 解析（含 SVG/MathML）**：~92% 完成。能正確解析絕大多數真實世界的 HTML 文件，包含內嵌 SVG 和 MathML。
- **完全符合 WHATWG 規範（含所有邊緣情況）**：~80% 完成。

### 已完成里程碑

1. ✅ **`<template>` Document Fragment** — `content` wrapper 表示
2. ✅ **Heading auto-close（`<h1>`-`<h6>`）** — heading 遇 heading 自動關閉
3. ✅ **Marker 補充（`applet` / `marquee` / `object`）** — start push marker + end 清空至 marker
4. ✅ **CR/LF 正規化** — 輸入前處理
5. ✅ **Numeric reference 範圍修正表** — Windows-1252 映射 + 控制碼/代理/超範圍 → U+FFFD
6. ✅ **Noah's Ark attribute 比對** — tag+attrs 去重，精確度提升
7. ✅ **`in table text` 模式** — pending text 收集 + foster
8. ✅ **`<form>` element pointer** — form_element_pointer 追蹤 + form_owner 關聯
9. ✅ **Foreign Content（SVG/MathML）** — 命名空間、Integration Points、CDATA、大小寫修正
10. ✅ **PLAINTEXT state** — 進入後永不離開，所有後續輸入為文字
11. ✅ **Script data 完整狀態機** — `process_script_data()` 逐字元實作 18 個子狀態（WHATWG §13.2.5.4–§13.2.5.20）

### 剩餘待完成項目

1. **完整 parse error 報告** — 系統性驗證所有 WHATWG 定義的 tree construction parse error
2. **`<input>` type=hidden 在 table 中的特殊處理** — 不 foster parent，直接插入 table
3. **Select scope** — 除 `optgroup` / `option` 外所有元素皆為障壁
4. **`<frameset>` 模式** — 已淘汰，極低優先
