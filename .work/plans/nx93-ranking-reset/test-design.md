# テスト設計（RANK.C）

plan.md の「更新の規則」が原典。ここはそれを検証可能な形に落としたもの。
ケースは実装ではなく plan.md の意図から起こすこと。

## 実装が守る契約

### マクロ

| マクロ | 意味 |
| --- | --- |
| `RANK_NO_MAIN` | `main` を除外。テストが `#include "../dist/RANK.C"` するとき定義する |
| `RANK_TEST_CLOCK` | `rank_now` をテスト可変のグローバルから返す版に差し替える。**DOS ビルドでは未定義** |
| `RANK_TEST_FAULT` | 故障注入点を有効化。**DOS ビルドでは未定義** |

### 定数

```c
#define RANK_ROWS       10   /* ゲームが保持する行数 */
#define RANK_NAME_LEN    8
#define RANK_MAX_LINE   63   /* 行バッファ。受理するのは長さ 27..35 のみ */
#define RANK_PATH_MAX  128
```

### 型

```c
typedef struct {
    int year;    /* 西暦 4 桁 */
    int month;   /* 1-12 */
    int day;     /* 1-31 */
    int hour;    /* 0-23 */
    int minute;  /* 0-59 */
    int second;  /* 0-59 */
} RankTime;

typedef struct {
    char raw[RANK_MAX_LINE + 1];   /* 入力バイト列そのまま。CR/LF/0x1A は含まない */
    long score;                    /* long であること（98 の int は 16 bit） */
    char name[RANK_NAME_LEN + 1];  /* 名前欄 8 バイトを無加工で */
    char key[RANK_NAME_LEN + 1];   /* name の前後 0x20 を落とし ASCII a-z のみ大文字化 */
} RankRow;

typedef struct {
    RankRow row[RANK_ROWS * 2];
    int count;
} RankTable;
```

`score` が `long` であることはテストがコンパイル時に表明する:

```c
typedef char chk_score_is_long[(sizeof(((RankRow *)0)->score) == sizeof(long)) ? 1 : -1];
```

### 関数

```c
/* --- 行 --- */
int  rank_parse_line(const char *line, RankRow *out);      /* 0 / -1 */
int  rank_read_table(FILE *fp, RankTable *out);            /* 行数 / -1 */
int  rank_write_table(FILE *fp, const RankTable *t);       /* 0 / -1 */
const char *rank_default_line(int i);                      /* i in [0,RANK_ROWS) */
void rank_rebuild(const RankTable *cur, RankTable *out);   /* 重複除去＋併合＋上位10 */

/* --- 台帳 --- */
int  rank_ledger_read_last_update(FILE *fp, RankTime *out);            /* 0 / -1 */
int  rank_ledger_write(FILE *out, FILE *old, const RankTime *now,
                       const RankTable *backup);                       /* 0 / -1 */
int  rank_json_escape(FILE *fp, const char *s, size_t len);            /* 0 / -1 */

/* --- 時刻 --- */
int  rank_now(RankTime *out);                                          /* 0 / -1 */
int  rank_time_parse(const char *s, RankTime *out);                    /* 0 / -1 */
void rank_time_format(const RankTime *t, char *buf20);
long rank_days_from_civil(int y, int m, int d);   /* 1970-01-01 = 0、先発グレゴリオ */
long rank_elapsed_days(const RankTime *from, const RankTime *to);

/* --- 入口 --- */
int  rank_run(const char *dir);   /* 0 = 正常（無処理 or リセット済）、1 = 失敗・無変更 */

#ifdef RANK_TEST_CLOCK
extern RankTime rank_test_now;    /* テストが書き換える。rank_now はこれを返す */
#endif
#ifdef RANK_TEST_FAULT
typedef enum {
    RANK_FAULT_NONE = 0,
    RANK_FAULT_LEDGER_WRITE,   /* 新 NX93.TXT の書き込み中に失敗させる */
    RANK_FAULT_LEDGER_COMMIT,  /* NX93.TXT の差し替えで失敗させる */
    RANK_FAULT_SCR_WRITE,      /* 新 NX93.SCR の書き込み中に失敗させる */
    RANK_FAULT_SCR_COMMIT      /* NX93.SCR の差し替えで失敗させる */
} RankFault;
extern RankFault rank_test_fault;
#endif
```

`rank_run(NULL)` と `rank_run("")` は裸のファイル名 `NX93.SCR` / `NX93.TXT` を開く
（DOS ビルドの `main` はこれを呼ぶ。98 側で区切り文字を組み立てない）。非空なら
`dir + '/' + 名前`。一時ファイルは `RANKTMP.SCR` / `RANKTMP.TXT`（8.3 準拠）で、
**どの失敗経路でも削除する**。

## 仕様判断（plan.md に無く、ここで固定したもの）

| # | 判断 | 根拠 |
| --- | --- | --- |
| D1 | 行の受理条件は長さのみで決まる: 長さ 27..35、`line[len-2]` `[len-11]` `[len-20]` `[len-23]` `[len-26]` がすべて `0x20`、`line[0..len-27]` がすべて ASCII 数字。lv/miss/date/mode の中身は検証しない | 実データ 30 行すべてで検証済み。中身を検証するとゲームの書式ゆれで誤拒否する |
| D2 | 「7 日以上」＝経過した丸一日が 7 日以上。6日23:59:59 は 6 日 | 境界が一意に決まる |
| D3 | 出力 NX93.SCR に末尾 0x1A は書かない。読むときは許容する | 現役 `nx93.scr` と配布原本の双方に 0x1A が無い。`nx93.bak` にはある |
| D4 | 1 行でもパースできなければ終了 1・無変更（fail closed） | 「台帳を壊さないほうを優先する」の対称。黙って捨てると回復不能 |
| D5 | 11 行以上の NX93.SCR は D4 でエラー | 黙って切り捨てない |
| D6 | NX93.SCR 不在はエラーではなく 0 行。日数判定は先に無条件で行う | 「初回はその場でリセット」と整合。新規インストールが自己修復する |
| D7 | 重複除去の生存者は自分の元 index を保つ（最高スコアの出現位置） | 後段が安定ソートなので、別名同点時の順序を決定的にするため |
| D8 | JSON 文字列は `"`→`\"`、`\`→`\\`、0x00-0x1F→`\u00XX`。0x80 以上は生バイト | SJIS 名は 2 バイト目に 0x5C が来る（ソ 表 十 能）。エスケープしないと台帳が壊れる |
| D9 | 大小同一視は手書きの ASCII a-z 畳み込みのみ。`<ctype.h>` 禁止 | 名前欄に 0x80 以上が入りうる。`toupper` に渡すのは ANSI C では未定義。`stricmp` は QuickC の ANSI 面に無い |
| D10 | ヒープを使わない（`malloc`/`free`/`strdup` を参照しない） | plan「メモリに載せない」を機械検査可能にした形。small モデル 64KB の実態とも一致 |
| D11 | DOS 判定は `RANK_DOS` 一箇所。綴りの一覧（`MSDOS` / `__MSDOS__` / `_DOS`）をそこ以外で繰り返さない | 綴りが共通でないのに、外れても警告なく通る。判定を 1 箇所に寄せると検査がマクロ 1 個で済み、綴りの追加に強い（S8・S9） |

## 検証済みの罠（ハーネス側で必ず踏むこと）

1. **clang は拡張子 `.C` を C++ とみなす**（`clang -### -c RANK.C` が `-x c++` を出す）。
   全コンパイル行に `-x c` を付ける。gcc も同じで、`ia16-elf-gcc -std=c89` に
   `RANK.C` を渡すと `cc1plus: warning: '-std=c90' is valid for C/ObjC but not for C++`
   だけ出して C++ として通る（実測）。`build.sh` は小文字へ写してから `-x c` で渡す。
2. **`mktime` で日数差を取ると夏時間で 7 日が 6 日になる**
   （`TZ=America/Los_Angeles` で 2026-03-07T12:00 → 03-14T12:00 が 6.9583 日）。
   civil-days 算術を必須とし、TZ を 3 通り変えて同一結果を要求する。
3. **オブジェクトの存在チェックを省くと偽グリーンになる**。ビルドが失敗したのに
   `nm` が `2>/dev/null` でエラーを飲み、静的ケースが ok を出す事故が実測で出た。

## ケース一覧

### 静的 (S)

| # | 狙う欠陥 | オラクル |
| --- | --- | --- |
| S1 | RANK.C の生 SJIS バイト → QCL が 0x5C をエスケープ誤読 | `grep '[\200-\377]'` が 0 件 |
| S2 | C99 記法（`//`、宣言と文の混在）→ QuickC 2.00 で通らない | `-x c -std=c89 -pedantic-errors -Wall -Wextra -Werror` が通る |
| S3 | `main` 欠落・リンク不能 | 同フラグでリンク成功 |
| S4 | 台帳を丸読みする実装 | `nm -u` に `_malloc/_calloc/_realloc/_free/_strdup` が無い |
| S5 | テスト継ぎ手が DOS ビルドに残る | `nm` に `rank_test` を含む記号が無い |
| S6 | ロケール依存の大小畳み込み | `toupper/tolower/strcasecmp/stricmp/setlocale` が現れない |
| S7 | `fprintf(fp, line)` 形式の書式注入（名前の `%`） | 第一引数が文字列リテラルでない printf 族が無い。`-Wformat-security` |
| S8 | DOS 判定が特定コンパイラの綴りに依る → 区切りが `/` のまま警告なしで通る | `clang -dM -E` で、`-D` なしは `RANK_DOS` 未定義かつ `RANK_SEP '/'`。`-DMSDOS` / `-D__MSDOS__` / `-D_DOS` の各々で `RANK_DOS` 定義済みかつ `RANK_SEP '\\'` |
| S9 | S8 を迂回して素の `MSDOS` を直接見る箇所が増える | `MSDOS` / `__MSDOS__` / `_DOS` を含む前処理指令が RANK.C 中に 1 行だけ（散文は対象外） |

### 時刻 (U-T)

| # | 狙う欠陥 | オラクル |
| --- | --- | --- |
| U-T1 | civil-days の式ミス | `1970-01-01→0`, `1970-01-02→1`, `1969-12-31→-1`, `2000-02-29→11016`, `2000-03-01→11017`, `2024-02-29→19782`, `1980-01-01→3652`, `2026-09-28→20724` |
| U-T2 | 閏年規則の綻び | 1970-01-01〜2100-12-31 の全 47847 日で「隣接日の差が常に 1」 |
| U-T3 | 不正日時の受理 | 受理: `2026-09-28T21:40:00` / `2000-02-29` / `2024-02-29`。拒否: 長さ≠19, 月 00・13, 日 00・32, `2026-02-29`, `1900-02-29`, 時 24, 分 60, 秒 60, `T` 欠落, 非数字 |
| U-T4 | 7 日境界の off-by-one | 基準 `2026-09-21T21:40:00` に対し `09-27T21:40:00`→6, `09-28T21:39:59`→6, `09-28T21:40:00`→7, `09-29T21:40:00`→8, 同時刻→0, 逆順→負 |
| U-T5 | `mktime`/`localtime` の混入 | `2026-03-07T12:00:00`→`2026-03-14T12:00:00` が **7**。かつ TZ=UTC / Asia/Tokyo / America/Los_Angeles の 3 本で全ケース同一結果 |
| U-T6 | 書式化の桁落ち | 全有効文字列 s について `format(parse(s)) == s` |

### 行パース (U-P)

| # | 狙う欠陥 | オラクル |
| --- | --- | --- |
| U-P1 | 基本的な取り違え | 実データ 3 ファイル 30 行すべてが受理され score/name が期待どおり（表駆動） |
| U-P2 | 長さ境界 | 長さ 27（1 桁）受理 / 35（9 桁）受理 / 26 拒否 / 36 拒否 / 空行 / 1 バイト行 拒否 |
| U-P3 | 先頭から固定長で読む実装 | `len-2, -11, -20, -23, -26` を 1 つずつ非空白に潰した 5 変種を全部拒否 |
| U-P4 | スコアの緩い受理 | 非数字 / 先頭 `+` / 先頭 `-` / 空 → 拒否 |
| U-P5 | 名前の切り出しずれ | `"OMZN    "`, `"Bio_100%"`（8 字ちょうど）, `"A B     "`（内部空白）, `"  AB    "`（先頭空白）, `"        "`（全空白）, `"%n%s%%%%"`, `0x5C`/`0x22` を含む名前 — いずれも 8 バイトが無加工で取れる |
| U-P6 | 畳み込みの過剰 | `omzn`→key `OMZN`。`OMZN@@@@` は `OMZN` と別。0xE0/0xC0 を含む名前は不変。全空白→key 空 |
| U-P7 | 行の正規化 | `raw` が入力バイト列とバイト同一（`memcmp`） |

### 読み取り (U-R)

| # | 狙う欠陥 | オラクル |
| --- | --- | --- |
| U-R1 | 末尾 0x1A で落ちる/1 行増える | `nx93.bak`（332 B、末尾 0x1A）が 10 行として読める |
| U-R2 | 最終行 CRLF 欠落 | 末尾 CRLF を削った 338 B が 10 行 |
| U-R3 | LF のみの行末 | `\n` のみのファイルも 10 行 |
| U-R4 | 空入力でのゼロ除算/誤エラー | 0 バイト / 0x1A のみ → count 0、戻り値 0 |
| U-R5 | 11 行目の黙殺 | 11 行 → `-1` |
| U-R6 | 行バッファ溢れ | 200 バイトの行 → `-1`、ASan/UBSan 無反応、次行に再同期しない |
| U-R7 | 未知の壊れ方 | 固定シード（`srand(20260928)`）でビット反転・切り詰め・挿入した 20000 本。戻り値は常に 0 か -1、ASan/UBSan 無反応 |

### 重複除去・併合 (U-M)

オラクルは plan.md「更新の規則」を手で実行して導出したもの。実装から採らないこと。

**U-M1 期待出力（入力 = 実機 `nx93.scr`、340 バイト）**

```
327400 30 01 OMZN     26/09/15 A
299370 30 00 OMZN@@@@ 61/11/17 A
200000 29 00 Bio_100% 93/10/01 A
180000 28 01 Bio_100% 93/10/01 A
160000 27 02 Bio_100% 93/10/01 A
140000 25 03 Bio_100% 93/10/01 A
120000 22 04 Bio_100% 93/10/01 A
100000 21 05 Bio_100% 93/10/01 A
080000 19 06 Bio_100% 93/10/01 A
060000 16 07 Bio_100% 93/10/01 A
```

**U-M2 期待出力（入力 = 実機 `nx93.bak`、338 バイト）** ← 数値比較 vs 辞書順の決定打

```
299370 30 00 OMZN@@@@ 61/11/17 A
200000 29 00 Bio_100% 93/10/01 A
180000 28 01 Bio_100% 93/10/01 A
160000 27 02 Bio_100% 93/10/01 A
140000 25 03 Bio_100% 93/10/01 A
120000 22 04 Bio_100% 93/10/01 A
100000 21 05 Bio_100% 93/10/01 A
080000 19 06 Bio_100% 93/10/01 A
75170 30 02 OMZN++++ 35/09/28 A
64830 30 02 omzn     80/02/01 A
```

辞書順比較なら `98840` が `200000` の上に来て top-10 が総崩れになる。
5 桁と 6 桁 0 詰めが混在する唯一の実データ。

| # | 狙う欠陥 | オラクル |
| --- | --- | --- |
| U-M1 | 併合全体の取り違え | 上表・340 B・ちょうど 10 行 |
| U-M2 | スコアの辞書順比較 | 上表・338 B |
| U-M3 | 大小同一視の欠落／生存者の取り違え | `OMZN`/`omzn` が 1 件に畳まれ 327400 が残る。`OMZN@@@@` は別件で 299370 |
| U-M4 | 併合が単なる連結 | 昇順に並べた現在表を入力しても出力は降順 |
| U-M5 | 不安定ソート（`qsort`） | 同点 3 行（現在 A, 現在 B, デフォルト）→ 現在A→現在B→デフォルト |
| U-M6 | デフォルト側も重複除去してしまう | 現在表 0 行 → 出力がデフォルト 10 行とバイト同一 |
| U-M7 | 件数不足時の埋め | 現在表 3 行 → 出力ちょうど 10 行 |
| U-M8 | デフォルトの押し込み | 全部別名・全部 900000 台の 10 行 → デフォルトが 1 行も入らない |
| U-M9 | 生存者の位置 | D7 のとおり元 index 昇順。同点別名で検証 |
| U-M10 | 書き出しの形 | 常にちょうど 10 行、各行 `raw` + `\r\n`、末尾 0x1A なし、余分なバイトなし |

### 台帳 (U-L)

| # | 狙う欠陥 | オラクル |
| --- | --- | --- |
| U-L1 | lastUpdate 抽出 | 正常 / 前後空白あり / 別キーが先にある の 3 形で同じ値 |
| U-L2 | 壊れた値の受理 | キー不在 / 値 18・20 バイト / 非数字 / 閉じ `"` 欠落 / 値が数値リテラル → すべて `-1` |
| U-L3 | backups 配列の欠落 | `"backups"` 不在 / `[` 不在 → `rank_ledger_write` が `-1` |
| U-L4 | 空配列への挿入で余計なカンマ | `"backups": []` → 1 件・`,]` が出ない |
| U-L5 | 先頭挿入の向き | 既存 1 件 → 2 件、`backups[0]` が新、旧テキストが `[` 以降バイト同一で末尾に残る |
| U-L6 | 台帳の丸読み・切り捨て | 4000 件・2 MiB 以上の台帳 → 成功し、出力の旧部分が入力とバイト同一 |
| U-L7 | JSON エスケープ欠落 | 名前に `0x22` `0x5C` `0x1A` `0x07` → `\"` `\\` `\u001a` `\u0007`。0x80 以上は生のまま |
| U-L8 | 自作台帳を自分で読めない | 書いた台帳を読み戻すと同じ `RankTime` |

### 統合 (I) — `rank_run(dir)` を `$TMPDIR` 下の使い捨てディレクトリで

| # | 狙う欠陥 | オラクル |
| --- | --- | --- |
| I-1 | 7 日未満で触ってしまう | 6 日 / 6日23:59:59 → 終了 0、SCR と TXT が両方バイト同一 |
| I-2 | 境界でリセットが走らない | ちょうど 7 日 → 終了 0、SCR が U-M1 の期待出力、backups が 1 件増える |
| I-3 | 8 日 | 同上 |
| I-4 | 初回が動かない | NX93.TXT 不在 → リセット。TXT 新規作成、`backups[0].ranking` が元の 10 行 |
| I-5 | 壊れた台帳で書き換えてしまう | lastUpdate 不在 / 値が壊れ / backups 不在 → 終了 1、両ファイルバイト同一、`RANKTMP.*` 残存なし |
| I-6 | バックアップが SCR 書き換えより後 | `RANK_FAULT_SCR_WRITE` / `..._COMMIT` → 終了 1、SCR は元のまま、かつ TXT には元 10 行のバックアップが既に入っている |
| I-7 | 台帳失敗時に SCR を触る | `RANK_FAULT_LEDGER_WRITE` / `..._COMMIT` → 終了 1、SCR も TXT もバイト同一 |
| I-8 | SCR 不在で落ちる | SCR 不在＋7 日経過 → デフォルト 10 行を書く、`backups[0].ranking` が空配列、終了 0 |
| I-9 | backups が古い順／上書き | T0 で実行 → T0+7d で再実行。`backups[0].backupAt == T0+7d` でその ranking が T0 で書いた表、`backups[1].backupAt == T0` でその ranking が元の表 |
| I-10 | 巨大台帳での統合破綻 | 2 MiB の TXT → 成功、旧部分が末尾にバイト同一で残る |
| I-11 | 冪等性 | 7 日未満で 5 回連続実行 → 全部終了 0、両ファイルバイト同一 |
| I-12 | lastUpdate を更新し忘れる | リセット直後に再実行 → 0 日経過なので無処理・終了 0 |
| I-13 | 書き込み不能時の破壊 | SCR を `chmod 0444` / ディレクトリを `chmod 0555` → 終了 1、既存ファイル無傷、一時ファイル残存なし |
| I-14 | パス組み立ての溢れ | `dir` が `RANK_PATH_MAX` 超 → 終了 1、ASan 無反応 |
| I-15 | 11 行 SCR の黙殺 | 11 行 → 終了 1、両ファイル無傷 |
| I-18 | **確定の窓で台帳を失う** | `RANK_FAULT_LEDGER_RENAME` → 終了 1、`NX93.TXT` が元の内容でバイト同一、`NX93.SCR` も元のまま、`RANKTMP.*`・`RANKOLD.*` の残骸なし。直後に再実行すると成功して U-M1 になる |
| I-19 | 確定の窓で表を失う | `RANK_FAULT_SCR_RENAME` → 終了 1、`NX93.SCR` が元の内容でバイト同一、`NX93.TXT` はバックアップ済みの新しい内容（`backupAt` 2 件。確定済みなので戻らない）、残骸なし |
| I-20 | 中断で残った保持ファイルが次回を詰まらせる | 事前に `RANKOLD.SCR` / `RANKOLD.TXT` を置いても実行が成功し、両方消えている |

### 時計の異常 (X)

| # | 狙う欠陥 | オラクル |
| --- | --- | --- |
| X-1 | `lastUpdate` が未来のとき黙って見送る／勝手にリセットする | 経過日数が負（`now < lastUpdate`）→ 終了 **1**、SCR も TXT もバイト同一、`RANKTMP.*` 残存なし。−1 日と −365 日の 2 変種 |

## ミューテーション対応表（実装が緑になったら回す）

使い捨てコピーに 1 つずつ仕込み、指定のテストだけが赤になることを確認する。
複数が同時に赤になる場合は表明が重複している。

| 仕込むミューテーション | 赤になるべきケース |
| --- | --- |
| スコアを先頭 6 桁固定で読む | U-P3, U-M2 |
| スコア比較を `strcmp` に | U-M2 のみ（U-M1 は生き残る） |
| `toupper()` を使う | S6 |
| `qsort` に差し替える（不安定化） | U-M5 |
| dedup で最後の出現を残す | U-M3 |
| 日数差を `mktime` で取る | U-T5 |
| `>= 7` を `> 7` に | I-2 のみ（I-1, I-3 は生き残る） |
| 名前を先頭から 13 バイト目で切る | U-P5 |
| `fputs` を `fprintf(fp, raw)` に | S7, U-M1 |
| SCR を先に書いてから台帳を書く | I-6 |
| 台帳を `malloc` で丸読み | S4, U-L6 |
| 出力末尾に 0x1A を付ける | U-M10, I-2 |
| JSON エスケープを省く | U-L7 |
| 空 backups でもカンマを出す | U-L4 |
| lastUpdate を更新しない | I-12 |

## ハーネス

```
nx93rank/tests/
├── run.sh          clang でビルドし全ケースを走らせる。set -euo pipefail / bash 3.2
├── test_rank.c     #include "../dist/RANK.C" + ハーネス + 全ケース
├── fixtures.h      実データ 3 種のバイト列を C 文字列リテラルで埋め込む
└── README.md       テスト計画
```

- 起動規約: `test_rank <scratch-dir> [name-substring]`、全ケース成功で 0。
- `run.sh` は TZ を `UTC` / `Asia/Tokyo` / `America/Los_Angeles` の 3 通りで 3 回起動し、
  それぞれ別の scratch サブディレクトリを渡す。
- ハーネス側のビルドは `-std=gnu89 -fsanitize=address,undefined -fno-sanitize-recover=all`
  （RANK.C の C89 厳密性は S2 が別途担保）。
- 一時領域は `mktemp -d "${TMPDIR:-/tmp}/nx93rank.XXXXXXXX"`、`trap ... EXIT HUP INT TERM`
  で必ず削除。`grep` は `/usr/bin/grep` に固定（mise.toml の `verify:encoding` と同じ理由）。
  変数展開はすべて `${...}` 波括弧（同 lint 対応）。依存は `clang` と `nm` のみ。
- `fixtures.h` にバイト列を埋め込むのは、デフォルトランキングの出典
  `~/test/nx93_110/NX93.SCR` がリポジトリ外で、他マシンには存在しないため。
  `hdd/` は実機ミラーなのでテストから読まない・書かない。

## 意図的に試験しないもの

- QuickC 2.00 固有のコード生成（16 bit `int`、small モデルのセグメント境界）。
  clang では再現できない。`long` 要求はコンパイル時表明、C89 適合は S2 で近似し、
  残りは実機確認に委ねる。
- GAME.BAT の `errorlevel` 分岐。DOS のバッチ意味論は Mac 上に無い。実機確認。
- nkf 変換後の RANK.C が QCL を通ること。S1（ASCII のみ）で構造的に不要にした。
- NX93.EXE 側の読み取り互換。ブラックボックス。バイト形式の保存で近似。
- 並行性。DOS はシングルタスクで、GAME.BAT は RANK → NX93 の逐次実行。
- 台帳からの復元。plan.md に要件がない。追加するなら仕様追加が先。

## 既知の限界

名前が SJIS のとき、バイト単位の a-z 畳み込みは 2 バイト文字の第 2 バイト
（0x40-0x7E に `a`-`z` を含む）を巻き込み、別の漢字名を同一視しうる。
「大文字小文字を区別せず」の素直な実装が持つ性質で、`Bio_100%` も `OMZN` も
ASCII なので実害は当面ない。
