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
    ia16-elf-gcc "${CFLAGS[@]}" -o RANK.EXE rank.c

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

# 一時ディレクトリで成功を確かめたものだけを dist へ出し、直後に由来を記録する。
mv "${work}/RANK.EXE" "${rank_exe}"
printf '%s\n' "${source_sum}" >"${stamp}"

printf 'dist/RANK.EXE を作りました（%s バイト、RANK.C = %s）。\n' \
    "$(wc -c <"${rank_exe}" | tr -d ' ')" "${source_sum:0:12}"
