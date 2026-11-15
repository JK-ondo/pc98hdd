#!/usr/bin/env bash
#
# NyaHaX'93 weekly ranking reset - RANK.EXE を Mac 上でビルドする。
#
#   ./build.sh          dist/RANK.EXE を作り直す
#   ./build.sh --check  ビルドせず、dist/RANK.EXE が RANK.C と同期しているかだけ見る
#
# ia16-elf-gcc を Docker 経由で呼ぶ。実機に QuickC を叩きに行かなくても
# RANK.EXE が得られる。QuickC 2.00 でのビルド（98 上で QCL RANK.C）は
# 保険として残してあり、RANK.C はどちらでも通る。
#
set -euo pipefail

# イメージはダイジェストで固定する。:latest が動いてビルド結果が黙って
# 変わるのを防ぐため。タグは ikkepop/ia16-tools:latest（ia16-elf-gcc 6.3.0）。
IMAGE_DIGEST=sha256:795d6631ad8852f433df18957831757d2bdb502299b75b269960fdc6f4c684d6
IMAGE="ikkepop/ia16-tools@${IMAGE_DIGEST}"

# small モデル（MZ 実行形式）。tiny でも 64KB に収まるが、RANK.EXE という名前は
# GAME.BAT と README が既に参照しているので変えない。
#
# ソースは小文字の rank.c へ写してから渡す。gcc は拡張子 .C を C++ とみなし、
# そうなると -std=c89 が cc1plus に無視されて C89 検査が効かなくなる（実測）。
#
# DOS 判定用の -D は要らない。ia16-elf-gcc は __MSDOS__ と _DOS を定義し、
# RANK.C はその綴りを見る（tests/run.sh の S8）。
CFLAGS=(-x c -std=c89 -pedantic -Wall -Wextra -Werror -Os -mcmodel=small)

# 静的領域（.data + .bss）に許す上限。small モデルでは初期化済みデータ・
# 文字列リテラル・.bss が 1 本の 64 KiB セグメント（DGROUP）に同居し、
# そこへスタックも載る。半分の 32 KiB までを静的領域に充て、残りをスタック
# （DOS では 2 KB 弱）と余白に残す。超えてもリンクは通り DOS は起動させるので、
# 実機では深い所でスタックを書き潰す形で静かに壊れる。ここで測って止める。
DGROUP_STATIC_MAX=32768

# 静的領域の実測はリンカの map から読む。コンテナ内の作業ディレクトリへ出るので
# dist には残らず、作業ディレクトリごと捨てられる。
MAP_FILE=rank.map
LDFLAGS=(-Wl,-Map="${MAP_FILE}")

here=$(cd -- "$(dirname -- "$0")" && pwd)
rank_c="${here}/dist/RANK.C"
rank_exe="${here}/dist/RANK.EXE"
stamp="${here}/.built-from"   # dist の外。dist の中身はすべて 98 へ送られる

usage() { /usr/bin/sed -n '3,10p' "$0"; }

check_only=0
case "${1:-}" in
    --check) check_only=1 ;;
    -h|--help) usage; exit 0 ;;
    "") ;;
    *) printf 'unknown option: %s\n' "$1" >&2; exit 2 ;;
esac

[ -f "${rank_c}" ] || { printf '%s がありません。\n' "${rank_c}" >&2; exit 1; }
source_sum=$(shasum -a 256 "${rank_c}" | cut -d' ' -f1)

# ---------------------------------------------------------------- --check 経路
# docker を使わない。RANK.EXE が未ビルドなら何も言わずに通し、古いときだけ落ちる。
# 「送る物が原本より古い」ほうだけが事故になるので、そちら側に閉じる。
if [ "${check_only}" -eq 1 ]; then
    if [ ! -f "${rank_exe}" ]; then
        printf 'RANK.EXE は未ビルドです（%s を実行すると作られます）。\n' "${here}/build.sh"
        exit 0
    fi
    if [ ! -f "${stamp}" ]; then
        printf 'RANK.EXE があるのに %s がありません。由来が追えないので作り直してください。\n' \
            "${stamp}" >&2
        exit 1
    fi
    if [ "$(cat "${stamp}")" != "${source_sum}" ]; then
        printf 'dist/RANK.EXE は今の dist/RANK.C から作られたものではありません。\n' >&2
        printf '  RANK.C   : %s\n' "${source_sum}" >&2
        printf '  RANK.EXE : %s（%s の記録）\n' "$(cat "${stamp}")" "${stamp}" >&2
        printf '%s を実行して作り直してください。古いまま 98 へ送られます。\n' \
            "${here}/build.sh" >&2
        exit 1
    fi
    printf 'dist/RANK.EXE は dist/RANK.C と同期しています。\n'
    exit 0
fi

# ------------------------------------------------------------------ ビルド経路
command -v docker >/dev/null 2>&1 || {
    printf 'docker がありません。98 上で QCL RANK.C を使ってください。\n' >&2
    exit 1
}

work=$(mktemp -d "${TMPDIR:-/tmp}/nx93rank-build.XXXXXXXX")
trap 'rm -rf "${work}"' EXIT
trap 'rm -rf "${work}"; exit 130' HUP INT TERM

cp "${rank_c}" "${work}/rank.c"

# イメージは amd64 のみ。arm64 の Mac では Rosetta/QEMU 経由で動くので明示する。
docker run --rm --platform linux/amd64 \
    -v "${work}:/work" -w /work "${IMAGE}" \
    ia16-elf-gcc "${CFLAGS[@]}" "${LDFLAGS[@]}" -o RANK.EXE rank.c

[ -f "${work}/RANK.EXE" ] || {
    printf 'RANK.EXE が生成されませんでした。\n' >&2
    exit 1
}

# MZ ヘッダを確かめる。-mcmodel が効かず tiny（フラットイメージ）が出ていると
# 拡張子だけ .EXE の .COM になり、DOS がヘッダ無しの EXE として扱って暴走する。
magic=$(head -c 2 "${work}/RANK.EXE" | xxd -p)
[ "${magic}" = "4d5a" ] || {
    printf 'MZ ヘッダがありません（先頭 2 バイト = %s）。small モデルで出ていません。\n' \
        "${magic}" >&2
    exit 1
}

# ------------------------------------------------ 静的領域（.data + .bss）の検査
# GNU ld の map は出力セクションを行頭の「名前 アドレス 大きさ」で書き、入力
# セクションは字下げして書く。行頭に固定すれば出力セクションだけを拾える。
# MS-DOS 向けのリンカスクリプトは .rodata を .data の中へ畳むので、文字列
# リテラルもこの 2 つのセクションで数え切れる。
#
# 測れなかったときは通さない。「測れないので OK」にすると、上限が守られていると
# 誤解させるぶん、検査が無いより悪くなる。
map_file="${work}/${MAP_FILE}"
section_bytes=0

# $1 = 出力セクション名。大きさを 10 進で section_bytes へ入れる。1 行に
# 定まらなければ落ちる（形が読めていない証拠なので、推測で数えない）。
read_section_bytes() {
    local name="$1" sizes count
    sizes=$(LC_ALL=C /usr/bin/awk -v want="${name}" '
        $1 == want && index($0, want) == 1 &&
        $2 ~ /^0x[0-9A-Fa-f]+$/ && $3 ~ /^0x[0-9A-Fa-f]+$/ { print $3 }
    ' "${map_file}")
    count=$(printf '%s\n' "${sizes}" | LC_ALL=C /usr/bin/awk 'NF { n++ } END { print n + 0 }')
    if [ "${count}" -ne 1 ]; then
        printf '%s から出力セクション %s の大きさを読めません（該当する行 %s 本）。\n' \
            "${MAP_FILE}" "${name}" "${count}" >&2
        printf 'map にある出力セクションは次のとおりです。\n' >&2
        LC_ALL=C /usr/bin/awk '
            /^\./ && $2 ~ /^0x[0-9A-Fa-f]+$/ && $3 ~ /^0x[0-9A-Fa-f]+$/ {
                printf "  %-16s %s\n", $1, $3
            }' "${map_file}" >&2
        printf 'リンカの出力の形が変わっています。%s の静的領域検査を直してください。\n' \
            "${here}/build.sh" >&2
        exit 1
    fi
    section_bytes=$((sizes))
}

[ -f "${map_file}" ] || {
    printf '%s が出ていないので静的領域を測れません（リンカが -Map を受けていません）。\n' \
        "${MAP_FILE}" >&2
    exit 1
}

read_section_bytes .data
data_bytes="${section_bytes}"
read_section_bytes .bss
bss_bytes="${section_bytes}"
static_bytes=$((data_bytes + bss_bytes))

# small モデルでは .text が独立したコードセグメントに載り、.data と .bss が 1 本の
# データセグメント（上限 DGROUP_STATIC_MAX）に同居する。文字列リテラルはこのリンカ
# スクリプトが .data の中に畳むので .data + .bss で数え切れる（.rodata という出力
# セクションは現れない。2026-11-15 に map で実測）。
#
# この前提が崩れた形、つまりデータセグメントに載りうるのに数えていない出力セクションが
# 居る map では、.data と .bss を足しても静的領域の全部にならない。そのまま通すと
# 「上限内」と表示しながら数 KB を数え落とすので、ここで落とす。黙って少なく数える
# より、未知のレイアウトで止まったほうが直せる。
#
# 除外するのは、データセグメントに載らないと分かっているものだけ:
#   .text          コードセグメント。small モデルではデータと別に載る
#   .msdos_mz_hdr  .EXE 先頭の MZ ヘッダ 32 バイト。DOS が読み込み量と再配置を
#                  知るための見出しで、どのセグメントにも載らない
DGROUP_EXEMPT_SECTIONS=".text .msdos_mz_hdr"

unknown=$(LC_ALL=C /usr/bin/awk \
    -v counted=" ${DGROUP_EXEMPT_SECTIONS} .data .bss " '
    /^\./ && $2 ~ /^0x[0-9A-Fa-f]+$/ && $3 ~ /^0x[0-9A-Fa-f]+$/ &&
    index(counted, " " $1 " ") == 0 && $3 != "0x0" {
        printf "  %-16s %s\n", $1, $3
    }' "${map_file}")

if [ -n "${unknown}" ]; then
    printf '%s に想定外の出力セクションがあります。\n' "${MAP_FILE}" >&2
    printf '%s\n' "${unknown}" >&2
    printf '.data + .bss だけでは静的領域を数え切れないので止めました。\n' >&2
    printf 'そのセクションがデータセグメントに載るなら合計に足し、載らないなら\n' >&2
    printf '%s の除外リストに加えてください。\n' "${here}/build.sh" >&2
    exit 1
fi

[ "${static_bytes}" -gt 0 ] || {
    printf '静的領域の合計が 0 バイトになりました。RANK.C は static な表を持つので\n' >&2
    printf 'ありえません。map の読み取りが壊れています。%s を直してください。\n' \
        "${here}/build.sh" >&2
    exit 1
}

if [ "${static_bytes}" -gt "${DGROUP_STATIC_MAX}" ]; then
    printf '静的領域が上限を超えました（.data %s ＋ .bss %s ＝ %s ＞ %s バイト）。\n' \
        "${data_bytes}" "${bss_bytes}" "${static_bytes}" "${DGROUP_STATIC_MAX}" >&2
    printf 'static な表を小さくするか、メッセージの文面を短くしてください。\n' >&2
    printf 'このまま 98 へ送ると、起動はしてから深い所で書き潰します。\n' >&2
    exit 1
fi

printf '静的領域: .data %s ＋ .bss %s ＝ %s バイト（上限 %s の %s%%）。\n' \
    "${data_bytes}" "${bss_bytes}" "${static_bytes}" "${DGROUP_STATIC_MAX}" \
    "$((static_bytes * 100 / DGROUP_STATIC_MAX))"

# 一時ディレクトリで成功を確かめたものだけを dist へ出し、直後に由来を記録する。
mv "${work}/RANK.EXE" "${rank_exe}"
printf '%s\n' "${source_sum}" >"${stamp}"

printf 'dist/RANK.EXE を作りました（%s バイト、RANK.C = %s）。\n' \
    "$(wc -c <"${rank_exe}" | tr -d ' ')" "${source_sum:0:12}"
