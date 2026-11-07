---
goal: このプロジェクトの Claude 設定（.claude/ と CLAUDE.md）を agent_settings kit を主体とする構成に再編する
state: 未着手。2026-09-25 にマシン全体の設定を kit のみにした。このプロジェクトの設定は削除せずに残してある。
next: ~/agent_settings/docs/how-to/adopt-in-a-project.ja.md を読み、下の一覧を kit の rules・対象一覧と照合する。重複・矛盾の扱いはユーザーに確認してから変更する。
---

# kit を主体とする構成への再編

2026-09-25 に、ユーザーは全セッションで https://github.com/Sapphire-Lilith/agent_settings
（`~/agent_settings`）だけが読み込まれるようにした。マシン全体（`~/.claude`）の自前の設定と
自動メモリは削除済みである。プロジェクトの設定は、内容を失わないために残してある。

## 手順

1. `~/agent_settings/docs/how-to/adopt-in-a-project.ja.md` に従い、kit のプロジェクト単位の
   導入（path-scoped rules・scoped skills・mise の verify タスク）を行う。
2. 下の各ファイルを kit の rules（`~/.claude/rules/kit`）・agent・skill と照合し、次の 3 つに
   分類する。kit と重複するもの、kit と矛盾するもの、このプロジェクト固有で kit に無いもの。
3. 重複は削除、矛盾は kit 側に合わせる。固有のものは残すかどうかをユーザーに確認する。
   削除・変更はすべてユーザーの承認後に行う。
4. 完了したら、このディレクトリ（`.work/plans/kit-reorg`）を削除する。

## 現在の設定（2026-09-25 時点）

- `.claude/.cc-writes`
- `.claude/settings.local.json`
