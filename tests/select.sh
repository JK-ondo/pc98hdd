#!/usr/bin/env bash
#
# I-S: 選択と同定。run-cards.sh が source する。exit を書かない。
#
# **どのケースも「何も起きなかった」だけを見ない。** 実装が無い状態では
# check_image が先に落ちて何も起きないので、不変性の表明は偶然満たされる。
# 必ず「正しいエラーを出した」ことを対にする。
#
# ケース名は test-design.md §6.2 の番号に対応する。

is_setup() {            # is_setup → 砂場を作り、偽 mtools（通す版）を置く
    local s
    s=$(scratch_new)
    mkdir -p "${s}/home/.98hdd/cards"
    fake_mtools_install "${s}/bin" "${s}/mtools.log" 1
    printf '%s\n' "$s"
}

if h_want "I-S1"; then
    # 別々のイメージが 2 つマウントされ、どちらも自分の宣言と一致 → E3。
    # 「最初の 1 枚」を選ぶと意図しないカードへ書く道が開く。
    s=$(is_setup)
    make_image "${s}/img/alpha.hda" "11112222"
    make_image "${s}/img/bravo.hda" "33334444"
    write_card "$s" "alpha" "${s}/img/alpha.hda" "1111-2222"
    write_card "$s" "bravo" "${s}/img/bravo.hda" "3333-4444"
    fake_mtools_install "${s}/bin" "${s}/mtools.log" 0
    run_98hdd "$s" ls
    assert_exit "I-S1 ambiguous exit" 1 "$h_last_status"
    assert_contains "I-S1 ambiguous names-alpha" "alpha" "$h_last_output"
    assert_contains "I-S1 ambiguous names-bravo" "bravo" "$h_last_output"
    assert_eq "I-S1 ambiguous no-mtools" "0" "$(mtools_log_lines "${s}/mtools.log")"
    scratch_drop "$s"
fi

if h_want "I-S2"; then
    # 同じパスを指す 2 定義。1 枚ずつ挿す運用なので、シリアルで必ず絞れる。
    # BlueSCSI のカードを同じ手順で作ると、これが最も普通の構成になる。
    s=$(is_setup)
    shared="${s}/img/shared.hda"
    make_image "$shared" "33334444"
    seed_image "$shared" "ONBRAVO.TXT" "bravo content"
    write_card "$s" "alpha" "$shared" "1111-2222"
    write_card "$s" "bravo" "$shared" "3333-4444"
    run_98hdd "$s" ls
    assert_exit "I-S2 same-path exit" 0 "$h_last_status"
    assert_contains "I-S2 same-path chose-bravo" "bravo" "$h_last_output"
    assert_contains "I-S2 same-path listed" "ONBRAVO" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "I-S3" || h_want "I-S4"; then
    # -c bravo を指定したが、挿さっているのは alpha。
    s=$(is_setup)
    make_image "${s}/img/shared.hda" "11112222"
    write_card "$s" "alpha" "${s}/img/shared.hda" "1111-2222"
    write_card "$s" "bravo" "${s}/img/shared.hda" "3333-4444" "${s}/home/mirror-bravo"
    fake_mtools_install "${s}/bin" "${s}/mtools.log" 0
    run_98hdd "$s" -c bravo pull
    if h_want "I-S3"; then
        assert_exit "I-S3 wrong-card exit" 1 "$h_last_status"
        # 終了 1・行き先が無い・mtools が呼ばれていない、の 3 つは
        # 「-c が不明なコマンド」でも満たされる。E2 で落ちたことを対にする。
        assert_lacks "I-S3 wrong-card parsed" "不明なコマンド" "$h_last_output"
        assert_nofile "I-S3 wrong-card no-dest" "${s}/home/mirror-bravo"
        assert_eq "I-S3 wrong-card no-mtools" "0" \
            "$(mtools_log_lines "${s}/mtools.log")"
    fi
    if h_want "I-S4"; then
        # 利用者が知りたいのは「違う」ことではなく「では何が挿さっているのか」。
        # それはこの道具が知っている。
        assert_contains "I-S4 e2-names-card" "bravo" "$h_last_output"
        assert_contains "I-S4 e2-expected" "3333-4444" "$h_last_output"
        assert_contains "I-S4 e2-actual" "1111-2222" "$h_last_output"
        assert_contains "I-S4 e2-which-matches" "alpha" "$h_last_output"
    fi
    scratch_drop "$s"
fi

if h_want "I-S5"; then
    s=$(is_setup)
    write_card "$s" "alpha" "${s}/img/absent-a.hda" "1111-2222"
    write_card "$s" "bravo" "${s}/img/absent-b.hda" "3333-4444"
    run_98hdd "$s" pull
    assert_exit "I-S5 none-mounted exit" 1 "$h_last_status"
    assert_contains "I-S5 none-mounted lists-alpha" "alpha" "$h_last_output"
    assert_contains "I-S5 none-mounted lists-bravo" "bravo" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "I-S6"; then
    # 挿さってはいるがどの定義とも一致しない。読めたシリアルが出れば
    # そのまま .conf に貼れる。「何も挿さっていない」とは別の事態である。
    s=$(is_setup)
    make_image "${s}/img/unknown.hda" "deadbeef"
    write_card "$s" "alpha" "${s}/img/unknown.hda" "1111-2222"
    run_98hdd "$s" pull
    assert_exit "I-S6 unknown-serial exit" 1 "$h_last_status"
    assert_contains "I-S6 unknown-serial shows-found" "DEAD-BEEF" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "I-S7"; then
    s=$(is_setup)
    make_bad_sig "${s}/img/nosig.hda"
    write_card "$s" "alpha" "${s}/img/nosig.hda" "1111-2222"
    run_98hdd "$s" ls
    assert_exit "I-S7 bad-signature exit" 1 "$h_last_status"
    assert_contains "I-S7 bad-signature blames-offset" "オフセット" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "I-S8"; then
    # PC-98 の DOS は FAT32 を扱えない。署名を見る規則が副産物として弾く。
    s=$(is_setup)
    make_fat32 "${s}/img/f32.hda" "55556666"
    write_card "$s" "alpha" "${s}/img/f32.hda" "5555-6666"
    run_98hdd "$s" ls
    assert_exit "I-S8 fat32 exit" 1 "$h_last_status"
    # 終了 1 だけでは、イメージが見つからないなど別の理由でも満たされる。
    # 署名を見た結果として落ちたことを述べていること。
    assert_contains "I-S8 fat32 blames-offset" "オフセット" "$h_last_output"
    scratch_drop "$s"
fi
