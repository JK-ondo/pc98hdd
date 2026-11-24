#!/usr/bin/env bash
#
# 統合ケース。run-cards.sh が source する（ハーネスは既に読み込まれている）。
# exit を書かない — ランナーごと終わって残りが走らなくなる。
#
# ケース名は test-design.md §6 の番号に対応する。

# ======================================================= I-B: 後方互換
# 仕様 §3.2。カード定義を持たない利用者の振る舞いが変わらないこと。
#
# **I-B1・I-B2・I-B3・I-B5 は現行実装に対して緑になるはず。** まだ何も変えて
# いないので。ここが緑にならなければ、ハーネスか期待値が間違っている
# （実装の欠陥ではない）。これが設計の検算である。
#
# I-B4（暗黙経路を告げる）と I-B6（HDD_OFFSET が実効値）は新しい要求なので赤。

ib_scratch=$(scratch_new)
ib_img="${ib_scratch}/img/legacy.hda"
make_image "$ib_img" "0C010A1F"
seed_image "$ib_img" "HELLO.TXT" "hello from the card"
seed_image "$ib_img" "READ.ME" "readme body"
write_rc "$ib_scratch" "$ib_img"
# 現行実装はドライブ p: を使い、その定義は ~/.mtoolsrc にしかない。HOME を
# 砂場へ向けると消えるので、利用者の現在の環境を再現するために書く。
# 実装後はこれが不要になり、そのことを I-B7 が見る。
write_mtoolsrc "$ib_scratch" "$ib_img"
# 通す版の偽 mtools。本物を呼ぶので状態が実際に動き、同時に呼び出しも記録される。
fake_mtools_install "${ib_scratch}/bin" "${ib_scratch}/mtools.log" 1

# ---- I-B1 ls が現行どおり動く
if h_want "I-B1"; then
    run_98hdd "$ib_scratch" ls
    assert_exit "I-B1 legacy-ls exit" 0 "$h_last_status"
    assert_contains "I-B1 legacy-ls lists HELLO" "HELLO" "$h_last_output"
fi

# ---- I-B2 pull の展開先が <BASE_DIR>/hdd（hdd/default ではない）
# 暗黙の default カードを hdd/default へ落とすと、既存利用者のミラーが
# 行方不明になる。既定値の置き場そのものが契約である。
if h_want "I-B2"; then
    : > "${ib_scratch}/mtools.log"
    run_98hdd "$ib_scratch" pull
    assert_exit "I-B2 legacy-pull exit" 0 "$h_last_status"
    assert_file "I-B2 legacy-pull dest" "${ib_scratch}/home/9801/hdd/hello.txt"
    assert_nofile "I-B2 legacy-pull not-hdd-default" \
        "${ib_scratch}/home/9801/hdd/default"
    got=$(cat "${ib_scratch}/home/9801/hdd/hello.txt" 2>/dev/null || true)
    assert_eq "I-B2 legacy-pull content" "hello from the card" "$got"
fi

# ---- I-B3 send がイメージへ届く
if h_want "I-B3"; then
    mkdir -p "${ib_scratch}/home/9801/send/SUB"
    printf 'sent body' > "${ib_scratch}/home/9801/send/SUB/NEW.TXT"
    : > "${ib_scratch}/mtools.log"
    run_98hdd "$ib_scratch" send "${ib_scratch}/home/9801/send"
    assert_exit "I-B3 legacy-send exit" 0 "$h_last_status"
    listing=$(MTOOLS_SKIP_CHECK=1 "$HARNESS_REAL_MDIR" -s -b \
              -i "${ib_img}@@${HARNESS_OFFSET}" :: 2>/dev/null || true)
    assert_contains "I-B3 legacy-send landed" "NEW.TXT" "$listing"
fi

# ---- I-B4 暗黙経路に入ったことを述べる（新しい要求なので現行では赤）
if h_want "I-B4"; then
    run_98hdd "$ib_scratch" ls
    assert_contains "I-B4 legacy-announced" "default" "$h_last_output"
fi

# ---- I-B5 HDD_DRIVE が残っていてもエラーにならない
# 公開済みの設定ファイルを読み込み時に落とさないこと。write_rc は
# HDD_DRIVE="p" を書くので、ここまで緑であること自体が証拠になるが、
# 終了コードとして明示する。
if h_want "I-B5"; then
    run_98hdd "$ib_scratch" ls
    assert_exit "I-B5 legacy-hdd-drive-ok" 0 "$h_last_status"
    assert_lacks "I-B5 legacy-hdd-drive-silent" "HDD_DRIVE" "$h_last_output"
fi

# ---- I-B6 HDD_OFFSET が実効値になる（現行では未使用なので赤）
# 誤ったオフセットを書いたら E9 で止まるべき。現行は ~/.mtoolsrc 側が
# 実効値なので、誤った値でも素通りする。
if h_want "I-B6"; then
    ib6_scratch=$(scratch_new)
    ib6_img="${ib6_scratch}/img/bad-offset.hda"
    make_image "$ib6_img" "11112222"
    write_rc "$ib6_scratch" "$ib6_img" $(( HARNESS_OFFSET + 512 ))
    write_mtoolsrc "$ib6_scratch" "$ib6_img"
    fake_mtools_install "${ib6_scratch}/bin" "${ib6_scratch}/mtools.log" 1
    run_98hdd "$ib6_scratch" ls
    assert_exit "I-B6 offset-effective exit" 1 "$h_last_status"
    assert_contains "I-B6 offset-effective message" "オフセット" "$h_last_output"
    scratch_drop "$ib6_scratch"
fi

# ---- I-B7 ~/.mtoolsrc が無くても動く（現行では赤）
# この機能の目的の直接の観測。現行はドライブ p: を使い、その定義は
# ~/.mtoolsrc にしかないので、無ければ Drive 'P:' not supported で落ちる。
# 実装後は -i 方式になるので、~/.mtoolsrc は要らなくなる。
#
# S3（ドライブ文字を渡していない）の振る舞い側の対応物である。静的検査は
# 「そう書かれていない」ことしか見られないが、これは「実際に依存していない」
# ことを見る。
if h_want "I-B7"; then
    ib7_scratch=$(scratch_new)
    ib7_img="${ib7_scratch}/img/no-mtoolsrc.hda"
    make_image "$ib7_img" "11112222"
    seed_image "$ib7_img" "ONLY.TXT" "no mtoolsrc needed"
    write_rc "$ib7_scratch" "$ib7_img"
    # **write_mtoolsrc を呼ばない。** それがこのケースの要点。
    fake_mtools_install "${ib7_scratch}/bin" "${ib7_scratch}/mtools.log" 1
    run_98hdd "$ib7_scratch" ls
    assert_exit "I-B7 no-mtoolsrc exit" 0 "$h_last_status"
    assert_contains "I-B7 no-mtoolsrc lists" "ONLY" "$h_last_output"
    scratch_drop "$ib7_scratch"
fi

scratch_drop "$ib_scratch"
