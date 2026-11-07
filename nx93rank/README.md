# NyaHaX'93 週次ランキングリセット

NyaHaX'93 のランキング（`NX93.SCR`）は上位 10 件しか持たないため、強い一人が上位を
占め続けると他の人の記録が入らなくなる。週に一度、各人の最高記録 1 件だけを残して
配布時のランキングへ差し戻し、席を空ける。消す前の表は必ず全件バックアップする。

仕様と設計の確定版は `.work/plans/nx93-ranking-reset/plan.md`。

## 構成

| ファイル | 置き場所 | 役割 |
| --- | --- | --- |
| `dist/GAME.BAT` | 98: `A:\GAME\NX93\` | 入口。`RANK` → `NX93` の順に起動する |
| `dist/RANK.EXE` | 98: `A:\GAME\NX93\` | 更新プログラム。`build.sh` が Mac 上で作る |
| `dist/RANK.C` | 98: `A:\GAME\NX93\` | その原本。98 上の `QCL RANK.C` に戻すための保険 |
| `build.sh` | Mac のみ | `RANK.C` → `dist/RANK.EXE`。ia16-elf-gcc を Docker 経由で呼ぶ |
| `.built-from` | Mac のみ | `dist/RANK.EXE` が由来する `RANK.C` の sha256 |
| `tests/` | Mac のみ | Mac 側の回帰テスト。98 には送らない |

98 側で動くのは `GAME.BAT` と `RANK.EXE` の 2 つ。`RANK.C` も一緒に送るが、98 上で
使うのは QuickC へ戻すときだけ。

## 導入手順

### 1. Mac で RANK.EXE を作る

```sh
mise run build:nx93rank
```

Docker で ia16-elf-gcc 6.3.0 を呼び、small モデルの MZ 実行形式を `dist/RANK.EXE`
へ書く。イメージはダイジェストで固定してあるので、`:latest` が動いても結果は変わらない。
MZ ヘッダが無ければビルドは失敗する（`-mcmodel` が効かずに tiny が出て、拡張子だけ
`.EXE` のフラットイメージを掴まされるのを防ぐため）。

`dist/RANK.EXE` はリポジトリにコミットしてあるので、`RANK.C` を触っていなければこの
手順は飛ばせる。触ったのに作り直し忘れた場合は `mise run verify` が
`verify:nx93rank-binary` で落ちる（sha256 の照合。Docker は要らない）。

### 2. Mac から 98 へ送る

SD カードを Mac にマウントした状態で（手順は README.md の 3 章）:

```sh
./98hdd send nx93rank/dist /GAME/NX93
```

`dist/` の 3 ファイルだけが `A:\GAME\NX93\` に入る。`.bat` と `.c` は拡張子
ホワイトリストに載っているので Shift-JIS + CR+LF へ自動変換され、`.exe` は載っていない
ので `[BIN]` としてそのままコピーされる（SPEC.md §5.5）。

### 3. 遊ぶ

```
A:\GAME\NX93>GAME
```

`RANK.EXE` が週に一度だけランキングを整理し、そのあと `NX93.EXE` が起動する。
`GAME.COM` や `GAME.EXE` をこのディレクトリに置かないこと。MS-DOS のコマンド解決順は
COM → EXE → BAT なので、`GAME.BAT` が実行されなくなる。

## QuickC に戻すとき（保険）

ia16-elf-gcc の newlib と QuickC のランタイムは別物なので、実機で `RANK.EXE` が
おかしな挙動をしたら 98 上でビルドし直して切り分ける。

```
A:\>CD \GAME\NX93
A:\GAME\NX93>QCL RANK.C
```

`QCL` のパスと `INCLUDE` / `LIB` は `AUTOEXEC.BAT` が設定済み（SPEC.md §7.1）。
メモリモデルは既定の small でよい。浮動小数点は使っていないので数値ライブラリは
引き込まれない。`RANK.OBJ` が残るが消してよい。Mac から送った `RANK.EXE` は
上書きされる。

この保険があるために `RANK.C` には制約が残っている（後述の「制約」）。やめるなら
`dist/RANK.C` を送るのをやめ、ASCII 限定の検査（`tests/run.sh` の S1）も外せる。

## 動作

起動のたびに `RANK.EXE` が `NX93.TXT` の `lastUpdate` を見る。

| 状態 | 動作 | 終了コード |
| --- | --- | --- |
| 前回から 7 日未満 | 何もしない | 0 |
| 前回から 7 日以上 | バックアップしてリセット | 0 |
| `NX93.TXT` が無い（初回） | バックアップしてリセット | 0 |
| `NX93.TXT` が壊れている | 何もしない | 1 |
| `lastUpdate` が未来（時計が巻き戻った） | 何もしない | 1 |
| `NX93.SCR` に読めない行がある / 11 行以上ある | 何もしない | 1 |

終了コード 1 のときは `GAME.BAT` がメッセージを出して `PAUSE` する。**表は 1 バイトも
書き換わらない。** そのままキーを押せばゲームは起動する。

リセットの中身:

1. 先に `NX93.TXT` へ現在の表を全件バックアップする。ここまで成功して初めて
   `NX93.SCR` に触る。
2. 現在の 10 件を名前で重複除去する。前後の空白を落とし、大文字小文字を区別せず
   比較し、同名は最高スコアの 1 件だけ残す（`OMZN` と `omzn` は同じ人）。
3. 残った件を配布時のランキング（`Bio_100%` の 10 件）と併合し、スコア降順で
   上位 10 件を採る。デフォルト側は重複除去しないので、席が余れば `Bio_100%` が埋める。

## `NX93.TXT`（バックアップ台帳）

```json
{
  "lastUpdate": "2026-09-28T21:40:00",
  "backups": [
    {
      "backupAt": "2026-09-28T21:40:00",
      "ranking": [
        "327400 30 01 OMZN     26/09/15 A"
      ]
    }
  ]
}
```

- `backups` は新しい順。消えた記録はすべてここに残る。
- 追記は先頭への挿入で、ファイル全体をメモリに載せない。週 400 バイト程度で増える。
- Mac に吸い上げるときは `98hdd pull` で `hdd/game/nx93/nx93.txt` に入る
  （`.txt` は変換対象なので UTF-8 + LF になる）。

### 記録を手で戻したいとき

`NX93.TXT` の `ranking` 配列の行は `NX93.SCR` の 1 行そのものなので、戻したい行を
`NX93.SCR` に書き戻せばよい。10 行・CR+LF・末尾の 0x1A なし、を守ること。

## Mac 側のテスト

```sh
nx93rank/tests/run.sh
```

`mise run verify` からも走る。実機の DOS では試せないもの（コンパイラのコード生成、
DOS のランタイム挙動、`errorlevel` 分岐、`NX93.EXE` 側の読み取り互換）は範囲外で、
実機確認に委ねている。何を試験していないかは `tests/README.md` に書いてある。

クロスコンパイルができても、ここは縮まない。`clang` で回す 61 ケースが保証するのは
RANK.C の論理で、`ia16-elf-gcc` が通ることが保証するのは C89 適合とリンクまで。
newlib の `fopen` がバイナリモードで CR+LF を素通しするか、`rename` / `remove` が
DOS でどう振る舞うかは、実機で一度動かすまで未検証のまま。

## 制約

- `RANK.C` のメッセージは ASCII に限る。Shift-JIS の 2 バイト目に `0x5C` を含む字
  （ソ・表・能 等）を QuickC がエスケープとして解釈するため。日本語を出したい箇所は
  `GAME.BAT` の `ECHO` に置く。クロスコンパイルだけなら不要な制約で、QuickC を保険に
  残しているあいだだけ効く。
- DOS 判定は `RANK.C` の `RANK_DOS` 一箇所に集約してある。QuickC は `MSDOS` を、
  ia16-elf-gcc は `__MSDOS__` と `_DOS` を定義し、どれも共通ではない。外すとパスの
  区切りが `/` のまま警告なしで通る（`tests/run.sh` の S8・S9 が固定している）。
- `RANK.EXE` は**カレントではなく自分の居場所**（`argv[0]` のディレクトリ）の
  `NX93.SCR` / `NX93.TXT` を読む。DOSSHELL から起動してもゲーム本体と同じ表を見る。
  DOS 2.x は `argv[0]` を空で渡すので、その場合だけカレントに落ちる。
