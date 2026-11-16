#!/usr/bin/env bash
#
# 98hdd 複数カード対応 — 回帰スイート。
#
#   ./run-cards.sh              全ケースを走らせる
#   ./run-cards.sh -v           ケース名を一件ずつ出す
#   ./run-cards.sh -k <substr>  名前に <substr> を含むケースだけ走らせる
#
# 依存は bash・mformat・mdir・mcopy・mmd・od・shasum・dd のみ。
# ネットワークも追加インストールも使わない。一時ファイルは ${TMPDIR} 配下に
# だけ作り、終了時に必ず消す。
#
# ケース名は .work/plans/98hdd-multi-card/test-design.md の番号に対応する。
# H1..H6（前提検査）と S1..S8（静的検査）はここ、それ以外は unit.sh /
# integration.sh / cli.sh にある。
#
# H 系は被試験対象ではなく「試験が意味を持つ前提」である。1 件でも落ちたら
# ケースを走らせずに終わる。特に H5 と H6 は、落ちているのに緑に見える形の
# 失敗で、H6 が落ちた状態で走らせると利用者の設定を壊す。
#
set -uo pipefail

verbose=0
filter=""
while [ $# -gt 0 ]; do
    case "$1" in
        -v) verbose=1; shift ;;
        -k) filter="${2:-}"; shift 2 ;;
        -h|--help) /usr/bin/sed -n '2,20p' "$0"; exit 0 ;;
        *) printf 'unknown option: %s\n' "$1" >&2; exit 2 ;;
    esac
done

here=$(cd -- "$(dirname -- "$0")" && pwd)
root=$(cd -- "${here}/.." && pwd)

export HARNESS_SUT="${root}/98hdd.sh"
export HARNESS_VERBOSE="${verbose}"
export HARNESS_FILTER="${filter}"

# shellcheck source=lib/harness.sh
. "${here}/lib/harness.sh"

[ -f "$HARNESS_SUT" ] || { printf '被試験対象が無い: %s\n' "$HARNESS_SUT" >&2; exit 2; }

preflight_failed=0
pf_ok()   { if [ "$verbose" -eq 1 ]; then printf 'ok    %s\n' "$1"; fi; }
pf_fail() {
    preflight_failed=$((preflight_failed + 1))
    printf 'FAIL  %s\n' "$1" >&2
    shift
    while [ $# -gt 0 ]; do printf '        %s\n' "$1" >&2; shift; done
}

# =============================================================== H: 前提検査

# ---- H1 道具が揃っている
h1_missing=""
for cmd in mformat mdir mcopy mmd od shasum dd; do
    command -v "$cmd" >/dev/null 2>&1 || h1_missing="${h1_missing} ${cmd}"
done
if [ -z "$h1_missing" ]; then pf_ok "H1 tools-present"
else pf_fail "H1 tools-present" "見つからない:${h1_missing}"; fi

pf_scratch=""
if [ "$preflight_failed" -eq 0 ]; then
    pf_scratch=$(scratch_new) || pf_fail "H1 scratch" "砂場を作れない"
fi

if [ -n "$pf_scratch" ]; then
    trap 'scratch_drop "$pf_scratch" 2>/dev/null || true' EXIT HUP INT TERM

    pf_img="${pf_scratch}/img/a.hda"
    if make_image "$pf_img" "11112222"; then
        pf_ok "H1 make-image"
    else
        pf_fail "H1 make-image" "mformat が失敗した"
    fi

    # ---- H2 拡張ブート署名が 0x29
    # 落ちたら、ハーネスが FAT12/16 のブートセクタを作れていない。
    # シリアルの位置そのものが成り立たないので、同定の試験が全部無意味になる。
    if [ -f "$pf_img" ]; then
        got=$(LC_ALL=C od -An -j $(( HARNESS_OFFSET + 38 )) -N 1 -t x1 "$pf_img" \
              | /usr/bin/awk 'NR==1 {print $1}')
        if [ "$got" = "29" ]; then pf_ok "H2 boot-signature"
        else pf_fail "H2 boot-signature" "want 29  got ${got:-読めない}"; fi

        # ---- H3 ファイルシステム種別が FAT16
        # FAT12 でもシリアルの位置は同じだが、実機は FAT16 なので標本を合わせる。
        # FAT32 になっていたら署名の位置ごと違うので H2 で先に落ちる。
        got=$(LC_ALL=C od -An -j $(( HARNESS_OFFSET + 54 )) -N 8 -c "$pf_img" \
              | /usr/bin/awk 'NR==1 {printf "%s%s%s%s%s", $1, $2, $3, $4, $5}')
        if [ "$got" = "FAT16" ]; then pf_ok "H3 fs-type"
        else pf_fail "H3 fs-type" "want FAT16  got [${got}]"; fi

        # ---- H4 指定したシリアルが読める
        # -N が効いていないと、全イメージが同じ（または乱数の）シリアルになり、
        # 同定の試験が区別をつけられなくなる。
        got=$(read_serial_ref "$pf_img" "$HARNESS_OFFSET" || true)
        if [ "$got" = "1111-2222" ]; then pf_ok "H4 serial-controlled"
        else pf_fail "H4 serial-controlled" "want 1111-2222  got [${got}]"; fi
    fi

    # ---- H5 偽 mtools が PATH 先頭で実際に呼ばれる
    # これが落ちると I-N 系（照合前に何もしない）が全部「緑の空振り」になる。
    # 呼ばれていないのに「ログが空」を満たしてしまうため、落ちていることが
    # 結果からは見えない。
    pf_log="${pf_scratch}/mtools.log"
    fake_mtools_install "${pf_scratch}/bin" "$pf_log" 0
    mkdir -p "${pf_scratch}/home"
    cat > "${pf_scratch}/home/.98hddrc" <<RC
HDD_IMAGE="${pf_img}"
HDD_OFFSET=${HARNESS_OFFSET}
HDD_DRIVE="p"
BASE_DIR="${pf_scratch}/home/9801"
SEND_DIR="${pf_scratch}/home/9801/send"
PULL_DIR="${pf_scratch}/home/9801/hdd"
RC
    run_98hdd "$pf_scratch" ls >/dev/null 2>&1 || true
    got=$(mtools_log_lines "$pf_log")
    if [ "$got" -gt 0 ] 2>/dev/null; then pf_ok "H5 fake-mtools-reached"
    else pf_fail "H5 fake-mtools-reached" \
        "偽 mtools が 1 度も呼ばれていない（ログ ${got} 行）" \
        "PATH 先頭の差し替えが効いていない。この状態で I-N 系を走らせると緑の空振りになる"; fi

    # ---- H6 HOME の差し替えが効き、実 $HOME に書かれていない
    # 落ちると試験が利用者の設定を壊す。最も危険な失敗なので、
    # 「砂場に書かれた」だけでなく「実 $HOME が動いていない」も見る。
    # 設定ファイルが実 $HOME に無い環境（コントリビュータの機械）では、
    # ハッシュの前後比較は "" と "" の比較になって無条件に通る。それでは
    # 「書かれていない」を見たことにならないので、存在の有無で場合を分ける。
    real_rc="${HOME}/.98hddrc"
    real_base="${HOME}/9801"
    rc_existed=0
    before=""
    if [ -f "$real_rc" ]; then
        rc_existed=1
        before=$(shasum -a 256 "$real_rc" | /usr/bin/awk '{print $1}')
    fi
    base_existed=0
    [ -d "$real_base" ] && base_existed=1

    rm -f "${pf_scratch}/home/.98hddrc"
    run_98hdd "$pf_scratch" init >/dev/null 2>&1 || true

    h6_problem=""
    if [ ! -f "${pf_scratch}/home/.98hddrc" ]; then
        h6_problem="砂場に .98hddrc が作られていない（HOME の差し替えが効いていない）"
    elif [ "$rc_existed" -eq 1 ]; then
        after=$(shasum -a 256 "$real_rc" 2>/dev/null | /usr/bin/awk '{print $1}')
        [ "$before" = "$after" ] || \
            h6_problem="**実 ${real_rc} が書き換わった** (${before} → ${after:-消えた})"
    else
        # 元から無いので、できていないことを見る。init は設定を作る側なので、
        # HOME が漏れていればここに現れる。
        [ -f "$real_rc" ] && h6_problem="**実 ${real_rc} が作られた**"
    fi
    # ディレクトリ側も見る。init は BASE_DIR 配下に send/ と hdd/ を作るので、
    # 設定ファイルだけを見ていると BASE_DIR の漏れを見逃す。
    if [ -z "$h6_problem" ] && [ "$base_existed" -eq 0 ] && [ -d "$real_base" ]; then
        h6_problem="**実 ${real_base} が作られた**"
    fi

    if [ -n "$h6_problem" ]; then
        pf_fail "H6 home-sandboxed" "$h6_problem" \
            "ここで止める。先へ進むと試験が利用者の設定を壊す"
    else
        pf_ok "H6 home-sandboxed"
    fi

    # ---- H7 run_98hdd が終了コードを正しく伝える
    # これが落ちると assert_exit が全部ひとつ前の呼び出しの古い値を見る。
    # 実際に一度その欠陥を作った: out=$(run_98hdd ...) と書くとサブシェルで
    # 走るので h_last_status が親へ伝わらず、期待 1 のところ 99 のままだった。
    # 終了コードの表明が全部「見ていないのに緑」になる。
    h_last_status=99
    run_98hdd "$pf_scratch" this-is-not-a-command >/dev/null 2>&1 || true
    if [ "$h_last_status" = "1" ]; then
        pf_ok "H7 status-propagated"
    else
        pf_fail "H7 status-propagated" \
            "未知のコマンドは終了 1 のはずが h_last_status=${h_last_status}" \
            "99 のままなら、run_98hdd をコマンド置換で包んでいる（サブシェルが飲む）" \
            "この状態では assert_exit が全部古い値を見る"
    fi
fi

if [ "$preflight_failed" -ne 0 ]; then
    printf '\n前提検査が %d 件落ちた。ケースは走らせない。\n' "$preflight_failed" >&2
    exit 1
fi

# =============================================================== S: 静的検査
# 構造の保証。「そう書かれていない」ことを grep で見る。実装が規約を破った
# 瞬間に落ちるので、振る舞いの試験では捕まらない形の退行を押さえる。
#
# grep の綴りは黙って壊れやすい（BSD grep に \b は無い、grep -c は行数を
# 数えて出現数を数えない）。各検査は現行の 98hdd.sh に対して予測した結果が
# 実際に出ることを確認してから仕込んである。

sut="$HARNESS_SUT"

# ---- S1 構文が通る
if bash -n "$sut" 2>/dev/null; then h_pass "S1 syntax"
else h_fail "S1 syntax" "bash -n が落ちた"; fi

# ---- S2 main が BASH_SOURCE の条件の内側にある
# これが崩れると source が全部実行してしまい、単体試験が 1 本も書けない。
if LC_ALL=C grep -q 'BASH_SOURCE' "$sut"; then h_pass "S2 sourceable"
else h_fail "S2 sourceable" \
    'main "$@" が BASH_SOURCE の条件で包まれていない' \
    'source したときに全部実行されるので、単体試験が成立しない'; fi

# ---- S3 ドライブ文字を mtools へ渡していない
# 1 箇所でも残ると、そこだけ ~/.mtoolsrc を読む。イメージを指定したつもりで
# 別のイメージに書く経路がそこに開く。
s3=$(LC_ALL=C grep -c 'run_mtools.*DRIVE' "$sut" || true)
if [ "${s3:-0}" -eq 0 ]; then h_pass "S3 no-drive-letter"
else h_fail "S3 no-drive-letter" \
    "run_mtools にドライブ文字を渡している行が ${s3} 行ある" \
    "$(LC_ALL=C grep -n 'run_mtools.*DRIVE' "$sut" | head -3)"; fi

# ---- S4 mtools の実行が run_mtools だけを通る
# 偽 mtools による観測に穴を作らないため。check_deps の依存一覧は
# 実行ではないので除く。
s4=$(LC_ALL=C grep -nE '(^|[^A-Za-z_])(mdir|mcopy|mmd|mformat)([ "]|$)' "$sut" \
     | LC_ALL=C grep -v 'run_mtools' \
     | LC_ALL=C grep -v 'for cmd in' \
     | LC_ALL=C grep -v '^[0-9]*: *#' || true)
if [ -z "$s4" ]; then h_pass "S4 mtools-via-wrapper"
else h_fail "S4 mtools-via-wrapper" "run_mtools を通らない実行がある" "$s4"; fi

# ---- S5 run_mtools が MTOOLS_SKIP_CHECK=1 を立てる
# 実測で必須と分かっている。外すと -i 方式が Bad media types fe/f8 で全部
# 落ちるが、理由の分からない落ち方をする。
if LC_ALL=C grep -q 'run_mtools().*MTOOLS_SKIP_CHECK=1' "$sut"; then
    h_pass "S5 skip-check"
else
    h_fail "S5 skip-check" 'run_mtools が MTOOLS_SKIP_CHECK=1 を立てていない' \
        '-i 方式はこれが無いと Bad media types fe/f8 で落ちる（実測）'
fi

# ---- S6 @@ はイメージ指定の組み立てだけに現れる
# 別の場所でオフセットを足すと、FAT の先頭でない位置を指す経路ができる。
s6=$(LC_ALL=C grep -n '@@' "$sut" | LC_ALL=C grep -v 'spec=' || true)
if [ -z "$s6" ]; then h_pass "S6 offset-spelling"
else h_fail "S6 offset-spelling" "@@ が spec の組み立て以外に現れる" "$s6"; fi

# ---- S7 シリアル位置の定数が 1 箇所にまとまっている
# 2 箇所に散ると、片方だけ直す欠陥が入る。署名とシリアルは隣り合う
# 2 つの定数で、どちらも 1 回だけ定義する。
s7_sig=$(LC_ALL=C grep -c '^CARD_SIG_OFFSET=' "$sut" || true)
s7_ser=$(LC_ALL=C grep -c '^CARD_SERIAL_OFFSET=' "$sut" || true)
if [ "${s7_sig:-0}" -eq 1 ] && [ "${s7_ser:-0}" -eq 1 ]; then
    h_pass "S7 serial-offset-single"
else
    h_fail "S7 serial-offset-single" \
        "CARD_SIG_OFFSET の定義 ${s7_sig} 件・CARD_SERIAL_OFFSET の定義 ${s7_ser} 件" \
        "どちらもちょうど 1 件であること"
fi

# ---- S8 mformat が製品コードに無い
# 仕様 §2 非目標（カードの作成・フォーマットはしない）。ハーネスには在ってよい。
if LC_ALL=C grep -q 'mformat' "$sut"; then
    h_fail "S8 no-mformat" "製品コードに mformat がある（フォーマットは非目標）" \
        "$(LC_ALL=C grep -n 'mformat' "$sut")"
else
    h_pass "S8 no-mformat"
fi

# =========================================================== 下位スイート
# source する（別プロセスで起動すると件数が集計されない）。下位スイートは
# exit を書かない — 書くとランナーごと終わって残りが走らない。
for suite in unit integration cli; do
    f="${here}/${suite}.sh"
    [ -f "$f" ] || continue
    # shellcheck source=/dev/null
    . "$f"
done

# =================================================================== 集計
printf '\n'
if [ "$h_fail_count" -eq 0 ]; then
    printf '前提検査 6 件・ケース %d 件、失敗 0。\n' "$h_ok_count"
    exit 0
fi
printf 'ケース %d 件成功、%d 件失敗。\n' "$h_ok_count" "$h_fail_count" >&2
exit 1
