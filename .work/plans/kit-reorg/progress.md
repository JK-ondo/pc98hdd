# 経過

- 2026-09-25: 計画を作成した。再編作業は未着手。
- 2026-11-16: **前提を照合したところ、計画の本体が消えていた。** 下のとおり。
  再編として残る作業が「段 2 の install を打つかどうか」の一行だけになったので、
  計画をたたむ。

## 2026-11-16 の照合結果

`~/agent_settings/docs/how-to/adopt-in-a-project.ja.md` の 4 段と突き合わせた。

| 段 | 内容 | 結果 |
| --- | --- | --- |
| 1 | git リポジトリにする | **済**。GitHub 公開の作業で完了した。それ以前はこのプロジェクトの `.claude/` が読まれず、手引きの言うとおり**プロジェクト層が丸ごと不活性**だった |
| 2 | プロジェクトから `install` する | **未**。`.claude/rules/kit` も `.claude/skills/` も無い |
| 3 | リンクを `.gitignore` へ | **実質済**。`/.claude/` が両方を覆っている。手引きは「ここで何も言われなければ何もしなくてよい」と書いている |
| 4 | ゲートを配線する | **済**。`verify` ＋ 5 葉があり、SessionStart は `Verification gate: ACTIVE` を出す |

段 1 と 4 は、この再編ではなく **NX93 ランキングの作業が結果として済ませた**。

## 前提が 3 つ変わっていた

1. **`CLAUDE.md` が存在しない。** 目標は「`.claude/` と `CLAUDE.md` を再編する」と
   書いていたが、このプロジェクトに `CLAUDE.md` は無い。手順 2〜3 の本体だった
   「プロジェクト指示を kit の rules と照合して重複・矛盾・固有に分類する」は、
   **対象が無い**。これが計画をたたむ主な理由である。
2. **ファイル一覧が古かった。** 計画は `.cc-writes` と `settings.local.json` の 2 つを
   挙げていたが、実際は 3 つある。`.claude/agent-memory/kit-test-designer/`（4 ファイル）が
   **計画の 3 日後の 2026-09-28 に、NX93 の作業によって生えていた**。
3. **残った設定に照合すべき中身がほとんど無い。** `settings.local.json` は 4 行で許可
   1 件（`WebFetch(domain:wiki3.jp)`）のみ。サンドボックスの許可ホストは既に `*` なので
   実際の効き目はほぼない。`.cc-writes/` は空で、kit 自身の機構であって「照合する設定」
   ではない。

## 段 2 を打つかどうかについて

打てば `~/agent_settings/rules/project/` のパス限定 rules と scoped skills が載る。
ただし現在の 5 本は `docs.md`・`frontend.md`・`lang-python.md`・`lang-rust.md`・`lang-ts.md`
で、**このプロジェクトは DOS 向け C とシェルなので効くのは `docs.md` くらい**である。
やるなら 1 コマンドで、`.gitignore` は既に覆っているので段 3 の後始末も要らない。
セッション再起動まで効かない点だけ注意。

## 副産物: 壊れていたメモリを 1 件直した

`.claude/agent-memory/kit-test-designer/reference_nx93_fixtures.md` が
フィクスチャ原本の所在を `hdd/game/nx93/` と書いていたが、**2026-11-16 に原本を
`nx93rank/tests/fixtures-src/` へ移したので事実として誤りになっていた**。しかも同日の
`98hdd pull` で `hdd/game/nx93/nx93.scr` は別の表に替わっていて、このメモリを思い出した
エージェントは違うバイト列を読む状態だった。原本 5 種の現在地と、`hdd/` を原本として
指してはいけない理由（pull が黙って上書きする）を書き直した。
