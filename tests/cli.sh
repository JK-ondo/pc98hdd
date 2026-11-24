#!/usr/bin/env bash
#
# A: 入口（引数解釈と終了コード）。run-cards.sh が source する。exit を書かない。
#
# ケース名は test-design.md §7 の番号に対応する。

a_setup() {             # a_setup → 砂場を作り、カード 1 枚と偽 mtools を置く
    local s
    s=$(scratch_new)
    mkdir -p "${s}/home/.98hdd/cards"
    make_image "${s}/img/alpha.hda" "11112222"
    seed_image "${s}/img/alpha.hda" "F.TXT" "x"
    write_card "$s" "alpha" "${s}/img/alpha.hda" "1111-2222"
    fake_mtools_install "${s}/bin" "${s}/mtools.log" 1
    printf '%s\n' "$s"
}

if h_want "A1"; then
    s=$(a_setup)
    run_98hdd "$s" -c alpha pull
    assert_exit "A1 opt-before-subcmd exit" 0 "$h_last_status"
    assert_file "A1 opt-before-subcmd pulled" "${s}/home/9801/hdd/alpha/f.txt"
    scratch_drop "$s"
fi

if h_want "A2"; then
    # 後置 `-c` は無視する（2026-11-16 ユーザー決定）。挿さっているのは
    # bravo なので、自動判定で bravo が選ばれる。
    # **安全弁は「カード: bravo（自動判定）」が出ること。** 無視そのものより
    # こちらが要件である。
    s=$(scratch_new)
    mkdir -p "${s}/home/.98hdd/cards"
    make_image "${s}/img/bravo.hda" "33334444"
    seed_image "${s}/img/bravo.hda" "F.TXT" "x"
    write_card "$s" "alpha" "${s}/img/absent-a.hda" "1111-2222"
    write_card "$s" "bravo" "${s}/img/bravo.hda" "3333-4444"
    fake_mtools_install "${s}/bin" "${s}/mtools.log" 1
    run_98hdd "$s" pull -c alpha
    assert_exit "A2 opt-after-subcmd exit" 0 "$h_last_status"
    assert_contains "A2 opt-after-subcmd announced-bravo" "bravo" "$h_last_output"
    assert_contains "A2 opt-after-subcmd announced-auto" "自動判定" "$h_last_output"
    assert_file "A2 opt-after-subcmd pulled-bravo" \
        "${s}/home/9801/hdd/bravo/f.txt"
    assert_nofile "A2 opt-after-subcmd not-alpha" "${s}/home/9801/hdd/alpha"
    scratch_drop "$s"
fi

if h_want "A2b"; then
    # send の 3・4 引数目に落ちて無視される（cmd_send は $1・$2 のみ参照）。
    s=$(a_setup)
    mkdir -p "${s}/home/9801/send"
    printf 'z' > "${s}/home/9801/send/Z.TXT"
    run_98hdd "$s" send "${s}/home/9801/send" / -c bravo
    assert_exit "A2b send-trailing-opt exit" 0 "$h_last_status"
    listing=$(MTOOLS_SKIP_CHECK=1 "$HARNESS_REAL_MDIR" -s -b \
              -i "${s}/img/alpha.hda@@${HARNESS_OFFSET}" :: 2>/dev/null || true)
    assert_contains "A2b send-trailing-opt landed" "Z.TXT" "$listing"
    scratch_drop "$s"
fi

if h_want "A2c"; then
    s=$(a_setup)
    run_98hdd "$s" ls / -c bravo
    assert_exit "A2c ls-trailing-opt exit" 0 "$h_last_status"
    assert_contains "A2c ls-trailing-opt listed" "F" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "A2d"; then
    # 送信元の位置に `-c` を書いた場合。黙って通さない。
    s=$(a_setup)
    run_98hdd "$s" send -c alpha
    assert_exit "A2d send-opt-as-src exit" 1 "$h_last_status"
    assert_contains "A2d send-opt-as-src complains" "-c" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "A3"; then
    s=$(a_setup)
    run_98hdd "$s" -c
    assert_exit "A3 opt-without-value exit" 1 "$h_last_status"
    # 「-c」の字面は「不明なコマンド: -c」でも満たされる。-c が解釈された
    # うえで値が無いことを述べているか、文面で判別する。
    assert_lacks "A3 opt-without-value parsed" "不明なコマンド" "$h_last_output"
    assert_contains "A3 opt-without-value names-card" "カード" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "A4"; then
    # card に副コマンドを付けない場合。list と同じか使い方を出すか、
    # どちらでもよいが終了コードが一貫していること。
    s=$(a_setup)
    run_98hdd "$s" card
    if [ "$h_last_status" = "0" ] || [ "$h_last_status" = "1" ]; then
        h_pass "A4 card-bare exit-in-vocabulary"
    else
        h_fail "A4 card-bare exit-in-vocabulary" \
            "終了コードが {0,1} の外: $h_last_status"
    fi
    # 「card」の字面は「不明なコマンド: card」でも満たされる。
    assert_lacks "A4 card-bare parsed" "不明なコマンド" "$h_last_output"
    assert_contains "A4 card-bare lists-or-helps" "alpha" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "A5"; then
    # **fd はカードと無関係**（仕様 §2 非目標）。ここで選択を走らせると、
    # カードが挿さっていないだけで fd が使えなくなる。現行の利用者が困る経路。
    s=$(scratch_new)
    mkdir -p "${s}/home/.98hdd/cards"
    write_card "$s" "alpha" "${s}/img/absent-a.hda" "1111-2222"
    write_card "$s" "bravo" "${s}/img/absent-b.hda" "3333-4444"
    fake_mtools_install "${s}/bin" "${s}/mtools.log" 1
    # FDI ではないファイルを渡す。カードは 2 枚とも不在なので、選択を
    # 走らせていれば E4（使えるカードが見つかりません）で落ちる。
    #
    # **どのエラーが先に出るかは細部なので表明しない。** 見るのは
    # 「渡したイメージについて述べている」ことと「カードについて述べて
    # いない」こと。前者が無いと、別の理由で落ちたのを見逃す。
    printf 'not an fdi' > "${s}/img/bogus.fdi"
    run_98hdd "$s" fd "${s}/img/bogus.fdi"
    assert_exit "A5 fd-ignores-cards exit" 1 "$h_last_status"
    assert_contains "A5 fd-ignores-cards blames-image" "bogus.fdi" "$h_last_output"
    assert_lacks "A5 fd-ignores-cards not-card-error" "カード" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "A6"; then
    s=$(a_setup)
    run_98hdd "$s" help
    assert_exit "A6 help exit" 0 "$h_last_status"
    assert_contains "A6 help mentions-c" "-c" "$h_last_output"
    assert_contains "A6 help mentions-card" "card" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "A7"; then
    s=$(a_setup)
    run_98hdd "$s" no-such-command
    assert_exit "A7 unknown-command exit" 1 "$h_last_status"
    assert_contains "A7 unknown-command says-so" "不明なコマンド" "$h_last_output"
    scratch_drop "$s"
fi
