#!/usr/bin/env bash
#
# NyaHaX'93 weekly ranking reset - Mac-side regression suite.
#
#   ./run.sh              全ケースを走らせる
#   ./run.sh -v           ケース名を一件ずつ出す
#   ./run.sh -k <substr>  名前に <substr> を含むケースだけ走らせる
#
# 依存は clang と nm のみ。ネットワークも追加インストールも使わない。
# 一時ファイルは ${TMPDIR} 配下にだけ作り、終了時に必ず消す。
#
# ケース名は .work/plans/nx93-ranking-reset/test-design.md の番号に対応する。
# S1..S7 はここ、それ以外は test_rank.c にある。
#
set -euo pipefail

verbose=0
filter=""
while [ $# -gt 0 ]; do
    case "$1" in
        -v) verbose=1; shift ;;
        -k) filter="${2:-}"; shift 2 ;;
        -h|--help) /usr/bin/sed -n '2,13p' "$0"; exit 0 ;;
        *) printf 'unknown option: %s\n' "$1" >&2; exit 2 ;;
    esac
done

here=$(cd -- "$(dirname -- "$0")" && pwd)
root=$(cd -- "${here}/.." && pwd)
rank_c="${root}/dist/RANK.C"

ok_count=0
fail_count=0
report_fail() { fail_count=$((fail_count + 1)); printf 'FAIL  %s\n' "$*" >&2; }
report_ok() {
    ok_count=$((ok_count + 1))
    if [ "${verbose}" -eq 1 ]; then printf 'ok    %s\n' "$*"; fi
}

# ---------------------------------------------------------------- 実装の有無
# RANK.C が無いあいだスイートは赤のままで、それが実装前の正しい状態。
# ここで抜けないと、以降の静的検査が「ファイルが無いので違反も無い」で
# 緑を出してしまう。
if [ ! -f "${rank_c}" ]; then
    printf 'FAIL  S0 implementation-present: %s がありません。\n' "${rank_c}" >&2
    printf '      RANK.C が実装されるまでこのスイートは赤のままです（それが正しい状態）。\n' >&2
    printf '\n1 件失敗 / 0 件成功\n' >&2
    exit 1
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/nx93rank.XXXXXXXX")
cleanup() { rm -rf "${work}"; }
trap cleanup EXIT
trap 'cleanup; exit 130' HUP INT TERM

# ------------------------------------------------------------------ 静的検査
# grep は実装差を避けるため BSD grep に固定する（mise.toml の verify:encoding と同じ理由）。
GREP=/usr/bin/grep

run_static() {
    local name="$1"; shift
    case "${name}" in
        *"${filter}"*) ;;
        *) return 0 ;;
    esac
    if "$@"; then report_ok "${name}"; else report_fail "${name}"; fi
}

# clang は拡張子 .C を C++ とみなす（clang -### -c RANK.C が -x c++ を出す）。
# -x c を外すと QuickC が見るのと違う言語でコンパイルしてしまう。
#
# 警告の除外は 1 つも入れていない。macOS の SDK は sprintf を deprecated に
# しているので、RANK.C に sprintf が入れば S2 がそこで赤くなる。QuickC 2.00 に
# snprintf は無いから代替はそのままでは無く、実装側の設計判断が必要になる。
# 黙って通さないために除外を置かない。
CC89="-x c -std=c89 -pedantic-errors -Wall -Wextra -Werror -Wformat-security"

# S1: RANK.C は ASCII のみ。送信時に nkf -s を通るため、SJIS 2 バイト目の 0x5C を
#     QuickC がエスケープと誤読する事故を構造的に排除する（plan.md 実装方針）。
check_ascii_only() {
    local pat
    pat=$(printf '[\200-\377]')
    if LC_ALL=C "${GREP}" -n "${pat}" "${rank_c}" >/dev/null 2>&1; then
        printf '      非 ASCII バイトを含む行:\n' >&2
        LC_ALL=C "${GREP}" -n "${pat}" "${rank_c}" | head -5 >&2
        return 1
    fi
    return 0
}

# S2: DOS ビルド（テスト用マクロなし）が C89 で厳密に通ること。
check_c89_build() {
    # shellcheck disable=SC2086
    clang ${CC89} -c "${rank_c}" -o "${work}/rank_dos.o"
}

# S3: DOS ビルドがリンクできる（main がある）。
check_dos_link() {
    # shellcheck disable=SC2086
    clang ${CC89} "${rank_c}" -o "${work}/RANK"
}

# S2 が失敗していれば .o は無い。nm のエラーを飲んで「違反ゼロ」を出す偽グリーンを
# 避けるため、オブジェクトの存在と nm 自身の終了コードを先に確かめる。
nm_into() {
    if [ ! -f "${work}/rank_dos.o" ]; then
        printf '      rank_dos.o がありません（S2 が失敗しています）。\n' >&2
        return 1
    fi
    if ! nm "$@" "${work}/rank_dos.o" >"${work}/nm.out" 2>"${work}/nm.err"; then
        printf '      nm が失敗しました:\n' >&2
        head -5 "${work}/nm.err" >&2
        return 1
    fi
    return 0
}

# S4: ヒープを使わない。DOS small モデルの 64KB データセグメント上で
#     台帳を丸ごと読まないという設計（plan.md「メモリに載せない」）を機械的に固定する。
check_no_heap() {
    nm_into -u || return 1
    if "${GREP}" -E '^_(malloc|calloc|realloc|reallocf|free|strdup)$' "${work}/nm.out" >&2; then
        return 1
    fi
    return 0
}

# S5: テスト専用の継ぎ手が DOS ビルドに残らない（plan.md「DOS ビルドには入らない」）。
check_no_test_seam() {
    nm_into || return 1
    if "${GREP}" -i 'rank_test' "${work}/nm.out" >&2; then
        return 1
    fi
    return 0
}

# S6: 名前の大小同一視に <ctype.h> やロケール依存関数を使わない。
#     名前欄には SJIS バイトが入りうる。toupper() に 0x80 以上を渡すのは
#     ANSI C では未定義で、stricmp/strcasecmp は QuickC の ANSI 面には無い。
check_no_locale_fold() {
    if "${GREP}" -nE '(^|[^A-Za-z0-9_])(toupper|tolower|strcasecmp|strncasecmp|stricmp|strcmpi|setlocale)[[:space:]]*\(' \
        "${rank_c}" >&2; then
        return 1
    fi
    if "${GREP}" -nE '^[[:space:]]*#[[:space:]]*include[[:space:]]*<(ctype|locale)\.h>' \
        "${rank_c}" >&2; then
        return 1
    fi
    return 0
}

# S7: 行を書式文字列として渡さない。名前に % が入りうる（Bio_100%、%n%s%%%%）。
#     printf 族の書式引数が文字列リテラルでないものを拒む。
#     -Wformat-security が CC89 に入っているので、コンパイラ側でも同じ穴を塞ぐ。
#
#     否定クラスから空白を外してあるのが要点。[^"] だけにすると、直前の
#     [[:space:]]* が幅ゼロで済んでしまい [^"] がその空白自身に当たるので、
#     どんな正しい呼び出しも一致してしまう（実測で全行が誤検出になった）。
check_no_format_injection() {
    if "${GREP}" -nE '(^|[^A-Za-z0-9_])(printf|vprintf)[[:space:]]*\([[:space:]]*[^"[:space:]]' \
        "${rank_c}" >&2; then
        return 1
    fi
    if "${GREP}" -nE '(^|[^A-Za-z0-9_])(fprintf|sprintf|vfprintf|vsprintf)[[:space:]]*\([^,]*,[[:space:]]*[^"[:space:]]' \
        "${rank_c}" >&2; then
        return 1
    fi
    return 0
}

# S8: DOS ビルドであることの判定が、コンパイラごとのマクロの綴りに依らないこと。
#     QuickC 2.00 は MSDOS を定義する。ia16-elf-gcc 6.3.0 は __MSDOS__ と _DOS を
#     定義するが素の MSDOS は定義しない。どれか一つで DOS 経路に入らないと、
#     区切り文字が '/' のまま、"A:" がドライブ接頭辞として扱われないまま、
#     警告もエラーも出さずにホスト用の挙動でビルドが通ってしまう。
#     判定は RANK_DOS 一箇所に集約されている前提で、そのマクロと RANK_SEP を見る。
#     -dM は原文のトークンをそのまま出すので、期待値は '\\' と二重になる。
rank_macro() {
    local macro="$1"
    shift
    clang -x c -std=c89 -dM -E "$@" "${rank_c}" 2>/dev/null \
        | "${GREP}" -E "^#define ${macro} " || true
}

check_dos_detection() {
    local spelling status got want_host want_dos
    want_host="#define RANK_SEP '/'"
    want_dos="#define RANK_SEP '\\\\'"
    status=0

    # DOS を名乗らないホスト（Mac の clang）では DOS 経路に入らない。
    got=$(rank_macro RANK_DOS)
    if [ -n "${got}" ]; then
        printf '      -D なし: RANK_DOS が定義されています（%s）。\n' "${got}" >&2
        status=1
    fi
    got=$(rank_macro RANK_SEP)
    if [ "${got}" != "${want_host}" ]; then
        printf '      -D なし: RANK_SEP が %s（期待 %s）。\n' "${got:-未定義}" "${want_host}" >&2
        status=1
    fi

    for spelling in MSDOS __MSDOS__ _DOS; do
        if [ -z "$(rank_macro RANK_DOS "-D${spelling}=1")" ]; then
            printf '      -D%s: RANK_DOS が定義されません。\n' "${spelling}" >&2
            status=1
        fi
        got=$(rank_macro RANK_SEP "-D${spelling}=1")
        if [ "${got}" != "${want_dos}" ]; then
            printf '      -D%s: RANK_SEP が %s（期待 %s）。\n' \
                "${spelling}" "${got:-未定義}" "${want_dos}" >&2
            status=1
        fi
    done
    return "${status}"
}

# S9: DOS 判定の綴りが、RANK_DOS を決める 1 行の外に現れないこと。
#     S8 は RANK_DOS しか見ないので、どこかが RANK_DOS を飛ばして素の MSDOS を
#     直接見に行くと S8 は緑のまま穴が開く。数えるのは前処理指令の行だけで、
#     経緯を説明する散文は対象外。
check_single_dos_gate() {
    local lines count
    lines=$(LC_ALL=C "${GREP}" -nE '^[[:space:]]*#' "${rank_c}" \
            | LC_ALL=C "${GREP}" -E '(^|[^A-Za-z0-9_])(MSDOS|__MSDOS__|_DOS)([^A-Za-z0-9_]|$)' \
            || true)
    count=$(printf '%s' "${lines}" | "${GREP}" -c . || true)
    if [ "${count}" != "1" ]; then
        printf '      DOS 判定の綴りを含む前処理指令が %s 行あります（期待 1 行）:\n' "${count}" >&2
        printf '%s\n' "${lines}" | head -5 >&2
        return 1
    fi
    return 0
}

run_static "S1 ascii-only"            check_ascii_only
run_static "S2 c89-strict-build"      check_c89_build
run_static "S3 dos-link"              check_dos_link
run_static "S4 no-heap"               check_no_heap
run_static "S5 no-test-seam"          check_no_test_seam
run_static "S6 no-locale-fold"        check_no_locale_fold
run_static "S7 no-format-injection"   check_no_format_injection
run_static "S8 dos-detection"         check_dos_detection
run_static "S9 single-dos-gate"       check_single_dos_gate

# -------------------------------------------------------------- スイート本体
# ハーネス側は gnu89 で組む（mkdir / chmod / dirent を使うため）。RANK.C 自体の
# C89 厳密性は S2 で別途担保されているので、ここで緩めても穴にならない。
if ! clang -x c -std=gnu89 -Wall -Wextra -Werror -g \
      -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
      -DRANK_NO_MAIN -DRANK_TEST_CLOCK -DRANK_TEST_FAULT \
      "${here}/test_rank.c" -o "${work}/test_rank"; then
    report_fail "H1 harness-build: test_rank.c がビルドできません。"
    printf '\n%d 件失敗 / %d 件成功\n' "${fail_count}" "${ok_count}" >&2
    exit 1
fi

# タイムゾーンを変えて同じ結果になることを要求する。日数差の計算に mktime や
# localtime が紛れ込むと、夏時間の切替を跨いだ 7 日が 6 日と評価されて
# リセットが飛ぶ。UTC / 夏時間なし / 夏時間ありの 3 本で不変性を確かめる。
round=0
for tz in UTC Asia/Tokyo America/Los_Angeles; do
    round=$((round + 1))
    scratch="${work}/run${round}"
    if [ -n "${filter}" ]; then
        ran=0
        TZ="${tz}" LC_ALL=C "${work}/test_rank" "${scratch}" "${filter}" || ran=1
    else
        ran=0
        TZ="${tz}" LC_ALL=C "${work}/test_rank" "${scratch}" || ran=1
    fi
    if [ "${ran}" -eq 0 ]; then
        report_ok "TZ=${tz}"
    else
        report_fail "TZ=${tz} で失敗しました。"
    fi
done

if [ "${fail_count}" -ne 0 ]; then
    printf '\n%d 件失敗 / %d 件成功\n' "${fail_count}" "${ok_count}" >&2
    exit 1
fi
printf '\nすべて成功（%d 件）。\n' "${ok_count}"
