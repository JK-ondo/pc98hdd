#!/usr/bin/env bash
#
# 単体ケース。run-cards.sh が source する（ハーネスは既に読み込まれている）。
# exit を書かない — ランナーごと終わって残りが走らなくなる。
#
# 被試験対象を source して関数を直接呼ぶ（sut_eval）。実装前は
# 「command not found」で終了 127 になるので、全部赤になるのが正しい状態。
#
# ケース名は test-design.md §5 の番号に対応する。

u_scratch=$(scratch_new)

# =================================================== U-S: シリアル読み取り
# 仕様 §3.3。read_serial <img> <off> が XXXX-XXXX を返し、署名が 0x29 で
# なければ失敗する。
us_a="${u_scratch}/img/us-a.hda"      # 11112222
us_b="${u_scratch}/img/us-b.hda"      # 0c010a1f（実機と同じ値）
us_c="${u_scratch}/img/us-c.hda"      # deadbeef
us_sig="${u_scratch}/img/us-badsig.hda"
us_32="${u_scratch}/img/us-fat32.hda"
us_tr="${u_scratch}/img/us-trunc.hda"
make_image "$us_a" "11112222"
make_image "$us_b" "0c010a1f"
make_image "$us_c" "deadbeef"
make_bad_sig "$us_sig"
make_fat32 "$us_32" "55556666"
make_truncated "$us_tr"

if h_want "U-S1"; then
    sut_eval "$u_scratch" "read_serial '$us_a' $HARNESS_OFFSET"
    assert_exit "U-S1 serial-basic exit" 0 "$h_last_status"
    assert_eq "U-S1 serial-basic value" "1111-2222" "$h_last_output"
fi

if h_want "U-S2"; then
    # 実機の値。小文字で作っても大文字で返すこと（mdir の表記に合わせる）。
    sut_eval "$u_scratch" "read_serial '$us_b' $HARNESS_OFFSET"
    assert_exit "U-S2 serial-real exit" 0 "$h_last_status"
    assert_eq "U-S2 serial-real value" "0C01-0A1F" "$h_last_output"
fi

if h_want "U-S3"; then
    # 4 バイトすべてが相異なるので、逆順・部分逆順・上下ワード入れ替えの
    # どれもが別の文字列になる。11112222 のような対称に近い値では、
    # バイト順を間違えても気づけない組み合わせがある。
    sut_eval "$u_scratch" "read_serial '$us_c' $HARNESS_OFFSET"
    assert_exit "U-S3 serial-byteorder exit" 0 "$h_last_status"
    assert_eq "U-S3 serial-byteorder value" "DEAD-BEEF" "$h_last_output"
fi

if h_want "U-S4"; then
    # 失敗したときに「何も返さない」こと。"-" の不在で見ると弱い（エラー文に
    # たまたまハイフンが無いだけで通る）ので、空であることを直に見る。
    sut_eval "$u_scratch" "read_serial '$us_sig' $HARNESS_OFFSET 2>/dev/null"
    assert_exit "U-S4 bad-signature exit" 1 "$h_last_status"
    assert_eq "U-S4 bad-signature silent" "" "$h_last_output"
fi

if h_want "U-S5"; then
    # FAT32 は署名を 0x42、シリアルを 0x43 に置く。0x26 は FATSz32 の上位
    # ワードで通常 0x00。PC-98 の DOS は FAT32 を扱えないので弾くのが正しい。
    sut_eval "$u_scratch" "read_serial '$us_32' $HARNESS_OFFSET"
    assert_exit "U-S5 fat32-rejected" 1 "$h_last_status"
fi

if h_want "U-S6"; then
    # 誤ったオフセットが黙って通らないこと。FAT の先頭を指していなければ
    # この位置は別のデータで、たまたま 0x29 が立っていれば無関係なバイトを
    # シリアルとして扱ってしまう。
    sut_eval "$u_scratch" "read_serial '$us_a' $(( HARNESS_OFFSET + 512 ))"
    assert_exit "U-S6 wrong-offset-rejected" 1 "$h_last_status"
fi

if h_want "U-S7"; then
    sut_eval "$u_scratch" "read_serial '$us_tr' $HARNESS_OFFSET"
    assert_exit "U-S7 truncated-rejected" 1 "$h_last_status"
fi

if h_want "U-S8"; then
    sut_eval "$u_scratch" \
        "read_serial '$us_a' $HARNESS_OFFSET; read_serial '$us_a' $HARNESS_OFFSET"
    assert_eq "U-S8 serial-stable" "1111-2222
1111-2222" "$h_last_output"
fi

if h_want "U-S9"; then
    # 同定は読み取り専用であること。書き込みが混じると、照合しただけで
    # イメージが変わる経路ができる。
    #
    # **バイト不変だけでは空振りする。** 実装が無ければ何も起きないので
    # 当然バイトは変わらない。読み取りが実際に成功したことも同時に見る。
    before="${u_scratch}/img/us-a.before"
    cp "$us_a" "$before"
    sut_eval "$u_scratch" "read_serial '$us_a' $HARNESS_OFFSET"
    assert_exit "U-S9 read-only did-read" 0 "$h_last_status"
    assert_eq "U-S9 read-only value" "1111-2222" "$h_last_output"
    assert_same_bytes "U-S9 read-only bytes" "$before" "$us_a"
fi

# ================================================= U-N: カード名の検証
# 仕様 §3.1。1 文字目は [A-Za-z0-9]、2 文字目以降は [A-Za-z0-9_-]。
if h_want "U-N1"; then
    sut_eval "$u_scratch" '
        for n in main games card-2 a_b A1 x; do
            if valid_card_name "$n"; then printf "%s=ok\n" "$n"
            else printf "%s=NG\n" "$n"; fi
        done'
    assert_eq "U-N1 names-accepted" "main=ok
games=ok
card-2=ok
a_b=ok
A1=ok
x=ok" "$h_last_output"
fi

if h_want "U-N2"; then
    # 空白・非 ASCII・パス区切り・ドット・先頭ハイフン・空文字。
    # 先頭ハイフンを拒むのは、名前が選択肢に化けるのを防ぐため
    # （98hdd -c -x pull のような呼び方を生まない）。
    sut_eval "$u_scratch" '
        for n in "my card" "カード" "a/b" "a.b" "-x" ""; do
            if valid_card_name "$n"; then printf "[%s]=ACCEPTED\n" "$n"
            else printf "[%s]=rejected\n" "$n"; fi
        done'
    assert_eq "U-N2 names-rejected" "[my card]=rejected
[カード]=rejected
[a/b]=rejected
[a.b]=rejected
[-x]=rejected
[]=rejected" "$h_last_output"
fi

if h_want "U-N3"; then
    # パス脱出の芽。.. が通ると PULL_DIR の既定値が砂場の外を指しうる。
    sut_eval "$u_scratch" '
        if valid_card_name ".."; then echo ACCEPTED; else echo rejected; fi'
    assert_eq "U-N3 dotdot-rejected" "rejected" "$h_last_output"
fi

# ============================================ U-L: 設定の読み込み
# 仕様 §3.1・§3.1.1・§3.2。load_cards が 1 枚 1 行で
# name|image|offset|serial|pull_dir を出す。
ul_setup() {            # ul_setup → 新しい砂場を作って返す
    local s
    s=$(scratch_new)
    mkdir -p "${s}/home/.98hdd/cards"
    printf '%s\n' "$s"
}

if h_want "U-L1"; then
    s=$(ul_setup)
    make_image "${s}/img/a.hda" "11112222"
    write_card "$s" "main" "${s}/img/a.hda" "1111-2222" "${s}/home/mirror-main"
    sut_eval "$s" "load_cards"
    assert_exit "U-L1 load-ok exit" 0 "$h_last_status"
    assert_eq "U-L1 load-ok line" \
        "main|${s}/img/a.hda|${HARNESS_OFFSET}|1111-2222|${s}/home/mirror-main" \
        "$h_last_output"
    scratch_drop "$s"
fi

if h_want "U-L2"; then
    s=$(ul_setup)
    cat > "${s}/home/.98hdd/cards/main.conf" <<EOF
IMAGE="${s}/img/a.hda"
OFFSET=${HARNESS_OFFSET}
EOF
    sut_eval "$s" "load_cards"
    assert_exit "U-L2 missing-serial exit" 1 "$h_last_status"
    assert_contains "U-L2 missing-serial names-it" "SERIAL" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "U-L3"; then
    s=$(ul_setup)
    cat > "${s}/home/.98hdd/cards/main.conf" <<EOF
IMAGE="${s}/img/a.hda"
SERIAL="1111-2222"
EOF
    sut_eval "$s" "load_cards"
    assert_exit "U-L3 missing-offset exit" 1 "$h_last_status"
    assert_contains "U-L3 missing-offset names-it" "OFFSET" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "U-L4"; then
    s=$(ul_setup)
    cat > "${s}/home/.98hdd/cards/main.conf" <<EOF
OFFSET=${HARNESS_OFFSET}
SERIAL="1111-2222"
EOF
    sut_eval "$s" "load_cards"
    assert_exit "U-L4 missing-image exit" 1 "$h_last_status"
    assert_contains "U-L4 missing-image names-it" "IMAGE" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "U-L5"; then
    # 読み飛ばすと、設定したつもりのカードが存在しないことになる。
    s=$(ul_setup)
    cat > "${s}/home/.98hdd/cards/my card.conf" <<EOF
IMAGE="${s}/img/a.hda"
OFFSET=${HARNESS_OFFSET}
SERIAL="1111-2222"
EOF
    sut_eval "$s" "load_cards"
    assert_exit "U-L5 bad-name exit" 1 "$h_last_status"
    assert_contains "U-L5 bad-name names-it" "my card" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "U-L6"; then
    s=$(ul_setup)
    write_card "$s" "games" "${s}/img/a.hda" "3333-4444"
    sut_eval "$s" "load_cards"
    assert_exit "U-L6 default-pull-dir exit" 0 "$h_last_status"
    assert_contains "U-L6 default-pull-dir value" "|${s}/home/9801/hdd/games" \
        "$h_last_output"
    scratch_drop "$s"
fi

if h_want "U-L7"; then
    # SEND_DIR 省略時はグローバルの値。カードごとに分けるのは任意（仕様 §3.1）。
    s=$(ul_setup)
    write_rc "$s" "${s}/img/a.hda"
    write_card "$s" "main" "${s}/img/a.hda" "1111-2222"
    sut_eval "$s" 'load_cards >/dev/null; printf "%s\n" "$SEND_DIR"'
    assert_eq "U-L7 send-dir-inherited" "${s}/home/9801/send" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "U-L8"; then
    # ディレクトリは在るが .conf が 0 個 → 暗黙の default へ落ちる。
    s=$(ul_setup)
    write_rc "$s" "${s}/img/a.hda"
    sut_eval "$s" "load_cards"
    assert_exit "U-L8 empty-dir exit" 0 "$h_last_status"
    assert_contains "U-L8 empty-dir default" "default|" "$h_last_output"
    assert_contains "U-L8 empty-dir legacy-pull" "|${s}/home/9801/hdd" \
        "$h_last_output"
    scratch_drop "$s"
fi

if h_want "U-L9"; then
    # ディレクトリ自体が無い → 同じく暗黙の default。
    s=$(scratch_new)
    write_rc "$s" "${s}/img/a.hda"
    sut_eval "$s" "load_cards"
    assert_exit "U-L9 no-dir exit" 0 "$h_last_status"
    assert_contains "U-L9 no-dir default" "default|" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "U-L10"; then
    # PULL_DIR の実パス衝突。シンボリックリンク経由でも捕まえること
    # （片方だけ解決すると見逃す）。
    s=$(ul_setup)
    mkdir -p "${s}/home/mirror"
    ln -s "${s}/home/mirror" "${s}/home/mirror-link"
    write_card "$s" "alpha" "${s}/img/a.hda" "1111-2222" "${s}/home/mirror"
    write_card "$s" "bravo" "${s}/img/b.hda" "3333-4444" "${s}/home/mirror-link"
    sut_eval "$s" "load_cards"
    assert_exit "U-L10 pull-dir-clash exit" 1 "$h_last_status"
    assert_contains "U-L10 pull-dir-clash names-alpha" "alpha" "$h_last_output"
    assert_contains "U-L10 pull-dir-clash names-bravo" "bravo" "$h_last_output"
    scratch_drop "$s"
fi

# ============================================ U-C: 選択の決定表
# 仕様 §3.4・§3.5。select_card [-c <名前>] が選んだ 1 行を出す。
uc_setup() {            # uc_setup → 砂場を作り、rc も書く
    local s
    s=$(scratch_new)
    mkdir -p "${s}/home/.98hdd/cards"
    printf '%s\n' "$s"
}
# 選ばれたカードの名前だけを取り出す小道具。
uc_name='select_card "$@" | /usr/bin/awk -F"|" "{print \$1}"'

if h_want "U-C1"; then
    # 定義 0 枚 → 暗黙の default。照合しない（SERIAL が無い）。
    s=$(scratch_new)
    make_image "${s}/img/a.hda" "11112222"
    write_rc "$s" "${s}/img/a.hda"
    sut_eval "$s" "select_card"
    assert_exit "U-C1 implicit-default exit" 0 "$h_last_status"
    assert_contains "U-C1 implicit-default name" "default|" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "U-C2"; then
    s=$(uc_setup)
    make_image "${s}/img/a.hda" "11112222"
    write_card "$s" "main" "${s}/img/a.hda" "1111-2222"
    sut_eval "$s" "select_card"
    assert_exit "U-C2 single-card exit" 0 "$h_last_status"
    assert_contains "U-C2 single-card name" "main|" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "U-C3"; then
    # 存在するがシリアルが宣言と違う → 段 2 で 0 枚 → E4。
    s=$(uc_setup)
    make_image "${s}/img/a.hda" "11112222"
    write_card "$s" "main" "${s}/img/a.hda" "9999-9999"
    sut_eval "$s" "select_card"
    assert_exit "U-C3 serial-mismatch exit" 1 "$h_last_status"
    assert_contains "U-C3 serial-mismatch shows-found" "1111-2222" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "U-C4"; then
    s=$(uc_setup)
    write_card "$s" "main" "${s}/img/absent.hda" "1111-2222"
    sut_eval "$s" "select_card"
    assert_exit "U-C4 image-absent exit" 1 "$h_last_status"
    scratch_drop "$s"
fi

if h_want "U-C5"; then
    s=$(uc_setup)
    make_image "${s}/img/b.hda" "33334444"
    write_card "$s" "alpha" "${s}/img/absent-a.hda" "1111-2222"
    write_card "$s" "bravo" "${s}/img/b.hda" "3333-4444"
    write_card "$s" "charlie" "${s}/img/absent-c.hda" "5555-6666"
    sut_eval "$s" "select_card"
    assert_exit "U-C5 one-mounted exit" 0 "$h_last_status"
    assert_contains "U-C5 one-mounted name" "bravo|" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "U-C6"; then
    # **この設計の核心。** BlueSCSI のカードを同じ手順で作ると、ラベルも
    # イメージのファイル名も同じになるので、3 定義とも同じパスを指す。
    # 段 1（存在チェック）だけでは「候補 3 つ＝曖昧」になり、最も普通の
    # 構成が使えなくなる。段 2 のシリアルで 1 枚に絞れること。
    s=$(uc_setup)
    shared="${s}/img/shared.hda"
    make_image "$shared" "33334444"
    write_card "$s" "alpha" "$shared" "1111-2222"
    write_card "$s" "bravo" "$shared" "3333-4444"
    write_card "$s" "charlie" "$shared" "5555-6666"
    sut_eval "$s" "select_card"
    assert_exit "U-C6 same-path-disambiguated exit" 0 "$h_last_status"
    assert_contains "U-C6 same-path-disambiguated name" "bravo|" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "U-C7"; then
    # 本物の曖昧さ。別々のイメージが 2 つ在り、どちらも自分の宣言と一致。
    # ここで「最初の 1 枚」を選ぶと、意図しないカードへ書く道が開く。
    s=$(uc_setup)
    make_image "${s}/img/a.hda" "11112222"
    make_image "${s}/img/b.hda" "33334444"
    write_card "$s" "alpha" "${s}/img/a.hda" "1111-2222"
    write_card "$s" "bravo" "${s}/img/b.hda" "3333-4444"
    sut_eval "$s" "select_card"
    assert_exit "U-C7 truly-ambiguous exit" 1 "$h_last_status"
    assert_contains "U-C7 truly-ambiguous names-alpha" "alpha" "$h_last_output"
    assert_contains "U-C7 truly-ambiguous names-bravo" "bravo" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "U-C8"; then
    s=$(uc_setup)
    write_card "$s" "alpha" "${s}/img/absent-a.hda" "1111-2222"
    write_card "$s" "bravo" "${s}/img/absent-b.hda" "3333-4444"
    write_card "$s" "charlie" "${s}/img/absent-c.hda" "5555-6666"
    sut_eval "$s" "select_card"
    assert_exit "U-C8 none-mounted exit" 1 "$h_last_status"
    assert_contains "U-C8 none-mounted lists-alpha" "alpha" "$h_last_output"
    assert_contains "U-C8 none-mounted lists-charlie" "charlie" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "U-C9"; then
    # 挿さってはいるがどの定義とも一致しない。「何も挿さっていない」とは
    # 別の事態で、利用者の次の一手が違う（定義を書き足すか、カードを替えるか）。
    # 読めたシリアルが出れば、そのまま .conf に貼れる。
    s=$(uc_setup)
    make_image "${s}/img/x.hda" "deadbeef"
    write_card "$s" "alpha" "${s}/img/x.hda" "1111-2222"
    write_card "$s" "bravo" "${s}/img/absent-b.hda" "3333-4444"
    write_card "$s" "charlie" "${s}/img/absent-c.hda" "5555-6666"
    sut_eval "$s" "select_card"
    assert_exit "U-C9 unknown-card exit" 1 "$h_last_status"
    assert_contains "U-C9 unknown-card shows-serial" "DEAD-BEEF" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "U-C10"; then
    # -c は段 1・段 2 を飛ばす。2 枚マウント済み（E3 になる状況）でも、
    # 指定した 3 つ目（未マウント）を選ぼうとして E1/E2 の側へ進む。
    # E3 にならないことが「飛ばしている」証拠になる。
    s=$(uc_setup)
    make_image "${s}/img/a.hda" "11112222"
    make_image "${s}/img/b.hda" "33334444"
    write_card "$s" "alpha" "${s}/img/a.hda" "1111-2222"
    write_card "$s" "bravo" "${s}/img/b.hda" "3333-4444"
    write_card "$s" "charlie" "${s}/img/absent-c.hda" "5555-6666"
    sut_eval "$s" "select_card -c charlie"
    assert_exit "U-C10 explicit-skips-detect exit" 1 "$h_last_status"
    assert_contains "U-C10 explicit-skips-detect names-charlie" "charlie" "$h_last_output"
    assert_lacks "U-C10 explicit-skips-detect not-ambiguous" "曖昧" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "U-C11"; then
    s=$(uc_setup)
    make_image "${s}/img/a.hda" "11112222"
    write_card "$s" "alpha" "${s}/img/a.hda" "1111-2222"
    sut_eval "$s" "select_card -c nosuch"
    assert_exit "U-C11 unknown-name exit" 1 "$h_last_status"
    assert_contains "U-C11 unknown-name echoes" "nosuch" "$h_last_output"
    assert_contains "U-C11 unknown-name lists-known" "alpha" "$h_last_output"
    scratch_drop "$s"
fi

if h_want "U-C12"; then
    s=$(uc_setup)
    make_image "${s}/img/a.hda" "11112222"
    write_card "$s" "alpha" "${s}/img/a.hda" "1111-2222"
    sut_eval "$s" "select_card -c alpha"
    assert_exit "U-C12 explicit-match exit" 0 "$h_last_status"
    assert_contains "U-C12 explicit-match name" "alpha|" "$h_last_output"
    scratch_drop "$s"
fi

scratch_drop "$u_scratch"
