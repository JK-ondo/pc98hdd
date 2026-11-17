#!/usr/bin/env bash
#
# I-A: 選んだカードを必ず告げる。I-C: card コマンド。
# run-cards.sh が source する。exit を書かない。
#
# ケース名は test-design.md §6.5・§6.6 の番号に対応する。

ia_setup() {            # ia_setup → 砂場を作り、偽 mtools（通す版）を置く
    local s
    s=$(scratch_new)
    mkdir -p "${s}/home/.98hdd/cards"
    fake_mtools_install "${s}/bin" "${s}/mtools.log" 1
    printf '%s\n' "$s"
}

# ========================= I-A: 選んだカードを告げる（仕様 §3.4.1）
# **後置 `-c` を無視することの安全弁なので、無視の決定と同じ強さで試験する。**
# `98hdd pull -c alpha` と打って `カード: bravo（自動判定）` が出れば、
# 利用者はその場で気づける。

if h_want "I-A1"; then
    s=$(ia_setup)
    make_image "${s}/img/alpha.hda" "11112222"
    seed_image "${s}/img/alpha.hda" "F.TXT" "x"
    write_card "$s" "alpha" "${s}/img/alpha.hda" "1111-2222"
    run_98hdd "$s" pull
    assert_exit "I-A1 auto-announced exit" 0 "$h_last_status"
    assert_contains "I-A1 auto-announced name" "alpha" "$h_last_output"
    assert_contains "I-A1 auto-announced how" "自動判定" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "I-A2"; then
    # 自動判定と明示指定で文面が異なること。同じ文面だと、無視された `-c` が
    # 「指定が効いた」ように見える。
    s=$(ia_setup)
    make_image "${s}/img/alpha.hda" "11112222"
    seed_image "${s}/img/alpha.hda" "F.TXT" "x"
    write_card "$s" "alpha" "${s}/img/alpha.hda" "1111-2222"
    run_98hdd "$s" -c alpha pull
    assert_exit "I-A2 explicit-announced exit" 0 "$h_last_status"
    assert_contains "I-A2 explicit-announced name" "alpha" "$h_last_output"
    assert_contains "I-A2 explicit-announced how" "-c" "$h_last_output"
    assert_lacks "I-A2 explicit-announced not-auto" "自動判定" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "I-A3"; then
    # 暗黙の default。黙って旧経路に落ちると、利用者は新機能が効いていると
    # 誤解する。
    s=$(scratch_new)
    make_image "${s}/img/legacy.hda" "11112222"
    seed_image "${s}/img/legacy.hda" "F.TXT" "x"
    write_rc "$s" "${s}/img/legacy.hda"
    fake_mtools_install "${s}/bin" "${s}/mtools.log" 1
    run_98hdd "$s" pull
    assert_exit "I-A3 implicit-announced exit" 0 "$h_last_status"
    assert_contains "I-A3 implicit-announced name" "default" "$h_last_output"
    assert_contains "I-A3 implicit-announced source" "98hddrc" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "I-A4"; then
    # 3 つのコマンドすべてで出ること。pull だけに付けると send の誤爆が
    # 見えない — 書き込む側こそ要る。
    s=$(ia_setup)
    make_image "${s}/img/alpha.hda" "11112222"
    seed_image "${s}/img/alpha.hda" "F.TXT" "x"
    write_card "$s" "alpha" "${s}/img/alpha.hda" "1111-2222"
    mkdir -p "${s}/home/9801/send"
    printf 'y' > "${s}/home/9801/send/Y.TXT"
    run_98hdd "$s" ls
    assert_contains "I-A4 announced-on-ls" "alpha" "$h_last_output"
    run_98hdd "$s" send "${s}/home/9801/send"
    assert_contains "I-A4 announced-on-send" "alpha" "$h_last_output"
    run_98hdd "$s" pull
    assert_contains "I-A4 announced-on-pull" "alpha" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "I-A5"; then
    # **告げるのは最初の mtools 呼び出しより前。** 後に出ると、利用者が
    # 気づくのは書き終わったあとになる。偽 mtools が呼ばれた瞬間に目印を
    # 標準出力へ混ぜ、告知の行がその前に在ることを見る。
    s=$(scratch_new)
    mkdir -p "${s}/home/.98hdd/cards" "${s}/bin"
    make_image "${s}/img/alpha.hda" "11112222"
    seed_image "${s}/img/alpha.hda" "F.TXT" "x"
    write_card "$s" "alpha" "${s}/img/alpha.hda" "1111-2222"
    # 呼ばれたら MTOOLSMARK を標準出力へ出してから本物へ渡す偽 mtools。
    for n in mdir mcopy mmd; do
        case "$n" in
            mdir)  real="$HARNESS_REAL_MDIR" ;;
            mcopy) real="$HARNESS_REAL_MCOPY" ;;
            mmd)   real="$HARNESS_REAL_MMD" ;;
        esac
        {
            printf '#!/usr/bin/env bash\n'
            printf 'printf "MTOOLSMARK\\n"\n'
            printf 'exec %s "$@"\n' "$real"
        } > "${s}/bin/${n}"
        chmod +x "${s}/bin/${n}"
    done
    run_98hdd "$s" pull
    assert_exit "I-A5 announce-before exit" 0 "$h_last_status"
    ann=$(printf '%s\n' "$h_last_output" | LC_ALL=C grep -n 'alpha' \
          | /usr/bin/head -1 | /usr/bin/cut -d: -f1)
    mark=$(printf '%s\n' "$h_last_output" | LC_ALL=C grep -n 'MTOOLSMARK' \
          | /usr/bin/head -1 | /usr/bin/cut -d: -f1)
    if [ -n "$ann" ] && [ -n "$mark" ] && [ "$ann" -lt "$mark" ]; then
        h_pass "I-A5 announce-before-first-mtools"
    else
        h_fail "I-A5 announce-before-first-mtools" \
            "告知の行 ${ann:-無し} / 最初の mtools ${mark:-無し}" \
            "告知が後に出ると、気づくのは書き終わったあとになる"
    fi
    scratch_drop "$s"
fi

# ================================================ I-C: card コマンド

if h_want "I-C1"; then
    s=$(ia_setup)
    make_image "${s}/img/alpha.hda" "11112222"
    write_card "$s" "alpha" "${s}/img/alpha.hda" "1111-2222"
    write_card "$s" "bravo" "${s}/img/absent-b.hda" "3333-4444"
    run_98hdd "$s" card list
    assert_exit "I-C1 card-list exit" 0 "$h_last_status"
    assert_contains "I-C1 card-list names-alpha" "alpha" "$h_last_output"
    assert_contains "I-C1 card-list names-bravo" "bravo" "$h_last_output"
    assert_contains "I-C1 card-list shows-pull-dir" "hdd/alpha" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "I-C2"; then
    # 読み取りのみ。一覧を見ただけでイメージが変わってはいけない。
    # 「不変」だけでは空振りするので、一覧が実際に出たことを対にする。
    s=$(ia_setup)
    make_image "${s}/img/alpha.hda" "11112222"
    cp "${s}/img/alpha.hda" "${s}/img/alpha.before"
    write_card "$s" "alpha" "${s}/img/alpha.hda" "1111-2222"
    run_98hdd "$s" card list
    assert_exit "I-C2 card-list-readonly exit" 0 "$h_last_status"
    assert_contains "I-C2 card-list-readonly listed" "alpha" "$h_last_output"
    assert_same_bytes "I-C2 card-list-readonly bytes" \
        "${s}/img/alpha.before" "${s}/img/alpha.hda"
    scratch_drop "$s"
fi

if h_want "I-C3"; then
    # 実測シリアルが出ること。定義を書き足すのに要る値がそのまま手に入る
    # （カードを作るコマンドを設けない代わりの仕掛け。仕様 §3.7）。
    s=$(ia_setup)
    make_image "${s}/img/alpha.hda" "deadbeef"
    write_card "$s" "alpha" "${s}/img/alpha.hda" "DEAD-BEEF"
    run_98hdd "$s" card show alpha
    assert_exit "I-C3 card-show exit" 0 "$h_last_status"
    assert_contains "I-C3 card-show measured-serial" "DEAD-BEEF" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "I-C4"; then
    s=$(scratch_new)
    make_image "${s}/img/legacy.hda" "11112222"
    write_rc "$s" "${s}/img/legacy.hda"
    fake_mtools_install "${s}/bin" "${s}/mtools.log" 1
    run_98hdd "$s" card list
    assert_exit "I-C4 card-list-implicit exit" 0 "$h_last_status"
    assert_contains "I-C4 card-list-implicit default" "default" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "I-C5"; then
    # **card list は選択経路を通さない。** 通すと、2 枚マウント中に一覧が
    # 見られなくなる。一覧は「何が定義されているか」を見るもので、
    # 「どれを使うか」とは別の問いである。
    s=$(ia_setup)
    make_image "${s}/img/alpha.hda" "11112222"
    make_image "${s}/img/bravo.hda" "33334444"
    write_card "$s" "alpha" "${s}/img/alpha.hda" "1111-2222"
    write_card "$s" "bravo" "${s}/img/bravo.hda" "3333-4444"
    run_98hdd "$s" card list
    assert_exit "I-C5 card-list-no-select exit" 0 "$h_last_status"
    assert_contains "I-C5 card-list-no-select names-alpha" "alpha" "$h_last_output"
    assert_contains "I-C5 card-list-no-select names-bravo" "bravo" "$h_last_output"
    scratch_drop "$s"
fi
