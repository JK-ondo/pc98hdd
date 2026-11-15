# 実装計画: RANK.EXE 毎日更新（nx93-daily-reset）

2026-10-22、architect。契約は `spec.md`（唯一の正）、試験設計は `test-design.md`。
本計画は spec を覆さず、実装の順序・構造・配置だけを決める。

Claude が計画へ加えた修正 2 件（下記 §3 の故障注入の分割、§8 未決の確定）を反映済み。

## 0. 守る不変条件（タスク共通）

spec §4 の I1〜I12 全部。加えて実装上の制約:

- **読みを全部終え、全ハンドルを閉じてから最初の書き込み**（spec §6）。移行の取り込みも
  「読み」側。
- コミット順 **RANK30 → RANKWHO → SCR → TXT** はコードに直書きし、データ駆動で
  並べ替え可能にしない。
- **旧台帳の取り込みで行数を数えない。** ループ制御は文法トークンのみ。存在する唯一の
  カウンタは表の `count`（≤101 で `int` 安全）。16 ビット `int` の溢れはホスト試験では
  原理的に捕まえられないので、構造で消す。数える必要が生じたら `long` 必須＋綴りを
  1 箇所に寄せる静的検査を足す。
- **移行は通常経路の 1〜3 段を共有する。** 4 段目だけが退避名 `NX93OLD.TXT`・成功時
  非削除・`RETIRE_RENAME`/`TXT_*` で分岐。共有を崩すなら test-design が刈った
  「移行 × 故障注入」8 組を復活させる義務が生じる。
- 既存 S1〜S9 静的検査は全フェーズで緑のまま。終了コードの語彙は {0,1}。ランキング行は
  printf 系書式引数に渡らない。`RANK_DOS` 一箇所。ASCII・C89。
- `tests/run.sh`・`mise.toml` は**全フェーズ緑になるまで触らない**。新試験は
  `run-daily.sh` 系に隔離。

## 1. RANK.C の構造

**1 ファイルのまま**（98 上で QCL に通すため分割しない）。現行 1302 行のセクションを
次のように組み替える。

| 現行 | 範囲 | 処置 |
|---|---|---|
| 冒頭コメント | 1–40 | plan 参照を nx93-daily-reset に差し替え。制約列挙は不変 |
| 定数 | 50–69 | `RANK_PERIOD_DAYS`・`RANK_WS_MAX` を**削除**。`RANK30_ROWS` `RANKWHO_ROWS` `RANK_MODE_WEEKLY/DAILY` を追加 |
| `RANK_DOS`／型 | 71–108 | 不変。型の直後に `Rank30Table` `RankWhoTable` を追加 |
| テスト継ぎ手 | 110–135 | enum を 17 点＋NONE に拡張（`LEDGER_*`→`TXT_*` 改名、`R30_*` `WHO_*` `*_STRAND` `RETIRE_RENAME` 追加）。`rank_fault` マクロは不変 |
| `--- text` | 137–178 | 不変（`rank_put_days` は無処理メッセージ用に生存） |
| `--- time` | 180–397 | 末尾に **`rank_week_index`・`rank_time_before`・`rank_crossed`** を追加 |
| `--- rows` | 399–651 | `rank_fold_key`（404–427）を spec §3.8 規則へ改訂。`rank_default_line` の直後に **`rank_is_default_line`** 追加。`rank_read_table`/`rank_write_table` は **`rank_rows_read`/`rank_rows_write` の上の薄いラッパへ**作り直す（行読取オートマトンを 1 本に保つ）。`rank_rebuild` に既定行除外を追加 |
| `--- tables`（**新設**） | — | `rank_sort_rows`（安定挿入ソート、降順）・`rank_r30_load`・`rank_who_load`・`rank_merge30`・`rank_mergewho` |
| `--- ledger` → `--- state / legacy ledger` | 653–913 | **削除**: `rank_copy_rest`・`rank_json_escape`・`rank_write_entry`・`rank_ledger_write`。**残す**: `rank_is_space`/`rank_skip_ws`/`rank_stream_find`・`rank_ledger_read_last_update`・`rank_ledger_seek_backups`（import が再利用）。**新設**: `rank_state_write`・`rank_state_check`・`rank_has_backups`・`rank_ledger_string`・`rank_import_ledger` |
| `--- paths` | 915–1046 | 不変 |
| commit | 1048–1091 | `rank_commit` を一般化（§3） |
| `--- run` | 1093–1284 | `rank_run` を分解（§2）。`rank_mode_parse` はこのセクション先頭 |
| `main` | 1286–1302 | argc/argv 判定・usage（stdout、`/D` `/W` を字面で含む） |

**行数見積もり**: 削除 ≈170 行、追加 ≈1,020 行。**完成形 ≈ 2,150 行（1.6 倍）**。

## 2. `rank_run` の分解

現行 190 行 → 司令塔 ≈60 行＋ヘルパ 5 本。**読み相（a〜c）→ 書き相（d）の 2 相**で、
spec §6 の「読み終えてから書く」と 4 段順序を構造で保証する。

```
int rank_run(const char *dir, int mode)
  ├ a. rank_paths_init(dir)        13 本の静的パス緩衝を充填。E1(mode)/E2
  ├ b. 判定相（読みのみ）
  │    rank_classify_txt(...)      NX93.TXT を {ABSENT, CANONICAL, LEGACY, OTHER} に分類し
  │                                lastUpdate を取り出す
  │    E3/E4 → rank_time_before で E5 →
  │    rank_crossed で非跨ぎなら無処理 0（mode 字句＋経過日数）
  │    OTHER＋跨ぎ → E10。LEGACY＋跨ぎ → NX93OLD.TXT 存在検査（E11）
  ├ c. 収集相（読みのみ、メモリ内併合）
  │    SCR 読み（E6）→ r30_load（E7）→ who_load（E8）→ writable×4（E9）
  │    → SCR 候補（is_default_line 除外）を merge30/mergewho（W1 をその場で報告）
  │    → LEGACY なら rank_import_ledger（E12。失敗＝ディスク無変更のまま 1）
  │    → rank_rebuild(cur, next)。ここで全 FILE* が閉じている
  └ d. 書き相 rank_write_all(migrate)
       rank_stage(...) を R30 → WHO → SCR → TXT の固定順で 4 回。
       前段失敗で即 return 1。失敗メッセージは「替わった／替わっていない」を
       段の添字から機械的に組む（E13〜E16 の残留表どおり）
```

`rank_import_ledger(fp, t30, who)` は spec §3.4 文法のストリームパーサ。1 文字列ずつ
`rank_ledger_string`（64 B 緩衝）→ `rank_parse_line`（静的 `RankRow` 作業域）→
`merge30`/`mergewho`。**行カウンタを持たない**。

モードは b の `rank_crossed` にだけ入る。確定後の経路にモードは一切現れない
（test-design がモード次元を刈った前提）。

## 3. 3 段コミットの一般化と故障注入 17 点

```c
static int rank_commit(const char *tmp, const char *dst, const char *keep,
                       int keep_retained,  /* 1 = 成功時に keep を消さない（移行の TXT 段のみ） */
                       int fail_retire,    /* 退避 rename(dst→keep) を失敗させる */
                       int fail_rename,    /* 差し替え rename を失敗させる。復帰は本物を走らせる */
                       int fail_strand);   /* 差し替えと復帰の両方を失敗させる */
```

- `keep_retained` が 0 のとき現行と同一。1 のとき末尾の `remove(keep)` を飛ばす
  （`NX93OLD.TXT` 温存）。冒頭の「残骸 remove」は E11 が先に拒むため移行時は到達前提が
  立つ。その論証をコメントに書く。
- **冒頭の「残骸 remove」の前に W2 を出す**（ユーザー決定 2026-10-21、spec §5）。
  E15 の後に残った唯一の回復コピーを、2 回目の跨ぎで黙って消すのを防ぐ。
  退避名が実在するときだけ出す（毎回出すと無意味な行が毎日出る）。**ASCII で書く。**
- `fail_retire` は最初の `rename(dst, keep)` を失敗させる ＝ **E16**（dst 無傷・FAILED）。
  非移行段では常に 0。
- **`fail_rename` と `fail_strand` を分けるのは必須**（Claude による計画の修正）。
  まとめると仕様 §5 の **E14「差し替え rename 失敗・復帰成功」が観測不能**になる。
  復帰が成功する経路は 3 段コミットが存在する理由そのもので、網を外せない。
  `*_COMMIT` は `rank_commit` に入る前に戻るので代替にならない。
  - `fail_rename` → 差し替えのみ失敗、復帰は本物（POSIX では成功）→ **E14 FAILED**
  - `fail_strand` → 両方失敗 → **E15 STRANDED**（注入しない限り POSIX では到達不能）

**注入点の渡し方**: 故障フラグは呼び出し側で `rank_fault(RANK_FAULT_R30_WRITE)` のように
**マクロ評価した int** をヘルパ引数で渡す。DOS ビルドでは `#define rank_fault(which) 0`
が識別子ごとトークンを捨てるので、enum 名が DOS ビルドに現れず **S5 が構造的に保たれる**。
`RankFault` を構造体フィールドや変数に格納してはならない（格納すると DOS ビルドに
enum が必要になる）。

段ヘルパは 1 本:

```c
static int rank_stage(int which,   /* 0=R30 1=WHO 2=SCR 3=TXT */
                      int migrate, /* which==3 のときだけ keep と retention を切り替える */
                      int f_write, int f_commit,
                      int f_retire, int f_rename, int f_strand);
```

内部: tmp を `wb` で作成 → switch で `rank_rows_write(t30)` / `rank_rows_write(who)` /
`rank_write_table(next)` / `rank_state_write(now)` → close → `f_commit` → `rank_commit`。
失敗の全経路で tmp を remove（I9）。関数ポインタ表は採らない（§6 の棄却案 2）。

故障点 = {R30,WHO,SCR,TXT}×{WRITE,COMMIT,RENAME,STRAND} 16 点 ＋ `RETIRE_RENAME` ＝ **17**、
＋ `NONE`。

## 4. 静的領域の配置

| オブジェクト | 置き場所 | 大きさ |
|---|---|---|
| `static RankTable cur, next` | rank_run（既存のまま） | 3,444 B |
| `static RankTable work` | rank_rebuild（既存のまま） | 1,722 B |
| `static Rank30Table t30`・`static RankWhoTable who` | run セクションのファイルスコープ static（収集相と書き相が共有） | 2,668 + 8,688 B |
| パス緩衝 13 本 | rank_run 内 static | 1,664 B |
| import の `RankRow` 作業域 1 本・デコード緩衝 64 B | rank_import_ledger 内 static | ≈150 B |
| メッセージ・既定行リテラル | .rodata/.data | ≈3 KB |

自動変数に許すのは既存 `rank_rebuild` の `RankRow tmp`（86 B）と小さなスカラのみ。
削除される `rank_copy_rest` の static 512 B は消える。合計は spec §6 の ≈21.4 KB に整合。

**build.sh の表明**: docker ビルド成功後に `.data + .bss ≤ 32768` を検査し、超えたら
非 0 で落ちる。計測はリンカへ `-Wl,-Map=rank.map` を足して map の `.data`/`.bss` を
集計する。docker 依存なので `verify` には繋がない。

**2026-11-15 訂正: `ia16-elf-size` の代案は素直には使えない。** `-mcmodel=small` の出力は
MZ（`i386msdos` 出力形式）で ELF ではないので、中間 ELF を残すには別途リンカ出力形式の
指定か一時ファイルの保存が要る。map 方式（第一候補）を採ったので実害はない。

計測は**測れなかったときに落ちる**こと。map 不在・該当行が 1 本に定まらない・合計 0 の
いずれでも非 0。「測れないので OK」にすると、上限が守られていると誤解させるぶん検査が
無いより悪くなる。map の出力セクション行は**行頭に固定して読む**（字下げした入力セクション行と
`Discarded input sections` ブロックを数えると十数 KB 多く見積もって偽の赤が出る）。

## 5. 順序付きタスク一覧

> RANK.C を触る turn は `verify:nx93rank-binary`（sha256 照合）が必ず赤くなる。
> **ユーザー決定（2026-10-22）: 各フェーズ後に `mise run build:nx93rank` を 1 回ずつ
> 打つ。** 実装者は T7/T8/T9 の末尾で止まり、ユーザーへ引き渡す。

### T0. 試験設計の確定確認 — **完了（2026-10-22）**
`U-G10` の期待値 10 行が `test-design.md` に埋まり、Claude が独立検算して一致を確認した
（338 バイト、辞書順実装なら 1 行目から 10 行すべて食い違う）。未決は残っていない。

### T1. fixtures_daily.h の生成
- **触る**: 新設 `nx93rank/tests/fixtures_daily.h`。手順を `nx93rank/tests/README.md` に追記。
- **原本**: `hdd/game/nx93/nx93.scr`（2026-10-13、実記録 6 行＋既定 4 行、CRLF）、
  `hdd/game/nx93/nx93.txt`（581 B・LF・ASCII のみ）。
- **内容**: `fix_scr13`（原本と cmp 一致）、`fix_ledger_lf`（581 B、cmp 一致）、
  `fix_ledger_crlf`（LF→CRLF、601 B、CR 全削除で lf 版へ戻ることを生成時に確認）。
  テスト実行時に `hdd/` を読まない（埋め込みのみ）。
- **受け入れ**: cmp 3 点一致の記録。`-Wall -Wextra -Werror` を汚さない。
- **依存**: なし。

### T2. run-daily.sh の新設
- **触る**: 新設 `nx93rank/tests/run-daily.sh`。`run.sh`・`mise.toml` には**繋がない**。
- **内容**: `run.sh` の骨格を踏襲。(1) `RANK_C` 環境変数で対象ソース差し替え（既定
  `../dist/RANK.C`）。(2) 静的検査 **S10**（8.3 名。新 7 名の出現も確認）・**S11**
  （旧 4 記号の出現 0 件）・**S12**（表型の関数内宣言はすべて static）・**S6′**
  （`isalnum/isalpha/isdigit/isupper/islower` の不在）。(3) **H2/H3**（ビルド失敗を
  名前付き 1 件の赤に）・**H4**（`geteuid() != 0`、root なら明示的に落ちる）。
  (4) TZ 3 本で両バイナリを起動。
- **受け入れ**: `bash -n` が通る。実行すると **H2 の名前付き赤 1 件**で exit 1。
- **依存**: なし。

### T3. test_daily.c の実装（実装前なので赤で正しい）
- **触る**: 新設 `nx93rank/tests/test_daily.c`。
- **内容**: test-design の U-A・U-C・U-K・U-F・U-G・U-V・I-21〜I-51・X-2/X-3。
  ハーネス拡張: `Env` を 13 パスに、stdout 採取（fflush→dup2→実行→復元→読み出し）、
  `expect_no_temps` の 8 名拡張＋全統合ケース末尾での横断呼び出し（I-48）、
  5 枚スナップショット、固定シード 4 本（各ケース冒頭で `srand`）、`rank_test_fault` を
  ケース冒頭と末尾で NONE に戻す。**U-K3 は必須 2 形（`omzn\x82\xa0`・`omzn\x82\xa0@`）を
  必ず含む**。**故障注入は 17 点**（`*_STRAND` を含む。§3 の分割に合わせる）。
  **故障注入の基線は「一度クリーンにリセットさせて 4 枚を実在させ、次の跨ぎで注入」に
  固定する。** 対象ファイルが不在だと `rank_commit` の `saved` が 0 のままで `!saved`
  経路が先に FAILED を返すため、`*_STRAND` は E15 に到達できず、`R30_STRAND` と
  `WHO_STRAND` の 2 点が黙って E14 を二度試すだけに縮退する。縮退自体を見るケースも置く。
  `*_COMMIT` と `*_RENAME` は残留バイトで区別できない（どちらも対象は旧内容のまま実在）
  ので、**画面出力の差分が E14 を E13 から分ける唯一の観測面**になる。
- **受け入れ**: H2 が「未宣言シンボル一覧 = spec §3.9 の新 API 一覧」で赤になる
  （コンパイラ出力を報告に貼る）。既存 `run.sh` は 61 ケース・緑のまま。
- **依存**: T1、T2。

### T4. test_cli.c の実装
- **触る**: 新設 `nx93rank/tests/test_cli.c`。
- **内容**: `#define main rank_entry` → `#include "../dist/RANK.C"`（`RANK_NO_MAIN` なし）。
  A-1〜A-7。`argv[0]` に `<dir>/RANK.EXE` を与えて `rank_dir_from_argv0` も本物を通す。
- **受け入れ**: H3 が名前付き赤。`run.sh` 緑のまま。
- **依存**: T1、T2。

### T5. 赤の帰属確認（リポジトリ無変更）
- **内容**: test-design §13.2 の 3 段。第 3 段は捨てスタブ 10 種を `RANK_C=<写し>` で
  当て、**ケースごとに「どのスタブで赤くなったか」を記録**。どのスタブでも赤にならない
  ケースは強めるか、設計へ差し戻す。スタブはスクラッチパッドのみ、リポジトリに入れない。
- **受け入れ**: 帰属表の報告。`run.sh` 61 緑・`run-daily.sh` 赤（理由付き）の実測ログ。
- **依存**: T3、T4。

### T6. build.sh に `.data + .bss ≤ 32768` の表明
- **触る**: `nx93rank/build.sh`（docker 実行後に検査を追加）。
- **受け入れ**: `bash -n` 緑。ユーザー実行で (a) 現行 RANK.C → 緑＋実測値の表示、
  (b) 静的領域を膨らませた捨て写し → 非 0。**2026-11-15 訂正: 当初書いた
  `static char blob[40000];` では検査の証拠にならない。** ファイルスコープの未使用
  `static` は `-Wall` の `-Wunused-variable` を出し、`CFLAGS` に `-Werror` があるので
  **サイズ検査より先にコンパイルが落ちる**。40000 B は見積もり 21.4 KB と合わせて約 62 KB で、
  64 KB セグメントに対してリンカが先に文句を言う余地もある。どちらも非 0 ではあるが
  サイズ検査が働いた証拠にならない。外部リンケージの `char rank_size_probe[20000];`
  （未使用警告が出ず、合計約 41 KB で上限超えだが 64 KB には余裕）を使う。
- **依存**: なし（T10 より前ならいつでも）。

### T7. 実装フェーズ①: 決定 7＋8（鍵規則と rebuild）＋期待値差し替え
- **触る**: `dist/RANK.C` — `rank_fold_key` を spec §3.8 の 4 段規則へ（`unsigned char`
  比較・0x80 で停止・空なら退避）。`rank_is_default_line` 新設。`rank_rebuild` に
  「`cur` のうち既定行バイト一致を重複除去前に除く」を追加。
  `tests/test_rank.c` — **同じ変更で** `UM1_WANT`・`UM2_WANT`・`case_up6` の表・
  `case_um3` を test-design の新期待値に差し替え。
- **受け入れ**: `run.sh` **61 ケース緑**（期待値更新込み）。U-K1〜K8・U-G1・U-G8・U-G9 が
  緑に転じる（他は赤のまま＝正しい）。U-M5〜M10 は無変更で緑（決定 7 で赤くなったら回帰）。
- **受け入れの観測方法（2026-11-15 訂正）**: 名指しの 3 群は **`run-daily.sh` では観測
  できない**。`H2`／`H3` が T8 の新 API 全部に依存するので、ハーネスがビルドできず 1 ケースも
  走らない。T5 と同じ作法で**捨ての包み**（T8 の API を失敗値を返すスタブで埋め、本物の
  `RANK.C` を `#include` する 1 ファイル）をスクラッチパッドに置いて観測する。
  リポジトリには入れない。
  採らなかった案: 受け入れを「`run.sh` 61 緑」だけに弱める（フェーズごとの証拠が消える）／
  `run-daily.sh` を「ビルドできた群だけ走らせる」形に直す（`H2` は偽緑を防ぐために置いた
  網なので、弱める向き）。
- **依存**: T5。末尾でユーザーが `mise run build:nx93rank`。

### T8. 実装フェーズ②: 新 API の追加（加算のみ、`rank_run` 不変）
- **触る**: `dist/RANK.C` — §1 の表のとおり新 API 全部（`rank_mode_parse`・
  `rank_week_index`・`rank_time_before`・`rank_crossed`・新型・新定数・
  `rank_rows_read`/`rows_write`＋既存 2 本のラッパ化・`rank_sort_rows`・`rank_r30_load`・
  `rank_who_load`・`rank_merge30`・`rank_mergewho`・`rank_state_write`・
  `rank_state_check`・`rank_has_backups`・`rank_ledger_string`・`rank_import_ledger`）。
  署名は spec §3.9 どおり。
- **受け入れ**: `run.sh` 緑（ラッパ化後も U-R1〜R7 が無変更で緑＝受理規約の保存）。
  U-A・U-C・U-F・U-G2〜G7・U-G10・U-V が緑。**S10 と S11 はまだ赤**（S11 は旧記号が残るため、
  S10 は新 7 ファイル名のリテラルが T9 の `rank_paths_init` に属するため。どちらも想定どおり）。
  I 系・A 系は赤のまま。
- **受け入れの観測方法（2026-11-15 訂正）**: T7 と同じく **`run-daily.sh` では観測できない**。
  `test_daily.c` は `rank_run(dir, mode)` を呼び、`test_rank.c`（T8 では凍結）は
  `rank_run(e.dir)` を呼ぶ。**`rank_run` の引数は 1 通りしか存在できず C89 に省略可引数は
  ないので、「`run.sh` 緑（`test_rank.c` 無変更）」と「`run-daily.sh` で U 系が緑」は
  T8 では同時に満たせない**（署名変更は spec §3.9 で T9 の項目）。捨ての包み
  （`rank_run` を改名して `RANK.C` を `#include` し、mode を捨てる 2 引数 shim を足す
  1 ファイル）で観測する。リポジトリには入れない。
  採らなかった案: `rank_run` の署名変更と `test_rank.c` の書き換えを T8 へ前倒しする
  （「T8 は加算のみ・`rank_run` 不変」という分割の前提が崩れる）／`H2` を「ビルドできた群
  だけ走らせる」形に緩める（T7 の節で既に棄却した向き）。
- **新 2 表は T8 では置けない（2026-11-15 実測）。** `Rank30Table`／`RankWhoTable` の
  ファイルスコープ static（§4 の 11,356 B）は、使い手が T9 の収集相なので T8 で置くと
  未使用のファイルスコープ static になり `-Wall` の `-Wunused-variable` ＋ `-Werror` で
  **`S2` が落ちる**（T6 の受け入れ (b) と同じ罠）。使い手と同じ変更で入る。§4 の配置の
  決定は変わらない。T8 時点の静的領域の実増は約 +450 B。
- **依存**: T7。末尾でユーザーがビルド。

### T9. 実装フェーズ③: `rank_run` 書き換え・移行・旧 writer 削除・CLI・文書
- **触る**:
  - `dist/RANK.C` — §2 の分解、§3 の `rank_commit` 一般化＋`rank_stage`、fault enum
    17 点化、`main` に `rank_mode_parse`/usage、11 条件の相異メッセージ（**E8 は
    「`RANKWHO.TXT` を消せば次回に作り直され、過去の記録は `RANK30.TXT` に残る」を
    必ず含める**）。旧 4 関数・旧 2 定数を削除。
  - `tests/test_rank.c` — 全 `rank_run(e.dir)` → `rank_run(e.dir, mode)`、I-1〜I-8/
    I-11〜I-20/X-1 を暦境界前提＋4 枚表明へ改訂、`case_ul3`〜`case_ul8`・`case_i9`・
    `case_i10` とレジストリ行を削除、不要化する `json_*` 覗き・`check_ledger`・
    `write_huge_ledger` を削除（`-Werror` の unused で強制される）、S8 契約表明に
    新定数・新型・旧記号不在を追加。
  - `dist/GAME.BAT` — `RANK` → **`RANK /D`**（ユーザー決定）、「更新頻度の変更＝この
    1 行の編集」のコメント。errorlevel 2 行と `NX93` は不変。
  - `nx93rank/README.md` — 引数・4 ファイル・移行手順・動作表・「記録を手で戻す」節を
    新形式へ全面改訂。
  - `tests/README.md` — ケース数（61→53）・削除ケース・新 3 ファイル・`run-daily.sh` の
    使い方・**mise スニペットの書き置き**（ユーザーが繋ぐ旨を明記）。
- **受け入れ**: `run.sh` **53 ケース緑**×3TZ。`run-daily.sh` **90 ケース全緑**×3TZ、
  ASan/UBSan 無反応、S10〜S12・S6′ 緑（S11 がここで初めて緑になる）。
  `clang -std=c89 -pedantic-errors` 緑。
- **依存**: T8。末尾でユーザーがビルド。

### T10.（ユーザー）`mise run build:nx93rank`
各フェーズ後に 1 回ずつ。最終回の受け入れ: build.sh 緑（T6 のサイズ表明込み、実測値
≤ 32768 の表示）。`mise run verify` 全緑。

### T11. ミューテーション検証（リポジトリ無変更）
- **内容**: test-design の全ミューテーションを捨て写し＋`RANK_C` で 1 つずつ仕込み、
  **指定ケースだけが赤**になることを確認。特に「0x80 以上でも落とす」系（`U-K3` のみが
  殺す）と「殺し手のないケース」の洗い出し。
- **受け入れ**: 対応表の実測報告。殺されないミューテーションが残ったら試験を強める。
- **依存**: T9。

### T12.（ユーザー）run-daily.sh の接続
`tests/README.md` のスニペットで `mise.toml` に `verify:nx93rank-daily` を追加、
`verify:syntax` と `verify:encoding` の対象に `nx93rank/tests/run-daily.sh` を追加。
`run.sh` への畳み込みは任意・後日。受け入れ: `mise run verify` 緑。

### T13. レビュー層
`code-simplifier` → `/codex:adversarial-review` → `audit`。`/code-review` はユーザー起動。

**audit の人手項目**（機械オラクルを持たない分）: 対処文の実効性（`DATE`・`ATTRIB -R`・
退避名からの rename 手順・E8 の削除助言）、PAUSE で読める長さ、**import に行カウンタが
居ないことの目視**（16 ビット対策はこれが唯一の網）、`rank_fault` の引数が変数経由に
なっていないこと（S5 の構造保証）。

### T14. コミット提案 → 実機（ユーザー）
コミットは一貫変更ごとに分割して**提案**（試験群／build.sh／実装①②③＋文書）。

実機: `98hdd send nx93rank/dist /GAME/NX93` → spec §7 H（`GAME.BAT` の `/D` と
errorlevel、移行の実走と `NX93OLD.TXT`、`TYPE RANK30.TXT`、NX93.EXE が新 SCR を読む）
→ **`QCL RANK.C` を一度通す**（ユーザー決定。ファイルが 1.6 倍になるので保険経路が
生きているかを確かめる。失敗してもクロスビルドが正なので印には困らない）→
**実機 `NX93.TXT` の実バイト数の計測**（601 B 推定の裏取り）→ progress.md に記録。

## 6. ADR に残す判断（spec §8 の棄却案に追加する分）

1. **故障フラグは `rank_fault()` のマクロ評価値を int 引数で渡す。** 棄却: `RankFault` を
   段記述構造体に格納 — DOS ビルドに enum が必要になり S5 が破れる。棄却: 全 17 点を
   直書きの重複ブロックに — 複製で注入点とメッセージが発散する。
2. **段ヘルパは int ディスパッチの 1 本。関数ポインタ表は使わない。** C89/QuickC で動くが、
   16 ビット側のデバッグ追跡性と引き換えに得るものがない。
3. **`*_RENAME` と `*_STRAND` を別の注入点にする**（Claude の修正）。棄却: 1 点に
   まとめて両方失敗させる — 仕様の E14「差し替え失敗・復帰成功」が観測不能になり、
   3 段コミットが存在する理由そのものの経路が無試験になる。`*_COMMIT` は `rank_commit`
   に入る前に戻るので代替にならない。
4. **`rank_read_table`/`rank_write_table` は `rank_rows_read`/`rows_write` のラッパへ。**
   棄却: 読取オートマトンの並立 — CR/LF/0x1A/空行の規約が 2 箇所で漂流し、
   「同一規約」が表明でしか守れなくなる。ラッパなら構造で真。
5. **新 2 表はファイルスコープ static。** 棄却: 自動変数（DOS スタック ≈2 KB）、
   ヒープ（S4）。
6. **取り込みは行を数えない。** カウンタ不在が 16 ビット溢れを構造的に消す。
7. **移行は 1〜3 段を通常経路と共有。** 刈られた 8 組の復活は不要。
8. **実装を緑保存の 3 フェーズに分割**し、`test_rank.c` の期待値差し替えは挙動を変える
   フェーズと同一変更に結合。棄却: 一括 1 変更 — レビュー不能な巨大 diff。棄却: 期待値の
   先行差し替え — `verify` が実装完了まで赤になりゲートと衝突。
9. **新静的検査は接続まで run-daily.sh に隔離**（S6′ 含む）。run.sh への反映はユーザーの
   接続作業に含める。
10. **`rank_import_ledger` は旧台帳を 2 回読む**（1 回目は文法検査のみ、2 回目で併合。
    2026-11-15、T8 の実装中に確定）。棄却: 1 行ずつ併合しながら進む — `U-V7` の
    「−1 のとき両表が入力前とバイト同一」が原理的に満たせない（途中まで併合済みになる）。
    棄却: 併合先の私用コピーを持って成功時に本体へ複写 — `Rank30Table`＋`RankWhoTable` の
    11,356 B が増えて §4 の見積 21.4 KB が 32.8 KB になり**上限 32 KiB を超える**。
    2 回読みはストリームのままなので `S4`（ヒープなし）と有界性の制約を保つ。
    **オラクルと静的領域の予算を同時に満たす唯一の形**だったので、素描（§2）から外れた。

## 7. リスクと潰すタスク

| リスク | 潰し |
|---|---|
| 16 ビット `int` の行数溢れ（ホストでは何を書いても緑） | 設計で消す（カウンタ不在、§6-6）＋ T13 audit の目視。導入されたら `long`＋静的検査を足す |
| DOS の rename 意味論（対象が在ると失敗） | I-46 変種②（ディレクトリで名前を塞ぐ、T3）＋ T14 実機 |
| newlib のバイナリモード・CRLF | U-V3/I-37 の LF/CRLF メタモルフィック（T3）＋ T14 実機（実バイト数計測） |
| QuickC 2.00 が 2,150 行・新リテラル群を通すか | S2 は C89 適合までしか代理しない。T14 で `QCL RANK.C` を通す（ユーザー決定済み） |
| `.data+.bss` の増殖（メッセージ 11 条件分） | T6 の build.sh 表明（上限 32 KiB、見積もり 21.4 KB）＋文面を短く保つ |
| `rank_state_check` の緩み（旧台帳の誤破棄） | U-F6 の 11 変種＋I-34（T3）。実装は「45 バイト読み＋46 バイト目 EOF＋リテラル memcmp＋time_parse」 |
| stdout 採取ハーネスの不安定（dup2/fflush 順序） | T3 でヘルパ 1 本に集約、H2/H3 が名前付き赤で検出 |
| RANK.C 変更ターンの `verify:nx93rank-binary` 赤 | 各フェーズ末尾でユーザーがビルド（決定済み） |

## 7.5 T1/T2 で判明した実装上の制約（2026-10-22 実測）

- **`S11` は本文の文字列走査なので、`RANK.C` のコメントに削除済み記号の名前を書けない。**
  「旧 `rank_ledger_write` を置き換える」と書くと赤になる。設計の「出現が 0 件」に忠実な
  挙動なので検査は直さない。T9 で文章を書くときに避けること。同じことが
  `rank_json_escape` `RANK_PERIOD_DAYS` `RANK_WS_MAX` にも当たる。
- **`fixtures_daily.h` は厳格 C89 では組めない。** 台帳リテラルが 581 / 601 バイトで、
  C90 が保証する文字列リテラル長の下限 509 バイトを超え、`-pedantic-errors` が
  `-Woverlength-strings` で落ちる。ハーネスは `-std=gnu89` で組む。`RANK.C` 本体の
  C89 厳格さは `S8` が別に固定しているので守りは落ちていない。既存 `fixtures.h` は
  最大 340 バイトなのでこの境界に当たらなかった。509 バイト未満に分割すると
  `fix_ledger_lf` / `fix_ledger_crlf` の「1 シンボル」契約が壊れるので分割しない。
- **`S10` は文章中のファイル名を見ない。** リテラル全体が点付き大文字名のものだけを
  8.3 検査する。spec §5 はエラー文面にファイル名を名指すことを要求するので、それらは
  文中に埋まる。開かれない名前なので DOS の失敗経路には到達しないが、`S10` は
  メッセージ本文の網ではないと知っておくこと。
- **フィクスチャ原本の 2 ファイルは追跡済み**（コミット `1ddac71`）。sha256 は
  `nx93rank/tests/README.md` の手順 0-b にある。原本が替わると `U-G8`・`I-26`・`I-37` の
  オラクルがまとめて動く。

## 8. 未決

なし。architect が挙げた 3 件はすべて解決した。

1. N5（`U-G10` の期待値）— T0 で確認済み、検算も一致。
2. ビルドの打つ回数 — **各フェーズ後に 1 回ずつ**（ユーザー決定 2026-10-22）。
3. 実機での `QCL RANK.C` — **一度通す**（ユーザー決定 2026-10-22）。
