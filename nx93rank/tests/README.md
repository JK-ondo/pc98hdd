# テスト計画（nx93rank）

`dist/RANK.C` の Mac 側回帰テスト。原典は `.work/plans/nx93-ranking-reset/plan.md`（仕様）と
`.work/plans/nx93-ranking-reset/test-design.md`（テスト設計の確定版）。
ケース名は設計の番号をそのまま先頭に持つので、`S1` `U-M2` `I-6` などで grep できる。

オラクルは plan.md「更新の規則」を手で実行して導出したもので、実装から採っていない。
実装より先に書き、赤を確認してから実装に渡している。

## 走らせる

```sh
cd nx93rank/tests
./run.sh              # 全ケース
./run.sh -v           # ケース名を一件ずつ出す
./run.sh -k U-M       # 名前に U-M を含むケースだけ
```

依存は `clang` と `nm` だけ。ネットワークも追加インストールも使わない。
一時ファイルは `mktemp -d "${TMPDIR:-/tmp}/nx93rank.XXXXXXXX"` 配下だけに作り、
`trap ... EXIT HUP INT TERM` で必ず消す。全体で 5 秒ほど。

`dist/RANK.C` が無いあいだは `S0 implementation-present` で赤のまま終わる。
実装前はそれが正しい状態で、緑にしてはいけない。

## 構成

| ファイル | 役割 |
| --- | --- |
| `run.sh` | clang でビルドし、静的検査 S1..S7 を自分で行い、本体を TZ 3 通りで起動する |
| `test_rank.c` | `#include "../dist/RANK.C"` して U-* / I-* / X-1 を実行する |
| `fixtures.h` | 実データ 3 種のバイト列（生成物） |

`test_rank` の起動規約は `test_rank <scratch-dir> [name-substring]`。
全ケース成功で終了 0。フィルタが 1 件も拾わなければ終了 2（空振りを緑と誤認しないため）。

`run.sh` は TZ を `UTC` / `Asia/Tokyo` / `America/Los_Angeles` の 3 通りにして
別々の scratch サブディレクトリで 3 回起動する。日数差の計算に `mktime` や
`localtime` が紛れ込むと夏時間の切替を跨いだ 7 日が 6 日になり、リセットが飛ぶ。

## fixtures.h の由来と再生成

実データを埋め込んでいる理由は 2 つある。デフォルトランキングの出典
`~/test/nx93_110/NX93.SCR`（配布アーカイブ nx93_110.lzh の原本）がリポジトリ外で
他マシンには無いこと、`hdd/` は実機ミラーなのでテストから読み書きしてはいけないこと。

| シンボル | 出典 | 内容 |
| --- | --- | --- |
| `fix_scr` | `hdd/game/nx93/nx93.scr` | 340 B・10 行・CRLF・末尾 0x1A なし |
| `fix_bak` | `hdd/game/nx93/nx93.bak` | 332 B・10 行・**末尾 0x1A あり**・5 桁と 6 桁 0 詰めが混在 |
| `fix_def` | `~/test/nx93_110/NX93.SCR` | 340 B・10 行・`Bio_100%` × 10（デフォルト） |

再生成は手作業。3 ファイルを読んで C 文字列リテラルに起こし、書き出したあと
元ファイルと `cmp` でバイト一致を確認する（作成時に 3 件すべてで確認済み）。
`hdd/` の内容が変わったときだけやり直す。

## fixtures_daily.h の由来と再生成

毎日更新（`run-daily.sh` 側）が使う 3 本。埋め込む理由は `fixtures.h` と同じで、
`hdd/` は実機ミラーなのでテストから読み書きしてはいけない。

| シンボル | 出典 | 内容 |
| --- | --- | --- |
| `fix_scr13` | `hdd/game/nx93/nx93.scr`（2026-10-13 pull） | 340 B・10 行・CRLF・末尾 0x1A なし。実記録 6 行＋出荷時の `Bio_100%` 埋め 4 行 |
| `fix_ledger_lf` | `hdd/game/nx93/nx93.txt`（同 pull） | 581 B・LF 20 本・CR 0 本・非 ASCII 0 バイト。旧 JSON 台帳＝移行の入力 |
| `fix_ledger_crlf` | 上の LF を CRLF へ戻した**再構成** | 601 B（= 581 + LF 20 本） |

`fix_ledger_crlf` は実機を観測したものではない。「98 上の `NX93.TXT` が本当にこの
バイト列か」は実機確認の範囲で、スイート側は「両改行で同じ結果」というより弱く
正しい関係だけを表明する（`U-V3`）。

### 再生成の手順

**手順 0（必須）: 前提の再確認。** `fix_ledger_lf` と `fix_ledger_crlf` の対が
成り立つのは、`98hdd pull` の Shift-JIS→UTF-8 の脚がこの 1 本については恒等変換
だからで、根拠は「非 ASCII が 0 バイト」の 1 点だけ。将来の pull で日本語の名前が
入るとこの前提が黙って崩れ、CRLF 版が実機のバイト列と無関係な再構成になる。
**毎回この 3 つを数え直す。**

```sh
cd <リポジトリのルート>
LC_ALL=C tr -d '\000-\177' < hdd/game/nx93/nx93.scr | wc -c   # 期待 0（非 ASCII）
LC_ALL=C tr -d '\000-\177' < hdd/game/nx93/nx93.txt | wc -c   # 期待 0（非 ASCII）
LC_ALL=C tr -cd '\r'       < hdd/game/nx93/nx93.txt | wc -c   # 期待 0（CR。LF 版の前提）
```

**手順 0-b（必須）: 原本が今のヘッダの出典と同じものか照合する。** `hdd/` はバージョン
管理の外にあり、次の `98hdd pull` が黙って上書きする。差分で気づく手立ては無いので、
毎回ハッシュを突き合わせる。期待値:

```sh
shasum -a 256 hdd/game/nx93/nx93.scr hdd/game/nx93/nx93.txt
# bf5780403bdc5586cc4ebba0da20c4a8cb38a5e9855c96ae7a5a7af645210da5  nx93.scr
# ad0b0b62e74060e853699eefa00f08db7298511108c9ccbcbc041baeec8a17cf  nx93.txt
```

一致しないなら、原本が別の pull のものに替わっている。その場合は再生成する前に、
**どの期待値がその差で動くのか**を `.work/plans/nx93-daily-reset/test-design.md` で
確かめること。`fix_scr13` は `U-G8`・`I-26`・`I-37` のオラクルの出所なので、
原本が替わると試験の期待値がまとめて変わる。

0 でなければそこで止める。日本語名が入ったのなら、`fix_ledger_crlf` を pull から
作ってはいけない（実機から別の経路で採るか、CRLF 版の表明を落とす）。

**手順 1: リテラルに起こす。** 1 行 1 リテラルで、行末の改行まで含める。

```sh
# fix_scr13（原本は CRLF。tr で CR を落としてから \r\n を書き戻す）
LC_ALL=C tr -d '\r' < hdd/game/nx93/nx93.scr \
  | LC_ALL=C sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/    "/' -e 's/$/\\r\\n"/'

# fix_ledger_lf（原本は LF のまま）
LC_ALL=C sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/    "/' -e 's/$/\\n"/' \
  hdd/game/nx93/nx93.txt

# fix_ledger_crlf（同じ原本の LF を CRLF へ戻す）
LC_ALL=C sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/    "/' -e 's/$/\\r\\n"/' \
  hdd/game/nx93/nx93.txt
```

出力を `static const char <名前>[] =` の下に貼り、最終行の後ろに `;` を置く。
`FIX_*_LEN` は `(sizeof <名前> - 1u)`。
`\\` と `"` のエスケープは上の `sed` が面倒を見るが、現在の 3 本に `\` は無く、
`"` は台帳だけに現れる。

**手順 2（必須）: バイト一致の検算。** Python や `sed` の逆変換ではなく、
**コンパイラが実際に作ったバイト列**を書き出して比べる。三連符号や 8 進エスケープの
桁食いはそこでしか現れない。

```sh
cat > "${TMPDIR:-/tmp}/emit.c" <<'EOF'
#include <stdio.h>
#include <string.h>
#include "fixtures_daily.h"
static int dump(const char *path, const char *d, size_t n)
{
    int bad;
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) { perror(path); return 1; }
    bad = (fwrite(d, 1u, n, fp) != n);
    if (fclose(fp) != 0) bad = 1;
    if (bad) perror(path);
    return bad;
}
int main(void)
{
    int bad = 0;
    bad |= dump("scr.out", fix_scr13, FIX_SCR13_LEN);
    bad |= dump("lf.out", fix_ledger_lf, FIX_LEDGER_LF_LEN);
    bad |= dump("crlf.out", fix_ledger_crlf, FIX_LEDGER_CRLF_LEN);
    /* strlen == sizeof-1 で、リテラルに NUL が紛れていないことも見る。 */
    if (strlen(fix_scr13) != FIX_SCR13_LEN) bad = 1;
    if (strlen(fix_ledger_lf) != FIX_LEDGER_LF_LEN) bad = 1;
    if (strlen(fix_ledger_crlf) != FIX_LEDGER_CRLF_LEN) bad = 1;
    return bad;
}
EOF
cd "${TMPDIR:-/tmp}" && clang -x c -std=gnu89 -Wall -Wextra -Werror \
    -I <リポジトリのルート>/nx93rank/tests emit.c -o emit && ./emit
cmp scr.out <リポジトリのルート>/hdd/game/nx93/nx93.scr     # 340 B 一致
cmp lf.out  <リポジトリのルート>/hdd/game/nx93/nx93.txt     # 581 B 一致
LC_ALL=C tr -d '\r' < crlf.out | cmp - lf.out               # 601 B から CR を全削除して 581 B へ戻る
```

3 本すべて一致すること。3 本目は「CRLF 版は LF 版の改行だけを変えたもので、
ほかのバイトは 1 つも動いていない」という、再構成に許される唯一の主張。

## run-daily.sh（毎日更新のスイート。実装中）

```sh
cd nx93rank/tests
./run-daily.sh              # 全ケース
./run-daily.sh -v           # ケース名を一件ずつ出す
./run-daily.sh -k U-K       # 名前に U-K を含むケースだけ
RANK_C=/path/to/RANK.C ./run-daily.sh   # 対象ソースの差し替え
```

`run.sh` とは独立に走り、`mise` からは**まだ呼ばれない**。実装前の赤いケースを
`run.sh` に足すと `mise run verify` が実装完了まで赤になるため、接続は実装が
緑になってからの作業（下記スニペット）。

静的検査は `S10`（ファイル名が 8.3 準拠・新 7 名の出現）・`S11`（旧 4 記号の不在）・
`S12`（表型の関数内宣言が static）・`S6'`（`isalnum` 系の不在）。
`H4` は `euid != 0` の前提検査で、root なら明示的に落ちる（root は読み取り専用
ファイルを素通りするので、`E9` のケースが「書けてしまった」側で偽の緑になる）。
`H2` / `H3` は `test_daily.c` / `test_cli.c` のビルド失敗を名前付き 1 件の赤として
数える（`H1` と同型）。本体は `test_daily` と `test_cli` を TZ 3 通りで起動する。

`RANK_C` は捨てスタブとミューテーション検証のための継ぎ手。実現のため、両ハーネスは
対象ソースを直接 `#include` せず、次の形で取り込む:

```c
#ifndef RANK_C_PATH
#define RANK_C_PATH "../dist/RANK.C"
#endif
#include RANK_C_PATH
```

`run-daily.sh` が `-DRANK_C_PATH="<絶対パス>"` を渡す。

ハーネスは `-std=gnu89` で組む。`fixtures_daily.h` の台帳リテラルは 581 / 601 バイトで、
C90 が要求する文字列リテラル長の下限 509 を超えるので、`-std=c89 -pedantic-errors` では
`-Woverlength-strings` が出る。超えてよいのはハーネス側だけで、`RANK.C` 自身の C89
厳密性は `run.sh` の `S2` が `-std=c89 -pedantic-errors` で押さえている。

接続（実装が全部緑になってから、`mise.toml` へ）:

```toml
[tasks."verify:nx93rank-daily"]
description = "NX93 毎日更新の回帰テスト"
run = "nx93rank/tests/run-daily.sh"
```

あわせて `verify:syntax` と `verify:encoding` の対象リストに
`nx93rank/tests/run-daily.sh` を足す。

## ケースと、そのオラクルの根拠

### 静的 (S) — `run.sh` 内

| ケース名 | 狙う欠陥 | オラクルの根拠 |
| --- | --- | --- |
| `S0 implementation-present` | 実装が無いのに緑になる | 追加。RANK.C 不在で即赤にする。これが無いと S1..S7 が「ファイルが無いので違反も無い」で緑を出す |
| `S1 ascii-only` | 生 SJIS バイト → QCL が 0x5C をエスケープ誤読 | plan.md 実装方針。`ソ 表 十 能` 等は 2 バイト目が 0x5C。`grep '[\200-\377]'` が 0 件 |
| `S2 c89-strict-build` | C99 記法 → QuickC 2.00 で通らない | `-x c -std=c89 -pedantic-errors -Wall -Wextra -Werror -Wformat-security` が通る |
| `S3 dos-link` | `main` 欠落・リンク不能 | 同フラグでリンク成功 |
| `S4 no-heap` | 台帳を丸読みする実装 | D10。`nm -u` に `_malloc/_calloc/_realloc/_reallocf/_free/_strdup` が無い |
| `S5 no-test-seam` | テスト継ぎ手が DOS ビルドに残る | plan.md「DOS ビルドには入らない」。`nm` に `rank_test` を含む記号が無い |
| `S6 no-locale-fold` | ロケール依存の大小畳み込み | D9。`toupper/tolower/strcasecmp/strncasecmp/stricmp/strcmpi/setlocale` と `<ctype.h>`/`<locale.h>` が現れない |
| `S7 no-format-injection` | `fprintf(fp, line)` 形式の書式注入 | 名前に `%` が入りうる（`Bio_100%`）。書式引数が文字列リテラルでない printf 族が無い。`-Wformat-security` が同じ穴をコンパイラ側でも塞ぐ |
| `H1 harness-build` | ハーネスが壊れたまま緑 | 追加。`test_rank.c` のビルド失敗を明示的に 1 件の失敗として数える |

`clang` は拡張子 `.C` を C++ とみなす（`clang -### -c RANK.C` が `-x c++` を出す）ので、
全コンパイル行に `-x c` を付けている。

警告の除外は 1 つも入れていない。macOS の SDK は `sprintf` を deprecated にしているので、
`RANK.C` に `sprintf` が入れば S2 がそこで赤くなる。QuickC 2.00 に `snprintf` は無いから
代替がそのままでは無く、実装側の設計判断が必要になる。黙って通さないために除外を置かない。
現状は `RANK.C` も `test_rank.c` も `sprintf` を 1 箇所も使っていない。

S4 / S5 は `nm` の前にオブジェクトの存在と `nm` 自身の終了コードを確かめる。
これをしないとビルド失敗時に「違反ゼロ」で緑が出る（設計「検証済みの罠」3 番）。
不正な RANK.C を与えて S2/S3/S4/S5 が揃って赤になることを確認済み。

### 契約 (S8)

| ケース名 | 内容 |
| --- | --- |
| `S8 contract-shape` | 追加。`RANK_ROWS/NAME_LEN/MAX_LINE/PATH_MAX`、`RankRow.score` が `long`、`RankTable.row` が 20 要素。ファイル先頭のコンパイル時表明（`typedef char chk_...[...]`）と同内容で、ケース一覧に契約を見えるようにしたもの。98 の `int` は 16 bit で、実データのスコアは全部溢れる |

### 時刻 (U-T)

| ケース名 | 狙う欠陥 | オラクルの根拠 |
| --- | --- | --- |
| `U-T1 civil-days-table` | civil-days の式ミス | 手計算 8 点。`1970-01-01→0`, `1970-01-02→1`, `1969-12-31→-1`, `2000-02-29→11016`, `2000-03-01→11017`, `2024-02-29→19782`, `1980-01-01→3652`, `2026-09-28→20724` |
| `U-T2 civil-days-monotone` | 閏年規則の綻び | 1970-01-01〜2100-12-31 を 1 日ずつ歩き、隣接日の差が常に 1、総日数 47847。2100 は 400 で割れないので閏年でない点をここだけが踏む |
| `U-T3 time-parse-validation` | 不正日時の受理 | D2 と ISO 形式。受理 4 / 拒否 15。長さ 18・20、月 00・13、日 00・32、`2026-02-29`、`1900-02-29`、時 24、分 60、秒 60、`T` 欠落、非数字、空。フィールドが正しい枠に入ることも別に見る |
| `U-T4 elapsed-days-boundary` | 7 日境界の off-by-one | D2「経過した丸一日」。基準 `2026-09-21T21:40:00` に対し 6 / 6 / **7** / 8 / 0 / −1 / −7 |
| `U-T5 elapsed-days-dst-free` | `mktime`/`localtime` の混入 | `2026-03-07T12:00`→`03-14T12:00` が **7**（`mktime` だと 6.9583 日で 6 になる）。秋の戻しも同様。TZ 3 本で同一結果 |
| `U-T6 time-format-roundtrip` | 書式化の桁落ち | 6 年 × 12 月 × 3 日 × 3 時 = 648 点で `format(parse(s)) == s` |

### 行パース (U-P)

D1: 受理条件は長さのみ。長さ 27..35、`line[len-2] [len-11] [len-20] [len-23] [len-26]` が
すべて 0x20、`line[0..len-27]` がすべて ASCII 数字。lv/miss/date/mode の中身は見ない。
行は `score` の桁数が一定しないので、**末尾側から数える**のが要点。

| ケース名 | 狙う欠陥 | オラクルの根拠 |
| --- | --- | --- |
| `U-P1 parse-real-data` | 基本的な取り違え | 実データ 3 ファイル 30 行すべてを表駆動で。score と 8 バイトの name を明記 |
| `U-P2 parse-length-bounds` | 長さ境界 | 行は「スコアの桁数 + 固定 26 バイト」。27（1 桁）受理 / 35（9 桁）受理 / 26・36 拒否 / 空行・1 バイト行 拒否 |
| `U-P3 parse-separator-positions` | 先頭から固定長で読む実装 | 区切り 5 か所を 1 つずつ非空白に潰した 5 変種を全部拒否。**6 桁と 5 桁の両方の行で**行う（6 桁だけだと前から数える実装が生き残る） |
| `U-P4 parse-score-digits` | スコアの緩い受理 | 非数字 / 先頭 `+` / 先頭 `-` / 先頭空白 / 小数点 → 拒否 |
| `U-P5 parse-name-field` | 名前の切り出しずれ | 8 字ちょうど・内部空白・先頭空白・全空白・`%n%s%%%%`・`0x22`/`0x5C` 入り・0x80 以上入りの 8 種を、**スコア 4 桁数（1/5/6/9 桁）すべてで**。6 桁だけだと前から 13 バイト目という誤りが偶然一致して見逃す（ミューテーションで実測） |
| `U-P6 parse-key-folding` | 畳み込みの過剰・不足 | D9。`omzn`→`OMZN`、`OMZN@@@@` は別、前後空白のみ落とす、内部空白は残す、全空白→空、0xE0/0xC0 は不変。同じくスコア 4 桁数すべてで |
| `U-P7 parse-raw-byte-identical` | 行の正規化 | plan.md「行はバイト列のまま保持して書き戻す」。`raw` が入力と `memcmp` 一致 |

### 読み取り (U-R)

| ケース名 | 狙う欠陥 | オラクルの根拠 |
| --- | --- | --- |
| `U-R1 read-trailing-eof-marker` | 末尾 0x1A で落ちる/1 行増える | D3。`nx93.bak`（末尾 0x1A あり）が 10 行 |
| `U-R2 read-missing-final-crlf` | 最終行 CRLF 欠落 | `nx93.scr` から末尾 CRLF を削った 338 B が 10 行 |
| `U-R3 read-lf-only` | LF のみの行末 | CR を抜いた 330 B が 10 行、`raw` に CR が残らない |
| `U-R4 read-empty` | 空入力での誤エラー | D6。0 バイト / 0x1A のみ / CRLF のみ → count 0・戻り値 0 |
| `U-R5 read-eleven-rows` | 11 行目の黙殺 | D5。11 行 → `-1` |
| `U-R6 read-overlong-line` | 行バッファ溢れ | `RANK_MAX_LINE` は 63。200 バイトの行 → `-1`。続けて正しい行を置いても再同期しない |
| `U-R7 read-fuzz` | 未知の壊れ方 | `srand(20260928)` 固定シードでビット反転・切り詰め・挿入を 20000 本。戻り値は必ず `-1` か `[0,10]` の行数で、行数のときは `count` が一致し `raw` が終端されている。ASan/UBSan が残りのオラクル |

設計の「戻り値は常に 0 か -1」は、成功時の戻り値が行数である契約（`int rank_read_table` は
「行数 / -1」）と合わないので、**「`-1`、または `[0, RANK_ROWS]` の行数」**と読んで実装した。

### 重複除去・併合 (U-M)

オラクルは plan.md「更新の規則」を手で実行して導出した。

`nx93.scr` の期待出力（340 B）— `OMZN`/`omzn` が 1 件に畳まれて 327400 が残り、
`OMZN@@@@` は別人として 299370 が残る。空いた 8 席はデフォルトの上位が埋める。

`nx93.bak` の期待出力（338 B）— 生存者は `OMZN@@@@` 299370 / `OMZN++++` 75170 /
`omzn` 64830 / `OMZN$$$$` 54600 / `OMZN~~~~` 40520 / `omzo` 15030 の 6 件。
このうちデフォルトに勝つのは上 3 件だけ。**辞書順比較なら `98840` が `200000` の上に来て
下半分が総崩れになる**。5 桁と 6 桁 0 詰めが混在する唯一の実データで、ここが決定打。

| ケース名 | 狙う欠陥 | オラクルの根拠 |
| --- | --- | --- |
| `U-M1 rebuild-live-scr` | 併合全体の取り違え | 上表・ちょうど 10 行・書き出し 340 B |
| `U-M2 rebuild-bak-numeric-order` | スコアの辞書順比較 | 上表・書き出し 338 B |
| `U-M3 rebuild-case-insensitive-dedup` | 大小同一視の欠落／生存者の取り違え | `OMZN`/`omzn` が 1 件で 327400、`OMZN@@@@` は別で 299370、key `OMZN` はちょうど 1 行 |
| `U-M4 rebuild-sorts-descending` | 併合が単なる連結 | 昇順に並べ替えた現在表を入力しても出力は U-M1 と同一で、スコアが非増加 |
| `U-M5 rebuild-stable-on-ties` | 不安定ソート（`qsort`） | 同点 3 行（現在 A・現在 B・デフォルト 200000）→ 現在A → 現在B → デフォルト |
| `U-M6 rebuild-empty-gives-defaults` | デフォルト側も重複除去してしまう | 現在表 0 行 → 出力が `rank_default_line` 10 行と一致し、書き出しが配布原本とバイト同一。`Bio_100%` 10 件は席を埋める役なので畳んではいけない |
| `U-M7 rebuild-always-ten` | 件数不足時の埋め | 現在表 3 行（900000 / 500000 / 1）→ ちょうど 10 行。900000 と 500000 が 2 席、デフォルト上位 8 件が残り、**スコア 1 の行は 060000 に負けて入らない** |
| `U-M8 rebuild-defaults-pushed-out` | デフォルトの押し込み | 全部別名・900000 台の 10 行 → デフォルトが 1 行も入らず、入力の順で並ぶ |
| `U-M9 rebuild-survivor-index` | 生存者の位置 | D7。`AAA`100000 / `BBB`500000 / `AAA`500000 → AAA の生存者は index 2 なので index 1 の BBB が先。同点別名の順序がこれで決まる |
| `U-M10 write-table-shape` | 書き出しの形 | D3。各行 `raw` + `\r\n` のみ、余分なバイトなし、末尾 0x1A なし |

### 台帳 (U-L)

| ケース名 | 狙う欠陥 | オラクルの根拠 |
| --- | --- | --- |
| `U-L1 ledger-read-last-update` | lastUpdate 抽出 | 正常 / 前後空白あり / 別キー（文字列値を含む）が先にある の 3 形で同じ `RankTime` |
| `U-L2 ledger-reject-corrupt` | 壊れた値の受理 | キー不在 / 値 18・20 バイト / 非数字 / 閉じ `"` 欠落 / 数値リテラル / `null` / 空 / `{` → すべて `-1`。plan.md「壊れている → 何も書き換えず終了 1」の読み側 |
| `U-L3 ledger-write-needs-backups-array` | backups 配列の欠落 | `"backups"` 不在 / `[` 不在（`null`）/ キー直後で切れている → `-1` |
| `U-L4 ledger-insert-into-empty` | 空配列への挿入で余計なカンマ | `"backups": []` → 1 件。カンマの直後に `]`/`}` が来ない。`[` 以降の旧バイトが末尾に残る |
| `U-L5 ledger-prepend-preserves-tail` | 先頭挿入の向き | 既存 1 件 → 2 件、`backups[0]` が新・`backups[1]` が旧、旧テキストが `[` 以降バイト同一で末尾に残る |
| `U-L6 ledger-huge-streamed` | 台帳の丸読み・切り捨て | plan.md「全体を読み込まずに」「メモリに載せない」。4000 件以上かつ 2 MiB 以上の台帳で成功し、旧部分が末尾にバイト同一で残る |
| `U-L7 ledger-json-escape` | JSON エスケープ欠落 | D8。`0x22`→`\"`、`0x5C`→`\\`、`0x1A`/`0x07`→`\u` エスケープ、0x80 以上は生バイト、出力に 0x20 未満が残らない。加えて**逆変換して元バイトに戻る**ことを見る。名前に 0x22/0x5C を含む行が台帳を往復してバイト一致することも確認する |
| `U-L8 ledger-roundtrip` | 自作台帳を自分で読めない | 書いた台帳を読み戻すと同じ `RankTime` |

### 統合 (I) — `rank_run(dir)` を `$TMPDIR` 下の使い捨てディレクトリで

各ケースは自分専用のディレクトリを作り、実行前のバイト列を控えて比較する。

| ケース名 | 狙う欠陥 | オラクルの根拠 |
| --- | --- | --- |
| `I-1 run-under-seven-days` | 7 日未満で触ってしまう | 6 日 / 6日23:59:59 → 終了 0、SCR と TXT が両方バイト同一、`RANKTMP.*` 残存なし |
| `I-2 run-exactly-seven-days` | 境界でリセットが走らない | ちょうど 7 日 → 終了 0、SCR が U-M1 の期待出力、backups が 1 件増え、`backups[0].ranking` が元の 10 行そのまま |
| `I-3 run-eight-days` | 8 日 | 同上 |
| `I-4 run-first-time-no-ledger` | 初回が動かない | 決定事項 3。NX93.TXT 不在 → リセット、TXT 新規作成、`backups[0].ranking` が元の 10 行 |
| `I-5 run-corrupt-ledger-fails-closed` | 壊れた台帳で書き換えてしまう | 7 形（lastUpdate 不在 / 壊れ値 / backups 不在 / 空 / 非 JSON）→ 終了 1、両ファイルバイト同一、`RANKTMP.*` 残存なし |
| `I-6 run-backup-precedes-scr-write` | バックアップが SCR 書き換えより後 | plan.md「先にバックアップ。ここまで成功して初めて NX93.SCR に触る」。`RANK_FAULT_SCR_WRITE` / `..._COMMIT` → 終了 1、SCR は元のまま、かつ TXT に元 10 行のバックアップが既に入っている |
| `I-7 run-ledger-failure-leaves-scr` | 台帳失敗時に SCR を触る | `RANK_FAULT_LEDGER_WRITE` / `..._COMMIT` → 終了 1、SCR も TXT もバイト同一 |
| `I-8 run-missing-scr` | SCR 不在で落ちる | D6。SCR 不在＋7 日経過 → デフォルト 10 行を書く、`backups[0].ranking` が空配列、終了 0 |
| `I-9 run-backups-newest-first` | backups が古い順／上書き | plan.md「backups は新しい順」。T0 で実行 → T0+7d で再実行。`backups[0].backupAt == T0+7d` でその ranking が T0 で書いた表、`backups[1].backupAt == T0` でその ranking が元の表 |
| `I-10 run-huge-ledger` | 巨大台帳での統合破綻 | 2 MiB の TXT → 成功、旧部分が末尾にバイト同一で残る |
| `I-11 run-idempotent` | 冪等性 | 7 日未満で 5 回連続実行 → 全部終了 0、毎回両ファイルバイト同一 |
| `I-12 run-updates-last-update` | lastUpdate を更新し忘れる | リセット直後に再実行 → 0 日経過なので無処理・終了 0・無変更 |
| `I-13a run-readonly-dir` | 書き込み不能時の破壊 | ディレクトリを `chmod 0555` → 一時ファイルすら作れない → 終了 1、既存ファイル無傷、一時ファイル残存なし |
| `I-13b run-readonly-scr` | 同（ファイル側） | SCR を `chmod 0444`。下記のとおり**全か無かだけ**を見る |
| `I-14 run-path-too-long` | パス組み立ての溢れ | `dir` が `RANK_PATH_MAX` 超 → 終了 1、ASan 無反応 |
| `I-15 run-eleven-row-scr` | 11 行 SCR の黙殺 | D5。11 行 → 終了 1、両ファイル無傷 |
| `I-16 run-unparsable-line` | 読めない行の黙殺 | 追加。D4 を統合レベルで。3 形の壊れた行 → 終了 1、両ファイルバイト同一 |
| `I-17 run-bare-filenames` | 98 側で区切り文字を組み立ててしまう | 追加。契約「`rank_run(NULL)` と `rank_run("")` は裸のファイル名を開く」。カレントを移して両方が同じ結果になる |
| `I-18 run-ledger-rename-rolls-back` | 確定の 2 段目で失敗して半端に残る | `RANK_FAULT_LEDGER_RENAME` → 終了 1、`NX93.TXT` が元の内容でバイト同一、`NX93.SCR` も元のまま、残骸なし。**加えて直後にそのまま再実行すると成功して U-M1 になる**（半端に確定していない証明。これが無いと「見た目は戻っているが次が通らない」状態を見逃す） |
| `I-19 run-scr-rename-keeps-ledger` | 表の失敗に台帳を巻き添えにする | `RANK_FAULT_SCR_RENAME` → 終了 1、`NX93.SCR` が元の内容でバイト同一、`NX93.TXT` は**戻らない**（`backupAt` が 2 件、`backups[0].ranking` が元の 10 行、`lastUpdate` が now）。台帳のバックアップはその時点で行の唯一の写しなので、両ファイルの辻褄を合わせるために捨てるのが唯一の回復不能な操作 |
| `I-20 run-clears-stale-retained` | 中断後の残骸で 2 回目が詰まる | 事前に `RANKOLD.SCR`/`RANKOLD.TXT` を置いても成功し、両方消えている。変種 2 つ（下記） |

DOS には置き換える `rename` が無いので、確定は「退避（`NX93.*` → `RANKOLD.*`）→ 本名へ
`rename` → 退避を捨てる」の 3 段になる。新しい 2 つの fault は 2 段目だけを失敗させる。
`expect_no_temps` は `RANKTMP.*` に加えて `RANKOLD.*` の残骸も見るので、この不変条件は
統合ケース全部にかかっている。

### 時計の異常 (X)

| ケース名 | 狙う欠陥 | オラクルの根拠 |
| --- | --- | --- |
| `X-1 run-clock-moved-backwards` | 巻き戻った時計を黙って見送る | 経過日数が負（now < lastUpdate）→ 終了 1、SCR も TXT もバイト同一、`RANKTMP.*` 残存なし。変種は −1 日 と −365 日。黙って見送るとリセットが永久に来なくなるため、GAME.BAT の PAUSE で知らせて `DATE` で直してもらう（RTC 電池切れで実際に起きる） |

## 設計に対して判断したこと

設計から落としたケースは無い。以下は設計が一意でなかった箇所の扱いで、実装側で
別の読み方をしているなら教えてほしい。

1. **`I-13` を `I-13a` / `I-13b` に分けた。** 設計は「SCR を `chmod 0444` / ディレクトリを
   `chmod 0555` → 終了 1、既存ファイル無傷」と両方に同じオラクルを与えているが、
   **読み取り専用ファイルは POSIX では書き込み障壁にならない。** ディレクトリが書ければ
   `rename(2)` は対象のモードに関わらず差し替えるし、MS-DOS でも先に消してから
   `rename` するので同じ。したがって `I-13b` を「終了 1 でなければならない」にすると
   正しい実装を落としてしまう。`I-13b` のオラクルは
   「終了 1 なら無変更、終了 0 なら正しくリセット済み。半端な状態にならない。
   どちらでも `RANKTMP.*` は残らない」にした。`I-13a`（ディレクトリ 0555）は
   設計どおり終了 1 を要求している。
2. **`rank_json_escape` が囲みの `"` を出すかは契約に書かれていない。** 出力が `"` で
   始まり全体が 1 個の JSON 文字列として読めればそのまま、そうでなければ `"` で囲んで
   から検証する。どちらの規約でも通る。
3. **`\u` エスケープの 16 進が大文字か小文字か、設計内で食い違っている**（D8 は `\u00XX`、
   U-L7 の表は 16 進 2 桁を `001a` と小文字で書いている）。両方受理している。
4. **`rank_ledger_write` の `old` に `NULL` を渡す経路を「台帳が無い初回」と解釈した。**
   台帳を新規作成する道は他に無く、I-4 がその振る舞いを要求している。`U-L8` が
   `old = NULL` で直接叩く。別の規約にするなら U-L8 を直す必要がある。
5. **U-R7 の「戻り値は常に 0 か -1」を「`-1` か `[0,10]` の行数」と読んだ**（上記 U-R7 の項）。
6. **`I-20` に「RANKOLD 名を別のもので塞ぐ」変種を足した。** 素の残骸ファイルだけでは
   **この機械で何も検証できない**。確定の 1 段目が名前を要求する前に `remove` する狙いは
   「DOS の `rename` は対象が在ると失敗する」ことへの備えだが、POSIX の `rename` は
   対象を置き換えるので、`remove` を消しても Mac 上では何も変わらない（実測：その
   ミューテーションが素のファイル変種を生き延びた）。空ディレクトリで名前を塞ぐと
   `rename` はどちらの系でも上書きできないので、`remove` の欠落が観測できるようになる。
   実装が 98 で詰まる欠陥を Mac 上で捕まえるための踏み台で、正しい実装は両変種を通る。
7. **追加したケース**: `S0`（実装不在で即赤）、`H1`（ハーネスのビルド失敗を 1 件として数える）、
   `S8`（契約の可視化）、`I-16`（D4 を統合レベルで）、`I-17`（裸のファイル名）。

## このスイート自身の検証

実装が無い状態で書いたので、**捨てスタブ実装**（`$TMPDIR` 内、リポジトリには入れない）を
別に起こして次を確認してある。

- 全 61 ケースが TZ 3 通りで緑になり、ASan/UBSan が無反応であること。
  静的検査 S1..S7 も正当な C89 実装に対して誤検出しないこと。
  いまの実装で **61 ケース・979 表明**。表明の総数は実装によって十数だけ動く。
  `I-13b` が全か無かのオラクルで、実装が終了 1 を選ぶか 0 を選ぶかで走る表明の
  数が変わるため。**ケース数が 61 から動いたらそちらは取りこぼし**なので調べること。
- 設計「ミューテーション対応表」を 1 つずつ仕込んで、**指定のケースが赤になること**。
  スコアを先頭 6 桁固定 →`U-P3`、スコア比較を `strcmp` →`U-M2`、`toupper()` →`S6`、
  ソートを不安定化 →`U-M5`、dedup で最後を残す →`U-M3`、日数差を `mktime` →`U-T5`、
  `>= 7` を `> 7` →`I-2`（`I-1`/`I-3` は生存）、名前を前から 13 バイト目で切る →`U-P5`、
  `fprintf(fp, raw)` →`S7`、SCR を先に書く →`I-6`、台帳の尾を切る →`U-L6`、
  出力末尾に 0x1A →`U-M10`/`I-2`、JSON エスケープ省略 →`U-L7`、
  空 backups にカンマ →`U-L4`、lastUpdate を更新しない →`I-12`、
  11 行目を黙殺 →`U-R5`/`I-15`、読めない行を飛ばす →`I-16`、
  巻き戻りを無視 →`X-1`、失敗時に一時ファイルを残す →`I-6`。
- 確定（commit）の 3 段についても、いまの実装のコピーに仕込んで確認した。
  失敗時に退避から戻さない →`I-18`/`I-19`、成功後に `RANKOLD.*` を消さない
  →`I-2` ほか多数（`expect_no_temps` が全統合ケースで見ている）、
  退避前に `RANKOLD.*` を消さない →`I-20`（塞ぐ変種のみ。判断 6 のとおり）。
- 壊れた RANK.C を与えると `S2`/`S3`/`S4`/`S5` が揃って赤になること
  （オブジェクトが無いのに `nm` のエラーを飲んで緑を出す事故＝設計「検証済みの罠」3 番）。

この過程で自分のテストの穴が 1 件見つかって直した。`U-P5`/`U-P6` が 6 桁スコアの行しか
使っておらず、6 桁では「前から 13 バイト目」と「末尾から 19 バイト目」が同じ位置に
なるため、名前を前から切る実装を見逃していた。いまは 1/5/6/9 桁すべてで回している。

## 意図的に試験しないもの

- QuickC 2.00 固有のコード生成（16 bit `int`、small モデルのセグメント境界）。clang では
  再現できない。`long` 要求はコンパイル時表明、C89 適合は S2 で近似し、残りは実機確認。
- `GAME.BAT` の `errorlevel` 分岐。DOS のバッチ意味論は Mac 上に無い。実機確認。
- nkf 変換後の RANK.C が QCL を通ること。S1（ASCII のみ）で構造的に不要にした。
- NX93.EXE 側の読み取り互換。ブラックボックス。バイト形式の保存で近似。
- 並行性。DOS はシングルタスクで、GAME.BAT は RANK → NX93 の逐次実行。
- 台帳からの復元。plan.md に要件がない。追加するなら仕様追加が先。

## 既知の限界

名前が SJIS のとき、バイト単位の a-z 畳み込みは 2 バイト文字の第 2 バイト（0x40-0x7E に
`a`-`z` を含む）を巻き込み、別の漢字名を同一視しうる。「大文字小文字を区別せず」の
素直な実装が持つ性質で、`Bio_100%` も `OMZN` も ASCII なので実害は当面ない。
