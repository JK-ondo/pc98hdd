@echo off
rem
rem     NyaHaX'93 起動用バッチファイル
rem     RANK.EXE が週に一度ランキングをリセットしてから、ゲーム本体を起動する。
rem     カレントディレクトリをこのファイルのある場所にして GAME と打つこと。
rem
RANK
if errorlevel 1 ECHO ランキングの更新に失敗しました。表はそのままです。
if errorlevel 1 PAUSE
NX93
