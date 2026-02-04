# html_parser 專題知識筆記（給人看的）

目標：讓你快速「看懂專案架構 + 知道缺什麼 + 能怎麼擴充」，不用一開始就啃完整 HTML Standard。

## 1. 你現在擁有什麼（專案現況）

- [x] Tokenizer：把 HTML 字串切成 token（doctype/tag/comment/text/EOF）
- [x] Tree builder：用最小規則把 token 建成 DOM-like tree
- [x] ASCII tree：把 tree 用文字畫出來（方便人工檢查）
- [x] Tests：`make test` 會跑 tokenizer + tree 測試

## 2. 專案架構（你應該怎麼讀）

由外到內：

1. `tests/parse_file_demo.c`：讀檔 -> `build_tree_from_input()` -> `tree_dump_ascii()`
2. `src/tree_builder.c`：tree construction（最容易看出行為與限制）
3. `src/tokenizer.c`：tokenizer（輸出 token，tree builder 立即消費）
4. `src/tree.c`：node 結構 + ASCII dump
5. `src/token.c`：token 釋放與生命週期

## 3. 兩階段模型（核心心智模型）

- Stage A：Tokenizer
  - 輸入：字元串
  - 輸出：token stream（包含 `TOKEN_CHARACTER`）
- Stage B：Tree construction
  - 輸入：token stream
  - 輸出：DOM-like tree

你在除錯時要先問：問題是「切 token」錯，還是「消費 token 建樹」錯。

## 4. 資料結構（最重要）

### Token（`src/token.h`）

- `TOKEN_START_TAG`：`name`, `self_closing`, `attrs[]`
- `TOKEN_END_TAG`：`name`
- `TOKEN_DOCTYPE`：`name`, `force_quirks`
- `TOKEN_COMMENT`：`data`
- `TOKEN_CHARACTER`：`data`
- `TOKEN_EOF`

### Node（`src/tree.h`）

- `NODE_DOCUMENT`
- `NODE_DOCTYPE`
- `NODE_ELEMENT`（`name`）
- `NODE_TEXT`（`data`）
- `NODE_COMMENT`（`data`）

結構是單向 sibling + first/last child 的樹，方便 append。

## 5. insertion modes（為什麼需要）

HTML 的難點不是「把 `<tag>` 變成節點」，而是「不同 context 下 token 要插哪裡、要不要隱式補齊 head/body、table 要怎麼處理」。

完整規格用 insertion modes 來描述「目前正在解析哪一段語意環境」。

本專案目前只做「最小集合」：

- `before html`：沒有 html 就先補 html
- `in head`：head 專屬元素，遇到非 head 元素要先關 head
- `in body`：一般內容；避免巢狀 `<html>` / `<body>`
- `in table`：最小 table 規則（table/tbody/tr/td/th）

## 6. 本專案的實作特性（你要記得的差異點）

- Text node 策略：
  - Tokenizer 會吐出包含換行/縮排的 `TOKEN_CHARACTER`
  - Tree builder 會「過濾純空白」：只保留含非空白字元的文字
- 容錯策略：
  - 以「產出可用結果」為主
  - 不是瀏覽器級相容
- 自動補 tag（最小集合）：
  - `in body` 遇到 block-like start tag 會先隱式關閉尚未關閉的 `<p>`（避免 `<p><div>...` 這類巢狀）
  - `in body` 遇到新的 `<p>` 會先關掉上一個 `<p>`
  - `in body` 遇到新的 `<li>` 會先關掉上一個 `<li>`
  - `in body` 遇到新的 `<dt>/<dd>` 會先關掉上一個 `<dt>/<dd>`
- `in body` 遇到新的 `thead/tbody/tfoot/tr/td/th` 會先關掉上一個同類型元素
- Parse error 觀察方式（Tokenizer）：
  - 設定 `HTMLPARSER_PARSE_ERRORS=1` 會在 stderr 顯示錯誤位置與訊息
  - 目前會報：doctype 名稱缺失、非法 end tag、start tag 內 attr name 缺失等
- Character references（擴充）：
  - Named entities 讀自 `entities.tsv`（已填入完整 WHATWG 列表）
  - 無分號容錯只在下一字元不是英數、也不是 `=` 時解碼

## 7. 怎麼驗證目前行為（你的檢查點）

### 跑測試

```bash
make -C html_parser test
```

### 看 tokenizer 的 checkpoint（token 流）

```bash
cd html_parser
HTMLPARSER_CHECKPOINT=1 ./tokenizer_tests
```

### 讀 `.html` 檔看 DOM（ASCII tree）

```bash
make -C html_parser parse_file_demo
./html_parser/parse_file_demo html_parser/sample.html
```

也可以用自動補 tag 測試檔：

```bash
./html_parser/parse_file_demo html_parser/autoclose.html
```

也可用 parse error 範例檔：

```bash
HTMLPARSER_PARSE_ERRORS=1 ./html_parser/parse_html html_parser/parse_errors.html
```

## 8. 擴充路線圖（你要學/要做什麼）

### Tokenizer 方向

- Character references（`&amp;`/`&#...;`）解碼
- RCDATA/RAWTEXT/script data 等狀態（`title/textarea/script/style`）
- 更完整的 parse error 行為（目前偏簡化）

### Tree construction 方向

- 完整 insertion modes（`in table` 其實非常大）
- active formatting elements（`b/i/em/strong` 等重建規則，本版為簡化版）
- template insertion mode stack
- foreign content（SVG/MathML）命名空間與整合點

## 9. C 實作注意事項（踩雷清單）

- C11 不保證有 `strdup`，建議用專案內的 `dup_string()`（或自己實作）
- 每次取到 token 後要 `token_free()`，避免 leaks
- tree 釋放要用 `node_free()`（會遞迴釋放）
