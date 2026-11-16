# テスト計画（nx93rank）

`dist/RANK.C` の Mac 側回帰テスト。スイートは 2 本ある。

| スイート | ケース数 | 原典 |
| --- | --- | --- |
| `run.sh` | 53 | 週次リセットの設計（この README のケース表が現役の記述） |
| `run-daily.sh` | 85 | [`../docs/SPEC.md`](../docs/SPEC.md) と [`../docs/TEST-DESIGN.md`](../docs/TEST-DESIGN.md) |

`run.sh` は週次リセットの時代からの回帰で、毎日更新への改修でも緑を保つ側。
`run-daily.sh` は毎日更新・引数・移行・故障注入を見る側で、実装より先に赤として
置いた。どちらもケース名は設計の番号をそのまま先頭に持つので、`S1` `U-M2` `I-6`
`U-K3` `I-43` `A-6` などで grep できる。

オラクルは仕様を手で実行して導出したもので、実装から採っていない。

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
| `run.sh` | clang でビルドし、静的検査 S1..S9 を自分で行い、`test_rank` を TZ 3 通りで起動する |
| `test_rank.c` | `#include "../dist/RANK.C"` して U-* / I-1..I-20 / X-1 を実行する |
| `fixtures.h` | 実データ 3 種のバイト列（生成物） |
| `run-daily.sh` | 静的検査 S10..S12・S6' を行い、`test_daily` と `test_cli` を TZ 3 通りで起動する |
| `test_daily.c` | U-A / U-C / U-K / U-F / U-G / U-V / I-21..I-53 / X-2 / X-3 |
| `test_cli.c` | A-1..A-7。`#define main rank_entry` で本物の `main` を取り込む |
| `harness_daily.h` | 上の 2 本が共有する表明・ファイル操作・stdout 採取・13 パスの `Env` |
| `fixtures_daily.h` | 実機の 2026-10-13 ミラーから採った 3 種のバイト列（生成物） |

起動規約はどのハーネスも `<name> <scratch-dir> [name-substring]`。
全ケース成功で終了 0。フィルタが 1 件も拾わなければ終了 2（空振りを緑と誤認しないため）。
`test_daily` と `test_cli` は scratch が深すぎる場合、1 件目を走らせる前に終了 2 で
止まる（`<dir>/RANKWHO.TXT` が `RANK_PATH_MAX` に収まらないと `rank_run` が E2 を
返し、RANK.C とは無関係な理由で統合ケースがまとめて赤くなるため）。
`test_cli` は scratch に絶対パスを要求する（A-4 がカレントディレクトリを移すので、
相対パスだとケースの途中で解決先が変わる）。

どちらのスイートも TZ を `UTC` / `Asia/Tokyo` / `America/Los_Angeles` の 3 通りにして
別々の scratch サブディレクトリで 3 回起動する。暦の計算に `mktime` や
`localtime` が紛れ込むと、夏時間の切替を跨いだ日付が 1 日ずれて跨ぎ判定が飛ぶ。

## fixtures.h の由来と再生成

実データを埋め込んでいる理由は 2 つある。デフォルトランキングの出典
`~/test/nx93_110/NX93.SCR`（配布アーカイブ nx93_110.lzh の原本）がリポジトリ外で
他マシンには無いこと、`hdd/` は実機ミラーなのでテストから読み書きしてはいけないこと。

**原本は `fixtures-src/` にある**（下の `fixtures_daily.h` の節で理由を述べる。
両スイートで同じ事情）。日付入りの名前なのは、実機の表が時と共に変わり、
どの時点の標本かがフィクスチャの性質そのものだから。

| シンボル | 出典 | 内容 |
| --- | --- | --- |
| `fix_scr` | `fixtures-src/nx93-2026-09-28.scr` | 340 B・10 行・CRLF・末尾 0x1A なし。実記録 10 行（埋め行なし） |
| `fix_bak` | `fixtures-src/nx93.bak` | 332 B・10 行・**末尾 0x1A あり**・5 桁と 6 桁 0 詰めが混在 |
| `fix_def` | `~/test/nx93_110/NX93.SCR`（配布アーカイブの原本。**リポジトリ外**） | 340 B・10 行・`Bio_100%` × 10（デフォルト） |

再生成は手作業。ファイルを読んで C 文字列リテラルに起こし、書き出したあと
元ファイルと `cmp` でバイト一致を確認する（作成時に 3 件すべてで確認済み）。

`fix_def` だけは出典がリポジトリ外にある。NEC/同人の配布物なので取り込めない。
この 1 本は再生成できない環境があることを承知で残してある（既定 10 行の原本なので
`rank_default_line()` の実装からも読み取れる）。

## fixtures_daily.h の由来と再生成

毎日更新（`run-daily.sh` 側）が使う 3 本。埋め込む理由は `fixtures.h` と同じで、
`hdd/` は実機ミラーなのでテストから読み書きしてはいけない。

| シンボル | 出典 | 内容 |
| --- | --- | --- |
| `fix_scr13` | `fixtures-src/nx93-2026-10-13.scr`（2026-10-13 pull） | 340 B・10 行・CRLF・末尾 0x1A なし。実記録 6 行＋出荷時の `Bio_100%` 埋め 4 行 |
| `fix_ledger_lf` | `fixtures-src/nx93-2026-10-13.txt`（同 pull） | 581 B・LF 20 本・CR 0 本・非 ASCII 0 バイト。旧 JSON 台帳＝移行の入力 |
| `fix_ledger_crlf` | 上の LF を CRLF へ戻した**再構成** | 601 B（= 581 + LF 20 本） |

**原本は `fixtures-src/` に置いてバージョン管理下にある。** 元は `hdd/game/nx93/` を
直に指していたが、`hdd/` は管理外で `98hdd pull` が黙って上書きするため、原本が
pull ごとに消える置き方だった。2026-11-16 の pull で実際に両方が替わり、ヘッダの
出典が作業ツリーから失われかけた。原本は 921 バイトしかないので、取り込んで固定する。
`.gitattributes` の `nx93rank/tests/fixtures-src/** binary` が改行変換を止めている
（`nx93.scr` は CRLF なので、リポジトリ既定の `eol=lf` に触らせると壊れる）。

`fix_ledger_crlf` のバイト列そのものは実機を観測したものではない。ただし**換算の
関係は実機で実測した**: 2026-11-16 の pull で得た台帳は LF 3,181 B・95 行で、同じ
ファイルを `98hdd ls` が HDD 上で **3,276 B** と報告した（3,181 ＋ 95 = 3,276）。
つまり「LF のバイト数 ＋ 行数 ＝ 実機の CRLF のバイト数」は実機で成り立つ。これを
581 B・20 行の原本へ当てると 601 B になる。推定ではなく、実測した関係からの導出。
それでも「98 上のこの台帳が本当にこのバイト列か」は別の問いなので、スイート側は
「両改行で同じ結果」というより弱く正しい関係だけを表明する（`U-V3`）。

### 再生成の手順

**手順 0（必須）: 前提の再確認。** `fix_ledger_lf` と `fix_ledger_crlf` の対が
成り立つのは、`98hdd pull` の Shift-JIS→UTF-8 の脚がこの 1 本については恒等変換
だからで、根拠は「非 ASCII が 0 バイト」の 1 点だけ。将来の pull で日本語の名前が
入るとこの前提が黙って崩れ、CRLF 版が実機のバイト列と無関係な再構成になる。
**毎回この 3 つを数え直す。**

```sh
cd <リポジトリのルート>
LC_ALL=C tr -d '\000-\177' < nx93rank/tests/fixtures-src/nx93-2026-10-13.scr | wc -c  # 期待 0（非 ASCII）
LC_ALL=C tr -d '\000-\177' < nx93rank/tests/fixtures-src/nx93-2026-10-13.txt | wc -c  # 期待 0（非 ASCII）
LC_ALL=C tr -cd '\r'       < nx93rank/tests/fixtures-src/nx93-2026-10-13.txt | wc -c  # 期待 0（CR。LF 版の前提）
```

**手順 0-b（必須）: 原本が今のヘッダの出典と同じものか照合する。** 原本は管理下に
入ったので pull では動かなくなったが、ヘッダは手で起こしたものなので、原本を
差し替えたときに起こし直しを忘れる経路は残る。毎回ハッシュを突き合わせる。期待値:

```sh
shasum -a 256 nx93rank/tests/fixtures-src/nx93-2026-10-13.scr nx93rank/tests/fixtures-src/nx93-2026-10-13.txt
# bf5780403bdc5586cc4ebba0da20c4a8cb38a5e9855c96ae7a5a7af645210da5  nx93.scr
# ad0b0b62e74060e853699eefa00f08db7298511108c9ccbcbc041baeec8a17cf  nx93.txt
```

一致しないなら、原本が別の pull のものに替わっている。その場合は再生成する前に、
**どの期待値がその差で動くのか**を [`../docs/TEST-DESIGN.md`](../docs/TEST-DESIGN.md) で
確かめること。`fix_scr13` は `U-G8`・`I-26`・`I-37` のオラクルの出所なので、
原本が替わると試験の期待値がまとめて変わる。

0 でなければそこで止める。日本語名が入ったのなら、`fix_ledger_crlf` を pull から
作ってはいけない（実機から別の経路で採るか、CRLF 版の表明を落とす）。

**原本を新しい pull のものへ差し替える判断は、それ自体が独立した決定である。**
新しい実機の表が必ず良い原本とは限らない。`fix_scr13` の役目は決定 7（埋め行を
記録として扱わない）と決定 8（末尾マークを無視する）を同時に踏む入力であることで、
そのために「実記録 6 行・鍵が全員 `OMZN`・埋め 4 行」という形が要る。2026-11-16 の
実機の表は実記録 4 行・別人 1 名を含み、鍵が 1 種類でなくなるので、同じ網を
張らない。原本を替えるなら、網が保たれるかを先に確かめる。

**手順 1: リテラルに起こす。** 1 行 1 リテラルで、行末の改行まで含める。

```sh
# fix_scr13（原本は CRLF。tr で CR を落としてから \r\n を書き戻す）
LC_ALL=C tr -d '\r' < nx93rank/tests/fixtures-src/nx93-2026-10-13.scr \
  | LC_ALL=C sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/    "/' -e 's/$/\\r\\n"/'

# fix_ledger_lf（原本は LF のまま）
LC_ALL=C sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/    "/' -e 's/$/\\n"/' \
  nx93rank/tests/fixtures-src/nx93-2026-10-13.txt

# fix_ledger_crlf（同じ原本の LF を CRLF へ戻す）
LC_ALL=C sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/    "/' -e 's/$/\\r\\n"/' \
  nx93rank/tests/fixtures-src/nx93-2026-10-13.txt
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
cmp scr.out <リポジトリのルート>/nx93rank/tests/fixtures-src/nx93-2026-10-13.scr   # 340 B 一致
cmp lf.out  <リポジトリのルート>/nx93rank/tests/fixtures-src/nx93-2026-10-13.txt   # 581 B 一致
LC_ALL=C tr -d '\r' < crlf.out | cmp - lf.out               # 601 B から CR を全削除して 581 B へ戻る
```

3 本すべて一致すること。3 本目は「CRLF 版は LF 版の改行だけを変えたもので、
ほかのバイトは 1 つも動いていない」という、再構成に許される唯一の主張。

## run-daily.sh（毎日更新のスイート）

```sh
cd nx93rank/tests
./run-daily.sh              # 全ケース
./run-daily.sh -v           # ケース名を一件ずつ出す
./run-daily.sh -k U-K       # 名前に U-K を含むケースだけ
./run-daily.sh -k I-43      # 故障注入 16 点だけ
RANK_C=/path/to/RANK.C ./run-daily.sh   # 対象ソースの差し替え
```

中身は 85 ケース（`test_daily` 78 ＋ `test_cli` 7）で、静的検査 4 件と前提検査
3 件が前に付く。TZ 3 通りで回して 30 秒ほど。2 MiB の旧台帳を扱うケース
（`U-V5`・`I-42`）が時間の大半を使う。

`run.sh` とは独立に走り、**`mise` からはまだ呼ばれない。** 実装前の赤いケースを
`run.sh` に足すと `mise run verify` が実装完了まで赤になるため、接続は実装が
緑になってから `mise.toml` へ 1 行足す作業として分けてある（下記スニペット。
実装は緑になったので、あとはその作業だけが残っている）。

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

### 接続（ユーザー作業。`mise.toml` は実装側では触らない）

実装が全部緑になったので、あとは `mise.toml` へ次を足すだけ。
**この書き置きは手順の控えであって、実装側が勝手に `mise.toml` を変えてはいない。**

```toml
[tasks."verify:nx93rank-daily"]
description = "NX93 毎日更新の回帰テスト"
run = "nx93rank/tests/run-daily.sh"
```

あわせて `verify:syntax` と `verify:encoding` の対象リストに
`nx93rank/tests/run-daily.sh` を足す（`verify:syntax` は `bash -n` の対象、
`verify:encoding` は `$VAR` に非 ASCII が続く綴りの検査）。
`run-daily.sh` の中身を `run.sh` へ畳むのは任意で、やるなら後日。

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
| `S8 contract-shape` | 追加。`RANK_ROWS/NAME_LEN/MAX_LINE/PATH_MAX`、`RankRow.score` が `long`、`RankTable.row` が 20 要素。加えて毎日更新の面として `RANK30_ROWS`=30・`RANKWHO_ROWS`=100・`RANK_MODE_WEEKLY`=0・`RANK_MODE_DAILY`=1、2 枚の表が**容量 +1 行**の作業余白を持つこと（挿入してから落とすため）、状態ファイルが 45 バイトでタイムスタンプが offset 20 から始まること。さらに `RANK_PERIOD_DAYS` と `RANK_WS_MAX` が**定義されていない**ことを `#ifdef` + `#error` で見る（消えた 2 関数は前処理では見えないので `run-daily.sh` の `S11` が文字列で見る）。ファイル先頭のコンパイル時表明（`typedef char chk_...[...]`）と同内容で、ケース一覧に契約を見えるようにしたもの。98 の `int` は 16 bit で、実データのスコアは全部溢れる |

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
| `U-P6 parse-key-folding` | 畳み込みの過剰・不足 | D9 ＋毎日更新の決定 8。`omzn`→`OMZN`、**`OMZN@@@@`→`OMZN`（同一人物。末尾のマークは識別に使わない）**、`Bio_100%`→`BIO_100`、前後空白のみ落とす、内部空白は残す、全空白→空、0xE0/0xC0 は不変。同じくスコア 4 桁数すべてで。鍵規則そのものの網は `run-daily.sh` の `U-K1`..`U-K8` が張る |
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

`nx93.scr` の期待出力（340 B）— 10 行すべてが決定 8 の鍵 `OMZN` に畳まれるので、
実記録として残るのは 327400 の 1 件だけ。空いた 9 席はデフォルトの上位
200000..040000 が埋める。

`nx93.bak` の期待出力（340 B）— こちらも `OMZN@@@@`×3・`OMZN++++`・`OMZN$$$$`・
`OMZN~~~~`・`omzn`×3 が全員 `OMZN` 1 人に畳まれ、生存者は 299370 の 1 件。
`omzo` だけが別人として残るが、その 15030 はデフォルトの 020000 に負けて入らない。
**このケースはもう「スコアを数値で比べているか」を決めない**（5 桁の行が畳み込みで
消えるため）。その網は `run-daily.sh` の `U-G10` が引き継いでいる。

| ケース名 | 狙う欠陥 | オラクルの根拠 |
| --- | --- | --- |
| `U-M1 rebuild-live-scr` | 併合全体の取り違え | 上表・ちょうど 10 行・書き出し 340 B |
| `U-M2 rebuild-bak-numeric-order` | 鍵の畳み込み漏れ | 上表・書き出し 340 B |
| `U-M3 rebuild-case-insensitive-dedup` | 大小同一視の欠落／生存者の取り違え | 実 `nx93.scr` では key `OMZN` がちょうど 1 行で 327400、2 行目はデフォルトの 200000（`OMZN@@@@` が同一人物なので席が空く）。別人の側は `OMZN`/`OMZN@@@@`/`omzn`/`omzo` の 4 行表で見て、`OMZN` が 1 行・`omzo` が別人として自分の行を保つ |
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
| `U-L1 ledger-read-last-update` | lastUpdate 抽出 | 正常 / 前後空白あり / 別キー（文字列値を含む）が先にある の 3 形で同じ `RankTime`。新しい 45 バイトの状態ファイルも旧台帳と同じ接頭辞を持つので、この読み手は両方から時刻を取り出す |
| `U-L2 ledger-reject-corrupt` | 壊れた値の受理 | キー不在 / 値 18・20 バイト / 非数字 / 閉じ `"` 欠落 / 数値リテラル / `null` / 空 / `{` → すべて `-1`。「壊れている → 何も書き換えず終了 1」の読み側 |

**削除した 6 ケース**（`U-L3`〜`U-L8`）。台帳への追記そのものが毎日更新で消え、
`rank_ledger_write` と `rank_json_escape` が `RANK.C` から無くなったため。
網の行き先:

| 消えたケース | 行き先 |
| --- | --- |
| `U-L3` backups 配列の欠落 | `run-daily.sh` の `U-V4`（移行側の文法検査・25 変種） |
| `U-L4` 空配列への挿入 | `U-V6`（空 backups / 空 ranking の取り込み） |
| `U-L5` 先頭挿入の向き | **なし**（機能ごと消えた） |
| `U-L6` 巨大台帳の streaming（書き側） | `U-V5`（読み側で 2 MiB）と `S4`（ヒープ不使用）が継続 |
| `U-L7` JSON エスケープ | `U-V1`/`U-V2`（**復号側**へ反転。`\\`→0x5C が往復の網を引き継ぐ） |
| `U-L8` 台帳の往復 | `U-F8`（状態ファイルの往復） |

### 統合 (I) — `rank_run(dir, mode)` を `$TMPDIR` 下の使い捨てディレクトリで

各ケースは自分専用のディレクトリを作り、実行前のバイト列を控えて比較する。控えるのは
5 枚（`NX93.SCR` `NX93.TXT` `RANK30.TXT` `RANKWHO.TXT` `NX93OLD.TXT`）で、
`expect_no_temps` は `RANKTMP.*` 4 本と `RANKOLD.*` 4 本の残骸を見る。

**前提は日数ではなく暦の境界になった。** 2026-09-21 と 2026-09-28 はどちらも月曜で、
あいだの日曜がその週を閉じる。この対が「週次が跨がない」と「跨ぐ」の境界で、
経過日数には触れていない（6 日 23 時間 59 分でも跨がず、2 分でも跨ぐ）。
モードは `RANK_MODE_WEEKLY` 固定で、モードの選択そのものは `run-daily.sh` の
`I-21`・`I-22` が見る。

| ケース名 | 狙う欠陥 | オラクルの根拠 |
| --- | --- | --- |
| `I-1 run-inside-one-week` | 境界を跨いでいないのに触ってしまう | 木曜 / 日曜 23:59:59 → 終了 0、5 枚バイト同一、残骸なし |
| `I-2 run-crosses-one-monday` | 境界でリセットが走らない | 月曜 → 終了 0。`RANK30.TXT` が元の 10 行そのまま（行一致の重複除去なので 1 行も畳まれない）、`RANKWHO.TXT` が 1 行（全員 `OMZN`）、`NX93.SCR` が U-M1 の期待出力、`NX93.TXT` が 45 バイトの状態ファイル |
| `I-3 run-crosses-two-mondays` | 境界を 2 本跨いでリセットが 2 回走る | 2 週前の月曜から → リセット 1 回、`lastUpdate` が now、直後の再実行が 0・無変更 |
| `I-4 run-first-time-no-state` | 初回が動かない | `NX93.TXT` 不在 → リセット、4 枚が新しくなる |
| `I-5 run-unusable-state-fails-closed` | 読めない `NX93.TXT` で書き換えてしまう | 7 形（lastUpdate 不在 / 壊れ値 2 種 / 空 / 非 JSON / 状態でも台帳でもない / backups が `null`）→ 終了 1、5 枚バイト同一、2 枚の表が**作られない**、残骸なし |
| `I-6 run-tables-precede-scr-write` | 記録の写しより先に `NX93.SCR` を書く | `RANK_FAULT_SCR_WRITE` / `..._COMMIT` → 終了 1、`NX93.SCR` と `NX93.TXT` は元のまま、かつ `RANK30.TXT`/`RANKWHO.TXT` に**消えるはずだった行がすでに入っている** |
| `I-7 run-state-failure-leaves-tables` | 状態ファイルを先に書く順序 | `RANK_FAULT_TXT_WRITE` / `..._COMMIT` → 終了 1、`NX93.TXT` が元の時刻のまま、前 3 枚は新しい。**直後の再実行で完遂する**。順序が逆なら、リセットを記録した後で `NX93.SCR` の書き換えに失敗した起動が二度とリセットしなくなる |
| `I-8 run-missing-scr` | SCR 不在で落ちる | SCR 不在＋跨ぎ → 終了 0、デフォルト 10 行を書き、2 枚の表は **0 バイトのファイルとして存在**（不在ではない） |
| `I-11 run-idempotent` | 冪等性 | 跨がない起動を 5 回連続 → 全部終了 0、毎回 5 枚バイト同一 |
| `I-12 run-updates-last-update` | lastUpdate を更新し忘れる | リセット直後に同じ時刻で再実行 → 跨がないので無処理・終了 0・無変更 |
| `I-13a run-readonly-dir` | 書き込み不能時の破壊 | ディレクトリを `chmod 0555` → 一時ファイルすら作れない → 終了 1、既存ファイル無傷、残骸なし |
| `I-13b run-readonly-scr` | 同（ファイル側） | SCR を `chmod 0444`。下記のとおり**全か無かだけ**を見る。4 枚それぞれを読み取り専用にする変種は `run-daily.sh` の `I-32` |
| `I-14 run-path-too-long` | パス組み立ての溢れ | `dir` が `RANK_PATH_MAX` 超 → 終了 1、ASan 無反応。境界ちょうどは `I-47` |
| `I-15 run-eleven-row-scr` | 11 行 SCR の黙殺 | D5。11 行 → 終了 1、5 枚無傷、2 枚の表が作られない |
| `I-16 run-unparsable-line` | 読めない行の黙殺 | 追加。D4 を統合レベルで。3 形の壊れた行 → 終了 1、5 枚バイト同一 |
| `I-17 run-bare-filenames` | 98 側で区切り文字を組み立ててしまう | 追加。契約「`rank_run(NULL, m)` と `rank_run("", m)` は裸のファイル名を開く」。カレントを移して両方が同じ結果になる |
| `I-18 run-state-rename-rolls-back` | 差し替えの rename で失敗して半端に残る | `RANK_FAULT_TXT_RENAME` → 終了 1、`NX93.TXT` が元の内容でバイト同一、退避名 `RANKOLD.TXT` が消えている（復帰 rename が走った証拠。この機械では残留バイトで `*_COMMIT` と区別できないので、ここが E14 の網）。**加えて直後に再実行すると成功して 4 枚が揃う** |
| `I-19 run-scr-rename-keeps-tables` | 表の失敗に記録の写しを巻き添えにする | `RANK_FAULT_SCR_RENAME` → 終了 1、`NX93.SCR` が元の内容でバイト同一、`NX93.TXT` は手付かず、**`RANK30.TXT`/`RANKWHO.TXT` は戻らない**。2 枚はその時点で消える行の唯一の写しなので、辻褄合わせに捨てるのが唯一の回復不能な操作 |
| `I-20 run-clears-stale-retained` | 中断後の残骸で 2 回目が詰まる | 事前に `RANKOLD.SCR`/`RANKOLD.TXT` を置いても成功し、両方消えている。変種 2 つ（下記）。`RANKOLD.30`/`RANKOLD.WHO` を置かないのは、初回リセットでは 2 枚の表が不在で退避が起きず、そこに置いた写しは**残るのが正しい**ため。4 本すべての変種は `run-daily.sh` の `I-46` |

DOS には置き換える `rename` が無いので、確定は「退避（本名 → `RANKOLD.*`）→ 本名へ
`rename` → 退避を捨てる」の 3 段になる。`*_RENAME` の fault は 2 段目だけを失敗させ、
復帰 rename は本物を走らせる。`expect_no_temps` は `RANKTMP.*` に加えて `RANKOLD.*` の
残骸も見るので、この不変条件は統合ケース全部にかかっている。
退避を捨てる前に画面へ 1 行出す（W2）ことの網は `run-daily.sh` の `I-53` にある。

### 時計の異常 (X)

| ケース名 | 狙う欠陥 | オラクルの根拠 |
| --- | --- | --- |
| `X-1 run-clock-moved-backwards` | 巻き戻った時計を黙って見送る | `rank_time_before(now, last)` が 1（= 時計が最終リセットより前）→ 終了 1、5 枚バイト同一、残骸なし。変種は −1 日 と −365 日で、前提そのものを `rank_time_before` で表明してから走らせる。秒粒度・同日内の巻き戻りは `run-daily.sh` の `X-2` が担う。黙って見送るとリセットが永久に来なくなるため、GAME.BAT の PAUSE で知らせて `DATE` で直してもらう（RTC 電池切れで実際に起きる） |

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
2. **U-R7 の「戻り値は常に 0 か -1」を「`-1` か `[0,10]` の行数」と読んだ**（上記 U-R7 の項）。
3. **`I-7` のオラクルは反転した。** 旧実装は台帳を先に書いたので「台帳の書き込みに失敗
   したら何も起きていない」を表明していたが、毎日更新では `NX93.TXT` が**最後**の段に
   なる（`spec.md` §3.6）。したがって同じ注入点での正しい残留は「前 3 枚が新しく、
   `NX93.TXT` だけが古い」である。`I-19` と同種の、順序の変更に追随する書き換え。
4. **`I-6` の表明は強くなった。** 「`NX93.TXT` に元 10 行のバックアップが入っている」が
   「`RANK30.TXT`/`RANKWHO.TXT` に消えるはずだった行が入っている」になる。
   記録の行き先が台帳から 2 枚の平文表へ移ったのに合わせただけで、不変条件
   （`NX93.SCR` から行が消えるのは写しが先にできた後だけ）は同じ。
5. **`I-20` に「RANKOLD 名を別のもので塞ぐ」変種を足した。** 素の残骸ファイルだけでは
   **この機械で何も検証できない**。確定の 1 段目が名前を要求する前に `remove` する狙いは
   「DOS の `rename` は対象が在ると失敗する」ことへの備えだが、POSIX の `rename` は
   対象を置き換えるので、`remove` を消しても Mac 上では何も変わらない（実測：その
   ミューテーションが素のファイル変種を生き延びた）。空ディレクトリで名前を塞ぐと
   `rename` はどちらの系でも上書きできないので、`remove` の欠落が観測できるようになる。
   実装が 98 で詰まる欠陥を Mac 上で捕まえるための踏み台で、正しい実装は両変種を通る。
6. **追加したケース**: `S0`（実装不在で即赤）、`H1`（ハーネスのビルド失敗を 1 件として数える）、
   `S8`（契約の可視化）、`I-16`（D4 を統合レベルで）、`I-17`（裸のファイル名）。

## このスイート自身の検証

実装が無い状態で書いたので、**捨てスタブ実装**（`$TMPDIR` 内、リポジトリには入れない）を
別に起こして次を確認してある。

- 全ケースが TZ 3 通りで緑になり、ASan/UBSan が無反応であること。
  静的検査 S1..S9 も正当な C89 実装に対して誤検出しないこと。
  いまの実装で **53 ケース・1029 表明**（毎日更新の前は 61 ケース・979 表明で、
  差は台帳追記の 6 ケースと `I-9`/`I-10` を削除した分）。表明の総数は実装によって
  十数だけ動く。`I-13b` が全か無かのオラクルで、実装が終了 1 を選ぶか 0 を選ぶかで
  走る表明の数が変わるため。**ケース数が 53 から動いたらそちらは取りこぼし**なので
  調べること。
- 設計「ミューテーション対応表」を 1 つずつ仕込んで、**指定のケースが赤になること**。
  スコアを先頭 6 桁固定 →`U-P3`、`toupper()` →`S6`、
  ソートを不安定化 →`U-M5`、dedup で最後を残す →`U-M3`、日数差を `mktime` →`U-T5`、
  名前を前から 13 バイト目で切る →`U-P5`、
  `fprintf(fp, raw)` →`S7`、SCR を先に書く →`I-6`、
  出力末尾に 0x1A →`U-M10`/`I-2`、lastUpdate を更新しない →`I-12`、
  11 行目を黙殺 →`U-R5`/`I-15`、読めない行を飛ばす →`I-16`、
  巻き戻りを無視 →`X-1`、失敗時に一時ファイルを残す →`I-6`。
  スコア比較を `strcmp` にする変異はこのスイートでは殺せなくなった（`U-M2` の 5 桁行が
  鍵の畳み込みで消えたため）ので、`run-daily.sh` の `U-G10` が引き継いでいる。
  毎日更新ぶんの対応表は [`../docs/TEST-DESIGN.md`](../docs/TEST-DESIGN.md) §14 にある。
- 確定（commit）の 3 段についても、いまの実装のコピーに仕込んで確認した。
  失敗時に退避から戻さない →`I-18`/`I-19`、成功後に `RANKOLD.*` を消さない
  →`I-2` ほか多数（`expect_no_temps` が全統合ケースで見ている）、
  退避前に `RANKOLD.*` を消さない →`I-20`（塞ぐ変種のみ。判断 5 のとおり）。
- 壊れた RANK.C を与えると `S2`/`S3`/`S4`/`S5` が揃って赤になること
  （オブジェクトが無いのに `nm` のエラーを飲んで緑を出す事故＝設計「検証済みの罠」3 番）。

`run-daily.sh` の側は、実装の 3 フェーズを通して 1 ケースずつ赤から緑へ移ったことで
帰属を確かめている。実測は [`../docs/TEST-DESIGN.md`](../docs/TEST-DESIGN.md) §14.3 にある。

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
  `NX93OLD.TXT` の「存在検査 → 後で rename」は check-then-act だが、DOS では無害。
- `RANK30.TXT` / `RANKWHO.TXT` からの復元・剪定・編集機能。`spec.md` §2 の非目標。
  追加するなら仕様追加が先。
- モードの永続化。`spec.md` §2 の非目標。`NX93.TXT` に `mode` 欄が無いことは
  `U-F5`（45 バイトのバイト一致）が自動的に保証する。
- エラーメッセージの文意。「できる対処を述べているか」「読んで分かるか」は自然文の
  正しさなので機械オラクルを持たない。機械で見るのは ①替わったファイルの基本名が
  文面に現れる ②11 条件の文面が互いに異なる の 2 点だけで（`I-43`・`I-51`・`A-6`）、
  残りは audit の人手レビュー項目。

## 既知の限界

名前が SJIS のとき、バイト単位の a-z 畳み込みは 2 バイト文字の第 2 バイト（0x40-0x7E に
`a`-`z` を含む）を巻き込み、別の漢字名を同一視しうる。「大文字小文字を区別せず」の
素直な実装が持つ性質で、`Bio_100%` も `OMZN` も ASCII なので実害は当面ない。
