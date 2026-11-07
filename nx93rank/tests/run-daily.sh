#!/usr/bin/env bash
#
# NyaHaX'93 daily ranking reset - Mac-side regression suite (nx93-daily-reset).
#
#   ./run-daily.sh              全ケースを走らせる
#   ./run-daily.sh -v           ケース名を一件ずつ出す
#   ./run-daily.sh -k <substr>  名前に <substr> を含むケースだけ走らせる
#
#   RANK_C=<path> ./run-daily.sh   対象ソースを差し替える（既定 ../dist/RANK.C）
#
# 依存は clang と nm のみ。ネットワークも追加インストールも使わない。
# 一時ファイルは ${TMPDIR} 配下にだけ作り、終了時に必ず消す。
#
# ケース名は .work/plans/nx93-daily-reset/test-design.md の番号に対応する。
# S10..S12・S6'・H2..H4 はここ、それ以外は test_daily.c と test_cli.c にある。
#
# 既存 run.sh とは独立に走る。mise からは呼ばれない（接続は実装が緑になってから）。
set -euo pipefail

verbose=0
filter=""
while [ $# -gt 0 ]; do
    case "$1" in
        -v) verbose=1; shift ;;
        -k) filter="${2:-}"; shift 2 ;;
        -h|--help) /usr/bin/sed -n '2,17p' "$0"; exit 0 ;;
        *) printf 'unknown option: %s\n' "$1" >&2; exit 2 ;;
    esac
done

here=$(cd -- "$(dirname -- "$0")" && pwd)
root=$(cd -- "${here}/.." && pwd)

# 対象ソースは RANK_C で差し替えられる。test-design §13.2 第 3 段の捨てスタブと
# §14 のミューテーション検証が、リポジトリを一切変更せずに回るための継ぎ手。
# 相対パスは呼び出し元のカレントから解く（絶対パスでも通る）。
if [ -n "${RANK_C:-}" ]; then
    rank_c="${RANK_C}"
else
    rank_c="${root}/dist/RANK.C"
fi

ok_count=0
fail_count=0
report_fail() { fail_count=$((fail_count + 1)); printf 'FAIL  %s\n' "$*" >&2; }
report_ok() {
    ok_count=$((ok_count + 1))
    if [ "${verbose}" -eq 1 ]; then printf 'ok    %s\n' "$*"; fi
}
summary_and_exit() {
    printf '\n%d 件失敗 / %d 件成功\n' "${fail_count}" "${ok_count}" >&2
    exit 1
}

# ---------------------------------------------------------------- 実装の有無
# RANK.C が無いあいだスイートは赤のままで、それが実装前の正しい状態。
# ここで抜けないと、以降の静的検査が「ファイルが無いので違反も無い」で
# 緑を出してしまう。RANK_C の綴り間違いも同じ穴なので、ここで止める。
if [ ! -f "${rank_c}" ]; then
    printf 'FAIL  S0 implementation-present: %s がありません。\n' "${rank_c}" >&2
    if [ -n "${RANK_C:-}" ]; then
        printf '      RANK_C=%s を見ています。綴りを確かめてください。\n' "${RANK_C}" >&2
    fi
    printf '      RANK.C が実装されるまでこのスイートは赤のままです（それが正しい状態）。\n' >&2
    printf '\n1 件失敗 / 0 件成功\n' >&2
    exit 1
fi
rank_c=$(cd -- "$(dirname -- "${rank_c}")" && pwd)/$(basename -- "${rank_c}")

work=$(mktemp -d "${TMPDIR:-/tmp}/nx93rank-daily.XXXXXXXX")
cleanup() { rm -rf "${work}"; }
trap cleanup EXIT
trap 'cleanup; exit 130' HUP INT TERM

GREP=/usr/bin/grep

run_static() {
    local name="$1"; shift
    case "${name}" in
        *"${filter}"*) ;;
        *) return 0 ;;
    esac
    if "$@"; then report_ok "${name}"; else report_fail "${name}"; fi
}

# ------------------------------------------------------------------ 前提検査
# H4: root で走らせない。root は読み取り専用ファイルもディレクトリも素通りするので、
#     E9（4 枚のいずれかが読み取り専用）と I-13a 系のケースが「書けてしまった」側で
#     偶然緑になる。黙ってスキップすると網が無いことに気づけないので、明示的に落とす。
check_not_root() {
    local uid
    uid=$(id -u)
    if [ "${uid}" -eq 0 ]; then
        printf '      euid が 0（root）です。読み取り専用ケース（E9）の前提が崩れるので\n' >&2
        printf '      このスイートは root では走りません。一般ユーザで実行してください。\n' >&2
        return 1
    fi
    return 0
}

run_static "H4 not-root" check_not_root
if [ "${fail_count}" -ne 0 ]; then summary_and_exit; fi

# ------------------------------------------------------------------ 静的検査
# grep は実装差を避けるため BSD grep に固定する（mise.toml の verify:encoding と同じ理由）。

# S10: ファイル名リテラルが 8.3 準拠であること。`RANK30TMP.TXT` のような 8.3 違反は
#      Mac では普通に開けて DOS でだけ壊れるので、ホスト試験では静的検査だけが網になる。
#      対象は「全体が大文字英数字・ドット・大文字英数字」の文字列リテラル。
#      fopen/rename に渡る 13 本の名前はすべてこの形で書かれている。
#      長い文面の途中に埋まった名前は拾わないが、そちらは開かれないので実害がない。
#      あわせて新設 7 名が実際に現れることも見る（名前を決め損ねたまま緑になるのを防ぐ）。
check_names_8dot3() {
    local status lit base ext name
    status=0
    LC_ALL=C "${GREP}" -oE '"[A-Z0-9]+\.[A-Z0-9]+"' "${rank_c}" \
        | LC_ALL=C sort -u > "${work}/s10.lits" || true
    while IFS= read -r lit; do
        [ -n "${lit}" ] || continue
        lit="${lit#\"}"
        lit="${lit%\"}"
        base="${lit%%.*}"
        ext="${lit#*.}"
        if [ "${#base}" -gt 8 ] || [ "${#ext}" -gt 3 ]; then
            printf '      8.3 違反: "%s"（基幹 %d 文字・拡張子 %d 文字）\n' \
                "${lit}" "${#base}" "${#ext}" >&2
            status=1
        fi
    done < "${work}/s10.lits"
    for name in RANK30.TXT RANKWHO.TXT RANKTMP.30 RANKTMP.WHO \
                RANKOLD.30 RANKOLD.WHO NX93OLD.TXT; do
        if ! LC_ALL=C "${GREP}" -qF "\"${name}\"" "${rank_c}"; then
            printf '      新ファイル名 "%s" のリテラルがありません（spec §3.3）。\n' "${name}" >&2
            status=1
        fi
    done
    return "${status}"
}

# S11: 旧経路の記号が 1 つも残っていないこと。`#if 0` で囲って残すのを拒む。
#      台帳追記（rank_ledger_write / rank_json_escape）と経過日数規則
#      （RANK_PERIOD_DAYS）と追記用の空白緩衝（RANK_WS_MAX）は毎日更新で死ぬ。
#      spec §3.9「死んだコードは残さない」。
check_no_dead_symbols() {
    local sym status
    status=0
    for sym in rank_ledger_write rank_json_escape RANK_PERIOD_DAYS RANK_WS_MAX; do
        if LC_ALL=C "${GREP}" -nE "(^|[^A-Za-z0-9_])${sym}([^A-Za-z0-9_]|\$)" \
            "${rank_c}" > "${work}/s11.out"; then
            printf '      %s が残っています（削除されるべき記号）:\n' "${sym}" >&2
            head -5 "${work}/s11.out" >&2
            status=1
        fi
    done
    return "${status}"
}

# S12: 表型のオブジェクトを自動変数に置かないこと。DOS の既定スタックは約 2KB で、
#      RankWhoTable 1 個（約 8.7KB）で踏み抜く。Mac では 8MB スタックなので
#      何も起きず、静的検査だけが網になる（spec §3.9・I11）。
#      対象は「インデントされた行＝関数の内側」で、型名のすぐ後に識別子が来て
#      `;` で終わる宣言。ポインタ宣言と仮引数は識別子の位置に `*` や `)` が来るので
#      一致しない（ポインタは 2 バイトなので自動変数でよい）。
check_tables_not_automatic() {
    local decl='^[[:space:]]+((static|const|volatile|register|auto)[[:space:]]+)*'
    decl="${decl}(Rank30Table|RankWhoTable|RankTable)[[:space:]]+"
    decl="${decl}[A-Za-z_][A-Za-z0-9_]*[^;()]*;"
    if ! LC_ALL=C "${GREP}" -nE "${decl}" "${rank_c}" > "${work}/s12.out"; then
        # 表型の宣言が 1 つも無いのは実装前だけ。違反もゼロなので緑でよい。
        return 0
    fi
    if LC_ALL=C "${GREP}" -vE '^[0-9]+:[[:space:]]*static[[:space:]]' \
        "${work}/s12.out" > "${work}/s12.bad"; then
        printf '      static でない表型の関数内宣言（DOS のスタックを踏み抜く）:\n' >&2
        head -5 "${work}/s12.bad" >&2
        return 1
    fi
    return 0
}

# S6': 文字種判定を <ctype.h> に委ねないこと。既存 S6 の拡張で、決定 8（§3.8 の鍵規則）が
#      「英数字か」の判定を要求するため isalnum() に手を伸ばす誘因が新しく生まれる。
#      名前欄には 0x80 以上のバイトが入りうる。ANSI C ではこれらに EOF 以外の
#      負値を渡すのは未定義で、QuickC の実装は 128 要素表の引きなので実際に壊れる。
check_no_ctype_classify() {
    if LC_ALL=C "${GREP}" -nE \
        '(^|[^A-Za-z0-9_])(isalnum|isalpha|isdigit|isupper|islower)[[:space:]]*\(' \
        "${rank_c}" >&2; then
        return 1
    fi
    return 0
}

run_static "S10 names-8dot3"           check_names_8dot3
run_static "S11 no-dead-symbols"       check_no_dead_symbols
run_static "S12 tables-not-automatic"  check_tables_not_automatic
run_static "S6' no-ctype-classify"     check_no_ctype_classify

# -------------------------------------------------------------- スイート本体
# ハーネス側は gnu89 で組む（mkdir / chmod / dirent / dup2 を使うため）。RANK.C 自体の
# C89 厳密性は run.sh の S2 で別途担保されているので、ここで緩めても穴にならない。
#
# fixtures_daily.h の旧台帳リテラルは 581/601 バイトあり、C90 が要求する
# 文字列リテラル長の下限 509 を超える。-std=gnu89 には -Woverlength-strings が
# 入らないのでここは通る。超えてよいのはハーネスだけで、RANK.C 側は S2 が
# -std=c89 -pedantic-errors で押さえている。
#
# RANK_C で対象を差し替えられるようにするため、両ハーネスは
#     #ifndef RANK_C_PATH
#     #define RANK_C_PATH "../dist/RANK.C"
#     #endif
#     #include RANK_C_PATH
# の形で取り込む。ここから絶対パスを -D で渡す。
HARNESS="-x c -std=gnu89 -Wall -Wextra -Werror -g"
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

# H2: test_daily.c のビルド失敗を「名前の付いた 1 件の失敗」として数える。
#     黙って通すと以降のケースが 1 件も走らないまま緑になる（既存 H1 と同型）。
# shellcheck disable=SC2086
if ! clang ${HARNESS} ${SAN} \
      -DRANK_NO_MAIN -DRANK_TEST_CLOCK -DRANK_TEST_FAULT \
      "-DRANK_C_PATH=\"${rank_c}\"" \
      "${here}/test_daily.c" -o "${work}/test_daily"; then
    report_fail "H2 daily-harness-build: test_daily.c がビルドできません。"
    summary_and_exit
fi
report_ok "H2 daily-harness-build"

# H3: test_cli.c も同じ扱い。こちらは `#define main rank_entry` で本物の main を
#     取り込むので RANK_NO_MAIN を渡さない（argc/argv/usage/終了コードを通す）。
# shellcheck disable=SC2086
if ! clang ${HARNESS} ${SAN} \
      -DRANK_TEST_CLOCK -DRANK_TEST_FAULT \
      "-DRANK_C_PATH=\"${rank_c}\"" \
      "${here}/test_cli.c" -o "${work}/test_cli"; then
    report_fail "H3 cli-harness-build: test_cli.c がビルドできません。"
    summary_and_exit
fi
report_ok "H3 cli-harness-build"

# タイムゾーンを変えて同じ結果になることを要求する。暦境界の計算に mktime や
# localtime が紛れ込むと、夏時間の切替を跨いだ日付が 1 日ずれて跨ぎ判定が飛ぶ。
# UTC / 夏時間なし / 夏時間ありの 3 本で不変性を確かめる（run.sh と同じ 3 本）。
round=0
for tz in UTC Asia/Tokyo America/Los_Angeles; do
    round=$((round + 1))
    for bin in test_daily test_cli; do
        scratch="${work}/${bin}-run${round}"
        ran=0
        if [ -n "${filter}" ]; then
            TZ="${tz}" LC_ALL=C "${work}/${bin}" "${scratch}" "${filter}" || ran=1
        else
            TZ="${tz}" LC_ALL=C "${work}/${bin}" "${scratch}" || ran=1
        fi
        if [ "${ran}" -eq 0 ]; then
            report_ok "${bin} TZ=${tz}"
        else
            report_fail "${bin} TZ=${tz} で失敗しました。"
        fi
    done
done

if [ "${fail_count}" -ne 0 ]; then summary_and_exit; fi
printf '\nすべて成功（%d 件）。\n' "${ok_count}"
