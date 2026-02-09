# html_parser 待補完整功能清單（最小拆解版）

以下依「優先順序」排列，優先解決最影響正確性的缺口。每一項都是**可獨立實作**的小任務。

---

## P0（最先做）— 核心詞法分析

- **Tokenizer：完整 state machine（核心狀態）** ✅ 已完成
  - Tag open / End tag open / Tag name ✅
  - Attribute name / Attribute value（雙引號 / 單引號 / 無引號）✅
  - Comment 與 Doctype 的完整狀態與容錯 ✅
- **Script data 特殊規則** ✅ 已完成（最小支援）
  - `<script>` 內的 `</script>` 偵測 ✅
  - `script data escaped` / `double escaped` ✅
- **Character references 完整化** ✅ 已完成
  - 加入完整 named entities 表 ✅（`entities.tsv` 已填入完整 WHATWG 列表）
  - 更精確的容錯規則（無 `;`，只在允許的 context 解碼）✅

---

## P1（緊接著做）— 樹構建核心

- **Tree construction：完整 insertion modes（核心缺口）** ✅
  - `in table body`, `in row`, `in cell`, `in caption` ✅
  - `in select`, `in select in table` ✅
  - `after body`, `after after body` ✅
- **Foster parenting（table 內錯誤節點）** ✅
  - 非 table 內容插入 table 前的行為 ✅
- **Active formatting elements 完整重建** ✅
  - Noah's Ark clause ✅
  - Scope 概念與正確重建 ✅
  - FMT_MARKER 隔離（cell / caption 邊界）✅

---

## P2（中期補完）

- **Quirks / Limited-quirks 模式**
  - 完整 doctype 判定規則 ✅
  - 影響 tree construction 的模式切換 ⬜（偵測已完成，執行未實現 — 見 P3）
- **Fragment parsing** ✅
  - context element 的 tokenizer 初始化 ✅
  - fragment tree 的特殊插入規則 ✅

---

## P3（進階/完整相容）— 現有待實現項目

以下分為 **Critical（影響輸出正確性）**、**Medium（影響兼容度）**、**Low（邊緣情況）** 三層。

### Critical

- **Node 節點屬性（attrs）支持** ✅
  - 目前 `token` struct 已解析出 `attrs[]`（name + value），但 `node` struct 僅有 `name` / `data`。
  - 任何屬性（`class`, `id`, `href`, `src` 等）在樹構建時全部丟失。
  - 需要做的事：
    1. 在 `node` struct 中新增 `token_attr *attrs; size_t attr_count;`
    2. 在 `tree_builder` 裡將 `token.attrs[]` 複製到新建的 `node`
    3. 在 `tree_dump_ascii()` 裡列印屬性
  - 這是單一最大的功能缺口。

- **HTML serialization（樹 → HTML 字串）** ✅
  - `tree_serialize_html(node *root)` 將樹序列化回 HTML 字串
  - 已實現完整的規範化序列化：
    - 空元素（void elements）不輸出 end tag ✓
    - Raw text 元素（`<script>`, `<style>`）內容不做 HTML entity 轉換 ✓
    - RCDATA 元素（`<textarea>`, `<title>`）內容做 entity 轉換 ✓
    - 一般文本做 `&amp;` / `&lt;` / `&gt;` 轉換 ✓
    - 屬性值做 `&quot;` / `&amp;` 轉換 ✓
  - 測試程式：`serialize_demo` — 讀取 HTML 文件並輸出序列化結果
  - Makefile 目標：`make test-serialize` 驗證序列化功能

### Medium

- **Adoption Agency Algorithm（AAA）補完** ✅
  - 完整實現 WHATWG §13.2.6.4，包含 furthest block 路徑（outer loop + inner loop + replacement element 建立）。
  - 新增 `is_special_element()` 完整列表、`clone_element_shallow()`、stack/formatting list 索引操作。
  - `tree.c` 新增 `node_remove_child()` 和 `node_reparent_children()` 用於 AAA 子節點搬移。
  - 三處 builder 函數的 end tag 格式化處理統一由 `adoption_agency()` 處理。

- **Scoping elements 列表補完** ✅
  - `is_scoping_element()` 現在僅保留 WHATWG general scope 障壁（applet, caption, html, table, td, th, marquee, object, template）。
  - 新增 4 種 scope 類型：general scope、list item scope（+ol, ul）、button scope（+button）、table scope（html, table, template）。
  - 新增 `has_element_in_list_item_scope()`、`has_element_in_button_scope()`、`has_element_in_table_scope()`。
  - `<p>` 自動關閉使用 button scope；`<li>` 自動關閉使用 list item scope；table end tag 使用 table scope。

- **Formatting elements 列表擴充** ✅
  - 已支援完整 WHATWG §13.2.6.1 定義的全部 14 個 formatting elements：`a`, `b`, `big`, `code`, `em`, `font`, `i`, `nobr`, `s`, `small`, `strike`, `strong`, `tt`, `u`。
  - `fmt_tag` enum、`fmt_tag_from_name()`、`fmt_tag_name()` 已涵蓋所有元素。
  - 所有 formatting elements 均進入 active formatting list，AAA 對它們均正確觸發。

- **Formal "generate implied end tags" 算法** ✅
  - WHATWG §13.2.6.3 定義了 "generate implied end tags" 和 "generate implied end tags, except for X" 兩種變體。
  - 已實現 `is_implied_end_tag_element()`、`generate_implied_end_tags()`、`generate_implied_end_tags_except()` 三個集中函數。
  - 覆蓋元素：`dd`, `dt`, `li`, `optgroup`, `option`, `p`, `rb`, `rp`, `rt`, `rtc`。
  - `</p>`、`</li>`、`</dd>`/`</dt>` 均有專用 end tag 處理（三個 builder 同步）。
  - `</body>` 在 pop 前先呼叫 `generate_implied_end_tags()`。
  - `<option>` / `<optgroup>` start tag auto-close 已統一至三個 builder。

- **Quirks 模式在樹構建中的實際套用** ✅
  - `doc_mode`（`NO_QUIRKS` / `LIMITED_QUIRKS` / `QUIRKS`）已由 DOCTYPE 正確推算。
  - WHATWG 規範中，quirks mode 對 parser 層**僅有一處**影響：
    - `<table>` 開始標籤在 "in body" 模式下，non-quirks 會先自動關閉 `<p>`（button scope），quirks 模式則**不關閉**。
  - 已在 `handle_in_body_start()` 與 `handle_in_body_start_fragment()` 中加入 `dmode != DOC_QUIRKS` 條件。
  - Limited-quirks 僅影響 CSS 層（行內元素盒子模型），parser 層行為與 no-quirks 相同。
  - 注意：先前描述的 `document.write` 和 `<object>` 差異經查證在 parser 層實際上不存在。

### Low

- **NULL 字元替換（U+0000 → U+FFFD）** ✅
  - WHATWG 要求輸入中的 U+0000 替換為 U+FFFD（replacement character）並拋出 parse error。
  - 新增 `tokenizer_replace_nulls(raw, raw_len)` 公開函式（tokenizer.h/c），在 tokenizer 初始化前預處理原始位元組。
  - 每個 NULL 位元組替換為 U+FFFD 的 UTF-8 編碼（0xEF 0xBF 0xBD），並以正確行/列位置報告 parse error。
  - 兩個 demo 的 `read_file()` 已更新為使用此預處理。

- **Attribute context 中的 character reference 解碼** ✅
  - WHATWG 要求在屬性值裡，legacy 實體無分號時後接 `=` 或 alnum 不解碼（比正文更嚴格）。
  - `named_entity` 新增 `legacy` 欄位區分 legacy / non-legacy 實體。
  - `match_named_entity()` 依 `in_attribute` 參數實現不同的匹配規則。
  - `decode_character_references()` 新增 `in_attribute` 參數；數字參考無分號一律解碼。
  - `entities.tsv` 載入時自動去重標記 legacy（同名實體出現兩次 → legacy=1）。

- **Comment 狀態機邊緣情況** ✅
  - `<!-->` 和 `<!--->` 等特殊 comment 開始序列的處理（WHATWG 有獨立的 comment-less-than-sign-bang 等狀態）。
  - 完整實作 WHATWG §13.2.5 定義的 10 種 comment 相關狀態。

- **Encoding sniffing** ⬜
  - 目前假設輸入為 UTF-8。
  - 完整相容需要：BOM 檢測（UTF-8 / UTF-16 LE / BE）、`<meta charset>` 或 `<meta http-equiv="Content-Type">` 提前掃描、HTTP header 的 Content-Type 傳入。
  - 對於 CLI 工具而言優先度很低，但若要成為嵌入式 parser 則必須處理。

- **Marker 站點補充（applet / marquee / object / template）** ⬜
  - 目前 FMT_MARKER 僅在 `td`, `th`, `caption` 開啟時推入。
  - WHATWG 規範要求 `applet`, `marquee`, `object` 開啟時也應推入 marker；`template` 推入時也需要隔離 active list。
  - 這些元素在實際 HTML 中使用罕見，但影響到這些元素內部的格式化巢套。

- **Foreign content（SVG / MathML）** ⬜
  - 命名空間處理（namespace 切換進入 / 離開）
  - Integration points（SVG 中的 foreignObject / title / desc）
  - 外國內容中的特殊自閉合行為

- **完整 parser error recovery** ⬜
  - 所有 parse errors 對應的標準化容錯行為（WHATWG §13.2 各模式裡標記為 parse error 的分支）
  - 目前大部分 error case 已隱式處理（如忽略不合法的開始標記），但沒有系統性驗證。

---

## 開發建議（參考順序）

1. **Node attrs 支持** → 這是最影響「這個 parser 有沒有用」的功能，做完後才能做 serialization。
2. **AAA 補完（furthest block 路徑）** → 影響格式化巢套正確度。
3. **Scoping elements + formatting elements 擴充** → 低成本，正確度提升明顯。
4. **generate implied end tags 集中化** → 代碼整理，順便修潜在 bug。
5. **HTML serialization** → 做完 attrs 後可獨立實現。
6. 其餘 Low 項目按需求決定。
