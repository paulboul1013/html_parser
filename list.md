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

- **Scoping elements 列表補完** ⬜
  - 目前 `is_scoping_element()` 僅列出 5 個：`html`, `table`, `td`, `th`, `caption`。
  - WHATWG 規範中 "scope" 包含更多：`button`, `select`, `li`（list-item scope）、`fieldset`, `details`, `summary`, `object`, `applet`, `marquee`, `template`, `dd`, `dt`。
  - 這些遺漏會導致 `has_element_in_scope()` 在某些巢套結構中傳回錯誤結果。

- **Formatting elements 列表擴充** ⬜
  - 目前僅追蹤 4 個：`b`, `i`, `em`, `strong`。
  - WHATWG §13.2.6.1 定義的 formatting elements 還包括：`a`, `b`, `big`, `code`, `em`, `font`, `i`, `nobr`, `s`, `small`, `strike`, `strong`, `tt`, `u`。
  - 遺漏的元素不會進入 active formatting list，AAA 也不會對它們觸發，巢套容錯將不正確。

- **Formal "generate implied end tags" 算法** ⬜
  - WHATWG §13.2.6.3 定義了 "generate implied end tags" 和 "generate implied end tags, except for X" 兩種變體。
  - 目前°程序中，隱含關閉通過零星的 `if (strcmp(name, "p") == 0)` 等判斷散落實現。
  - 應提取為一個集中函數，覆蓋：`dd`, `dt`, `li`, `optgroup`, `option`, `p`, `rb`, `rp`, `rt`, `rtc`。

- **Quirks 模式在樹構建中的實際套用** ⬜
  - `doc_mode`（`NO_QUIRKS` / `LIMITED_QUIRKS` / `QUIRKS`）已由 DOCTYPE 正確推算。
  - 但目前樹構建中沒有任何地方讀取 `doc_mode` 來改變行為。
  - 主要影響：
    - Quirks：`<table>` 中裸露的字元參考可能套用別的規則；table 的 border/width 屬性影響計算。
    - Limited-quirks：僅影響行內元素的盒子模型（CSS 層，非 parser 層）。
    - Parser 層中最重要的是：quirks mode 下 `document.write` 和 `<object>` 的處理差異。

### Low

- **NULL 字元替換（U+0000 → U+FFFD）** ⬜
  - WHATWG 要求詞法分析器將輸入中的 U+0000 替換為 U+FFFD（replacement character）並拋出 parse error。
  - 目前詞法分析器直接傳遞 NULL byte，可能導致 C 字串截斷。

- **Attribute context 中的 character reference 解碼** ⬜
  - WHATWG 要求在屬性值裡，字元參考只在遇到 `;` 時才解碼（比正文更嚴格）。
  - 目前詞法分析器在屬性值中的容錯行為可能與規範不完全一致。

- **Comment 狀態機邊緣情況** ⬜
  - `<!--->` 和 `<!----->` 等特殊 comment 開始序列的處理（WHATWG 有獨立的 comment-less-than-sign-bang 等狀態）。
  - 目前僅支援標準 `<!-- ... -->` 路徑。

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
