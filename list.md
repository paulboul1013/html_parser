# html_parser 待補完整功能清單（最小拆解版）

以下依「優先順序」排列，優先解決最影響正確性的缺口。每一項都是**可獨立實作**的小任務。

## P0（最先做）

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

## P1（緊接著做）

- **Tree construction：完整 insertion modes（核心缺口）**
  - `in table body`, `in row`, `in cell`, `in caption` ✅
  - `in select`, `in select in table` ✅
  - `after body`, `after after body` ✅
- **Foster parenting（table 內錯誤節點）**
  - 非 table 內容插入 table 前的行為 ✅
- **Active formatting elements 完整重建**
  - Noah’s Ark clause ✅
  - Scope 概念與正確重建 ✅

## P2（中期補完）

- **Quirks / Limited-quirks 模式**
  - 完整 doctype 判定規則
  - 影響 tree construction 的模式切換
- **Fragment parsing**
  - context element 的 tokenizer 初始化
  - fragment tree 的特殊插入規則

## P3（進階/完整相容）

- **Foreign content（SVG/MathML）**
  - 命名空間處理
  - Integration points
- **完整 parser error recovery**
  - 所有 parse errors 對應的標準化容錯行為
