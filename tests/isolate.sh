#!/usr/bin/env bash
#
# I-X: 隔離（この機能の目的）と I-N: 照合前に何もしない。
# run-cards.sh が source する。exit を書かない。
#
# ケース名は test-design.md §6.3・§6.4 の番号に対応する。

ix_setup() {            # ix_setup → 砂場を作り、偽 mtools（通す版）を置く
    local s
    s=$(scratch_new)
    mkdir -p "${s}/home/.98hdd/cards"
    fake_mtools_install "${s}/bin" "${s}/mtools.log" 1
    printf '%s\n' "$s"
}

# ============================================== I-X: 隔離（機能の目的）

if h_want "I-X1"; then
    # **この機能の受け入れ条件。** 他のどの表明が通っても、これが落ちたら
    # 機能は目的を果たしていない。
    #
    # 「bravo が不変」だけでは空振りする（実装が無ければ何も起きない）。
    # 「alpha が実際に変わった」ことを対にして初めて意味を持つ。
    s=$(ix_setup)
    make_image "${s}/img/alpha.hda" "11112222"
    make_image "${s}/img/bravo.hda" "33334444"
    write_card "$s" "alpha" "${s}/img/alpha.hda" "1111-2222"
    write_card "$s" "bravo" "${s}/img/bravo.hda" "3333-4444"
    cp "${s}/img/alpha.hda" "${s}/img/alpha.before"
    cp "${s}/img/bravo.hda" "${s}/img/bravo.before"
    mkdir -p "${s}/home/9801/send"
    printf 'to alpha' > "${s}/home/9801/send/TOALPHA.TXT"
    run_98hdd "$s" -c alpha send "${s}/home/9801/send"
    assert_exit "I-X1 isolation exit" 0 "$h_last_status"
    a_now=$(shasum -a 256 "${s}/img/alpha.hda" | /usr/bin/awk '{print $1}')
    a_was=$(shasum -a 256 "${s}/img/alpha.before" | /usr/bin/awk '{print $1}')
    if [ "$a_now" != "$a_was" ]; then
        h_pass "I-X1 isolation target-changed"
    else
        h_fail "I-X1 isolation target-changed" \
            "送信先のイメージが変わっていない（送信そのものが起きていない）" \
            "この表明が無いと「他方が不変」は偶然満たされる"
    fi
    assert_same_bytes "I-X1 isolation other-untouched" \
        "${s}/img/bravo.before" "${s}/img/bravo.hda"
    scratch_drop "$s"
fi

if h_want "I-X2"; then
    s=$(ix_setup)
    make_image "${s}/img/alpha.hda" "11112222"
    make_image "${s}/img/bravo.hda" "33334444"
    seed_image "${s}/img/alpha.hda" "FROMA.TXT" "from alpha"
    seed_image "${s}/img/bravo.hda" "FROMB.TXT" "from bravo"
    write_card "$s" "alpha" "${s}/img/alpha.hda" "1111-2222"
    write_card "$s" "bravo" "${s}/img/bravo.hda" "3333-4444"
    run_98hdd "$s" -c alpha pull
    assert_exit "I-X2 separate-mirrors alpha-exit" 0 "$h_last_status"
    run_98hdd "$s" -c bravo pull
    assert_exit "I-X2 separate-mirrors bravo-exit" 0 "$h_last_status"
    assert_file "I-X2 separate-mirrors alpha-has-own" \
        "${s}/home/9801/hdd/alpha/froma.txt"
    assert_file "I-X2 separate-mirrors bravo-has-own" \
        "${s}/home/9801/hdd/bravo/fromb.txt"
    assert_nofile "I-X2 separate-mirrors no-crosstalk-a" \
        "${s}/home/9801/hdd/alpha/fromb.txt"
    assert_nofile "I-X2 separate-mirrors no-crosstalk-b" \
        "${s}/home/9801/hdd/bravo/froma.txt"
    scratch_drop "$s"
fi

if h_want "I-X3"; then
    # alpha → bravo → alpha。3 回目で「変更なし」が出れば、bravo の pull が
    # alpha のミラーを汚していない。
    s=$(ix_setup)
    make_image "${s}/img/alpha.hda" "11112222"
    make_image "${s}/img/bravo.hda" "33334444"
    seed_image "${s}/img/alpha.hda" "FROMA.TXT" "from alpha"
    seed_image "${s}/img/bravo.hda" "FROMB.TXT" "from bravo"
    write_card "$s" "alpha" "${s}/img/alpha.hda" "1111-2222"
    write_card "$s" "bravo" "${s}/img/bravo.hda" "3333-4444"
    run_98hdd "$s" -c alpha pull
    run_98hdd "$s" -c bravo pull
    run_98hdd "$s" -c alpha pull
    assert_exit "I-X3 revisit exit" 0 "$h_last_status"
    assert_contains "I-X3 revisit unchanged" "変更なし 1" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "I-X4"; then
    s=$(ix_setup)
    make_image "${s}/img/alpha.hda" "11112222"
    mkdir -p "${s}/home/mirror"
    write_card "$s" "alpha" "${s}/img/alpha.hda" "1111-2222" "${s}/home/mirror"
    write_card "$s" "bravo" "${s}/img/bravo.hda" "3333-4444" "${s}/home/mirror"
    fake_mtools_install "${s}/bin" "${s}/mtools.log" 0
    run_98hdd "$s" pull
    assert_exit "I-X4 pull-dir-clash exit" 1 "$h_last_status"
    assert_contains "I-X4 pull-dir-clash names-alpha" "alpha" "$h_last_output"
    assert_eq "I-X4 pull-dir-clash no-mtools" "0" \
        "$(mtools_log_lines "${s}/mtools.log")"
    scratch_drop "$s"
fi

if h_want "I-X5"; then
    # 1 回の起動が触るイメージが 1 つだけであること（仕様 I3）。
    s=$(ix_setup)
    make_image "${s}/img/alpha.hda" "11112222"
    make_image "${s}/img/bravo.hda" "33334444"
    seed_image "${s}/img/alpha.hda" "FROMA.TXT" "from alpha"
    write_card "$s" "alpha" "${s}/img/alpha.hda" "1111-2222"
    write_card "$s" "bravo" "${s}/img/bravo.hda" "3333-4444"
    : > "${s}/mtools.log"
    run_98hdd "$s" -c alpha pull
    assert_exit "I-X5 single-image exit" 0 "$h_last_status"
    got=$(mtools_log_specs "${s}/mtools.log" | /usr/bin/wc -l | /usr/bin/tr -d ' ')
    assert_eq "I-X5 single-image spec-count" "1" "$got"
    assert_contains "I-X5 single-image spec-is-alpha" "alpha.hda" \
        "$(mtools_log_specs "${s}/mtools.log")"
    scratch_drop "$s"
fi

# ================================= I-N: 照合前に何もしない（仕様 I1・I2）
# 偽 mtools を遮る版にして、エラー経路で mtools が 1 度も呼ばれないことを見る。
#
# **「ログが空」だけでは空振りする。** 実装が無ければ check_image が先に落ちて
# やはりログは空になる。**正しいエラーを出したこと**を必ず対にする。

in_setup() {            # in_setup → 砂場を作り、偽 mtools（遮る版）を置く
    local s
    s=$(scratch_new)
    mkdir -p "${s}/home/.98hdd/cards"
    fake_mtools_install "${s}/bin" "${s}/mtools.log" 0
    printf '%s\n' "$s"
}

if h_want "I-N1"; then
    # E1: IMAGE が存在しない。
    s=$(in_setup)
    write_card "$s" "alpha" "${s}/img/absent.hda" "1111-2222" "${s}/home/mirror-a"
    run_98hdd "$s" -c alpha pull
    assert_exit "I-N1 e1 exit" 1 "$h_last_status"
    assert_contains "I-N1 e1 names-path" "absent.hda" "$h_last_output"
    assert_eq "I-N1 e1 no-mtools" "0" "$(mtools_log_lines "${s}/mtools.log")"
    assert_nofile "I-N1 e1 no-dest" "${s}/home/mirror-a"
    scratch_drop "$s"
fi

if h_want "I-N2"; then
    # E2: シリアル不一致。**読み取りすら許さない** — この時点で「どのイメージを
    # 読んだのか」が不確かなので、出力を信じてよい根拠が無い（仕様 I1）。
    s=$(in_setup)
    make_image "${s}/img/x.hda" "11112222"
    write_card "$s" "alpha" "${s}/img/x.hda" "9999-9999" "${s}/home/mirror-a"
    run_98hdd "$s" -c alpha pull
    assert_exit "I-N2 e2 exit" 1 "$h_last_status"
    assert_contains "I-N2 e2 shows-actual" "1111-2222" "$h_last_output"
    assert_eq "I-N2 e2 no-mtools" "0" "$(mtools_log_lines "${s}/mtools.log")"
    assert_nofile "I-N2 e2 no-dest" "${s}/home/mirror-a"
    scratch_drop "$s"
fi

if h_want "I-N3"; then
    # E3: 曖昧。send なので、どのイメージもバイト不変であることまで見る。
    s=$(in_setup)
    make_image "${s}/img/alpha.hda" "11112222"
    make_image "${s}/img/bravo.hda" "33334444"
    cp "${s}/img/alpha.hda" "${s}/img/alpha.before"
    cp "${s}/img/bravo.hda" "${s}/img/bravo.before"
    write_card "$s" "alpha" "${s}/img/alpha.hda" "1111-2222"
    write_card "$s" "bravo" "${s}/img/bravo.hda" "3333-4444"
    mkdir -p "${s}/home/9801/send"
    printf 'x' > "${s}/home/9801/send/X.TXT"
    run_98hdd "$s" send "${s}/home/9801/send"
    assert_exit "I-N3 e3 exit" 1 "$h_last_status"
    assert_contains "I-N3 e3 names-alpha" "alpha" "$h_last_output"
    assert_eq "I-N3 e3 no-mtools" "0" "$(mtools_log_lines "${s}/mtools.log")"
    assert_same_bytes "I-N3 e3 alpha-untouched" \
        "${s}/img/alpha.before" "${s}/img/alpha.hda"
    assert_same_bytes "I-N3 e3 bravo-untouched" \
        "${s}/img/bravo.before" "${s}/img/bravo.hda"
    scratch_drop "$s"
fi

if h_want "I-N4"; then
    # E5: PULL_DIR 衝突。
    s=$(in_setup)
    make_image "${s}/img/alpha.hda" "11112222"
    mkdir -p "${s}/home/mirror"
    write_card "$s" "alpha" "${s}/img/alpha.hda" "1111-2222" "${s}/home/mirror"
    write_card "$s" "bravo" "${s}/img/bravo.hda" "3333-4444" "${s}/home/mirror"
    run_98hdd "$s" pull
    assert_exit "I-N4 e5 exit" 1 "$h_last_status"
    assert_contains "I-N4 e5 names-dir" "mirror" "$h_last_output"
    assert_eq "I-N4 e5 no-mtools" "0" "$(mtools_log_lines "${s}/mtools.log")"
    scratch_drop "$s"
fi

if h_want "I-N5"; then
    # E9: 署名不正。send なのでイメージ不変も見る。
    s=$(in_setup)
    make_bad_sig "${s}/img/nosig.hda"
    cp "${s}/img/nosig.hda" "${s}/img/nosig.before"
    write_card "$s" "alpha" "${s}/img/nosig.hda" "1111-2222"
    mkdir -p "${s}/home/9801/send"
    printf 'x' > "${s}/home/9801/send/X.TXT"
    run_98hdd "$s" -c alpha send "${s}/home/9801/send"
    assert_exit "I-N5 e9 exit" 1 "$h_last_status"
    assert_contains "I-N5 e9 blames-offset" "オフセット" "$h_last_output"
    assert_eq "I-N5 e9 no-mtools" "0" "$(mtools_log_lines "${s}/mtools.log")"
    assert_same_bytes "I-N5 e9 image-untouched" \
        "${s}/img/nosig.before" "${s}/img/nosig.hda"
    scratch_drop "$s"
fi
