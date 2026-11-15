@echo off
rem
rem     NyaHaX'93 起動用バッチファイル
rem     RANK.EXE がランキングをリセットしてから、ゲーム本体を起動する。
rem     カレントディレクトリをこのファイルのある場所にして GAME と打つこと。
rem
rem     更新の頻度を変えるには、下の RANK の行を書き換える。ここが唯一の指定場所で、
rem     ディスクのどこにも記録されない。
rem         RANK /D    日付が変わったらリセットする（出荷時）
rem         RANK /W    週が変わったら（月曜 00:00 を跨いだら）リセットする
rem         RANK       引数なしは /W と同じ
rem     打ち間違いは黙って既定に落ちず、何も書き換えずに errorlevel 1 で止まる。
rem
RANK /D
if errorlevel 1 ECHO ランキングの更新に失敗しました。表はそのままです。
if errorlevel 1 PAUSE
NX93
