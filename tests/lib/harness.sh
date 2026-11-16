#!/usr/bin/env bash
#
# 98hdd 複数カード対応 — 試験ハーネス。
#
# 各スイート（unit.sh / integration.sh / cli.sh）が source する。
# 提供するもの:
#
#   表明        assert_eq / assert_ne / assert_contains / assert_lacks
#               assert_exit / assert_file / assert_nofile / assert_same_bytes
#   砂場        scratch_new   HOME を差し替えた隔離ディレクトリを作る
#   イメージ    make_image / make_bad_sig / make_fat32 / make_truncated
#   独立実装    read_serial_ref   被試験対象とは別に書いたシリアル読み取り
#   観測        fake_mtools_install / mtools_log_lines / mtools_log_specs
#
# 依存は bash・mformat・mdir・mcopy・mmd・od・shasum・dd のみ。
# ネットワークも追加インストールも使わない。
#
# このファイルは set -e を敷かない。表明が失敗しても走り続ける必要がある。

# ------------------------------------------------------------ 本物の道具の位置
# 偽 mtools を PATH 先頭に置くので、通過させる版が本物を見つけられるように
# 絶対パスを先に捕まえる。PATH から探し直すと自分自身を呼んで無限に回る。
HARNESS_REAL_MDIR=$(command -v mdir || true)
HARNESS_REAL_MCOPY=$(command -v mcopy || true)
HARNESS_REAL_MMD=$(command -v mmd || true)
HARNESS_REAL_MFORMAT=$(command -v mformat || true)

# 実機と同じオフセットを既定にする。0 を既定にすると、@@ を落とす欠陥が
# 試験を通ってしまう。
HARNESS_OFFSET=360448

# FAT16 になる最小に近い総セクタ数。-T 16384 だと FAT12 になり、
# クラスタが小さいぶん FAT 表が巨大化して実消費がかえって増える（実測 9.5 MB）。
HARNESS_TOTAL_SECTORS=32768

# ---------------------------------------------------------------------- 表明
h_ok_count=0
h_fail_count=0
h_verbose=${HARNESS_VERBOSE:-0}

h_pass() {
    h_ok_count=$((h_ok_count + 1))
    if [ "${h_verbose}" -eq 1 ]; then printf 'ok    %s\n' "$1"; fi
}

h_fail() {
    h_fail_count=$((h_fail_count + 1))
    printf 'FAIL  %s\n' "$1" >&2
    shift
    while [ $# -gt 0 ]; do printf '        %s\n' "$1" >&2; shift; done
}

assert_eq() {           # assert_eq <label> <want> <got>
    if [ "$2" = "$3" ]; then h_pass "$1"
    else h_fail "$1" "want: [$2]" "got : [$3]"; fi
}

assert_ne() {           # assert_ne <label> <unwanted> <got>
    if [ "$2" != "$3" ]; then h_pass "$1"
    else h_fail "$1" "この値であってはならない: [$2]"; fi
}

assert_contains() {     # assert_contains <label> <needle> <haystack>
    case "$3" in
        *"$2"*) h_pass "$1" ;;
        *) h_fail "$1" "含むべき: [$2]" "全体: [$3]" ;;
    esac
}

assert_lacks() {        # assert_lacks <label> <needle> <haystack>
    case "$3" in
        *"$2"*) h_fail "$1" "含んではならない: [$2]" "全体: [$3]" ;;
        *) h_pass "$1" ;;
    esac
}

assert_exit() {         # assert_exit <label> <want> <got>
    if [ "$2" = "$3" ]; then h_pass "$1"
    else h_fail "$1" "終了コード want: $2  got: $3"; fi
}

assert_file() {         # assert_file <label> <path>
    if [ -f "$2" ]; then h_pass "$1"
    else h_fail "$1" "在るべきファイルが無い: $2"; fi
}

assert_nofile() {       # assert_nofile <label> <path>
    if [ -e "$2" ]; then h_fail "$1" "在ってはならない: $2"
    else h_pass "$1"; fi
}

assert_same_bytes() {   # assert_same_bytes <label> <path-a> <path-b>
    local a b
    a=$(shasum -a 256 "$2" 2>/dev/null | /usr/bin/awk '{print $1}')
    b=$(shasum -a 256 "$3" 2>/dev/null | /usr/bin/awk '{print $1}')
    if [ -n "$a" ] && [ "$a" = "$b" ]; then h_pass "$1"
    else h_fail "$1" "バイト一致しない" "$2: ${a:-読めない}" "$3: ${b:-読めない}"; fi
}

# ------------------------------------------------------------------- 砂場
# HOME を差し替えて、設定・hdd/・send/ をまとめて隔離する。98hdd.sh の
# BASE_DIR と CONFIG_FILE は ${HOME} からのみ派生するので、これで足りる。
#
# 呼び出し側は戻り値のパスを使う。HOME の差し替えは run_98hdd が行うので、
# ハーネス自身のプロセスの HOME は変えない（変えると shasum などが
# 設定を探しに行く先が変わり、試験の外に影響が出る）。
scratch_new() {         # scratch_new → パスを stdout へ
    local d
    d=$(mktemp -d "${TMPDIR:-/tmp}/98hdd-cards.XXXXXXXX") || return 1
    mkdir -p "${d}/home" "${d}/bin" "${d}/img"
    printf '%s\n' "$d"
}

scratch_drop() {        # scratch_drop <dir>
    case "$1" in
        */98hdd-cards.*) rm -rf "$1" ;;
        *) printf 'scratch_drop: 消す先が砂場に見えない: %s\n' "$1" >&2; return 1 ;;
    esac
}

# ------------------------------------------------------- 試験用の HDD イメージ
# 実機と同じ「オフセットの先に FAT16 パーティションが 1 つ」の形を作る。
# スパースで作ってから mformat するので、実消費は FAT 表のぶんだけ（約 496 KB）。
make_image() {          # make_image <path> <serial8hex>
    local img="$1" serial="$2"
    local bytes=$(( HARNESS_OFFSET + HARNESS_TOTAL_SECTORS * 512 + 1048576 ))
    dd if=/dev/zero of="$img" bs=1 count=0 seek="$bytes" 2>/dev/null || return 1
    MTOOLS_SKIP_CHECK=1 "$HARNESS_REAL_MFORMAT" \
        -i "${img}@@${HARNESS_OFFSET}" \
        -T "$HARNESS_TOTAL_SECTORS" -N "$serial" :: 2>/dev/null
}

# 拡張ブート署名が 0x29 でないイメージ。mformat を通さないのでゼロのまま。
# 仕様 E9 の入力。
make_bad_sig() {        # make_bad_sig <path>
    local bytes=$(( HARNESS_OFFSET + HARNESS_TOTAL_SECTORS * 512 ))
    dd if=/dev/zero of="$1" bs=1 count=0 seek="$bytes" 2>/dev/null
}

# FAT32 イメージ。mformat の -F は FAT32 を強制する（FAT16 の指定ではない）。
# FAT32 は署名を 0x42、シリアルを 0x43 に置くので、0x26 は FATSz32 の上位
# ワード（通常 0x00）になる。PC-98 の DOS は FAT32 を扱えないので、
# これが E9 で弾かれるのは正しい振る舞いである。
make_fat32() {          # make_fat32 <path> <serial8hex>
    local img="$1" serial="$2"
    local bytes=$(( HARNESS_OFFSET + 80 * 1024 * 1024 ))
    dd if=/dev/zero of="$img" bs=1 count=0 seek="$bytes" 2>/dev/null || return 1
    MTOOLS_SKIP_CHECK=1 "$HARNESS_REAL_MFORMAT" \
        -i "${img}@@${HARNESS_OFFSET}" -F \
        -T $(( 80 * 1024 * 1024 / 512 )) -N "$serial" :: 2>/dev/null
}

# オフセットより短いファイル。署名を読もうとすると 0 バイト返る。
make_truncated() {      # make_truncated <path>
    dd if=/dev/zero of="$1" bs=1 count=16 2>/dev/null
}

# ------------------------------------------- シリアル読み取り（独立実装）
# 被試験対象の read_serial とは別に書く。同じコードを両側に置くと、
# 誤りが両側で打ち消し合って何も検証しないことになる。
#
# 失敗（署名が 0x29 でない／読めない）なら 1 を返し、何も出さない。
read_serial_ref() {     # read_serial_ref <img> <off> → XXXX-XXXX
    local img="$1" off="$2" sig b1 b2 b3 b4
    sig=$(LC_ALL=C od -An -j $(( off + 38 )) -N 1 -t x1 "$img" 2>/dev/null \
          | /usr/bin/awk 'NR==1 {print $1}')
    [ "$sig" = "29" ] || return 1
    read -r b1 b2 b3 b4 <<EOF
$(LC_ALL=C od -An -j $(( off + 39 )) -N 4 -t x1 "$img" 2>/dev/null \
  | /usr/bin/awk 'NR==1 {print $1, $2, $3, $4}')
EOF
    [ -n "$b4" ] || return 1
    printf '%s%s-%s%s\n' \
        "$(printf '%s' "$b4" | /usr/bin/tr 'a-f' 'A-F')" \
        "$(printf '%s' "$b3" | /usr/bin/tr 'a-f' 'A-F')" \
        "$(printf '%s' "$b2" | /usr/bin/tr 'a-f' 'A-F')" \
        "$(printf '%s' "$b1" | /usr/bin/tr 'a-f' 'A-F')"
}

# ------------------------------------------------------------- 偽 mtools
# 「呼び出しが起きなかったこと」を見るために要る。結果の状態を見るだけでは
# 「照合前に何も読み書きしない」（仕様 I1）を張れない。
#
# 2 形ある:
#   遮る版   (passthrough=0) 記録だけして終了 0。本物は呼ばない
#   通す版   (passthrough=1) 記録してから本物へ exec する
fake_mtools_install() { # fake_mtools_install <bindir> <logfile> <passthrough 0|1>
    local bindir="$1" log="$2" pass="$3" name real
    mkdir -p "$bindir"
    : > "$log"
    for name in mdir mcopy mmd mformat; do
        case "$name" in
            mdir)    real="$HARNESS_REAL_MDIR" ;;
            mcopy)   real="$HARNESS_REAL_MCOPY" ;;
            mmd)     real="$HARNESS_REAL_MMD" ;;
            mformat) real="$HARNESS_REAL_MFORMAT" ;;
        esac
        {
            printf '#!/usr/bin/env bash\n'
            printf 'printf "%%s" "%s" >> %s\n' "$name" "$(h_shq "$log")"
            printf 'for a in "$@"; do printf " %%s" "$a" >> %s; done\n' "$(h_shq "$log")"
            printf 'printf "\\n" >> %s\n' "$(h_shq "$log")"
            if [ "$pass" = "1" ]; then
                printf 'exec %s "$@"\n' "$(h_shq "$real")"
            else
                printf 'exit 0\n'
            fi
        } > "${bindir}/${name}"
        chmod +x "${bindir}/${name}"
    done
}

h_shq() { printf "'%s'" "$(printf '%s' "$1" | /usr/bin/sed "s/'/'\\\\''/g")"; }

mtools_log_lines() {    # mtools_log_lines <logfile> → 行数
    if [ -f "$1" ]; then /usr/bin/wc -l < "$1" | /usr/bin/tr -d ' '
    else printf '0\n'; fi
}

# -i に渡された綴りを重複なく並べる。1 回の起動が触るイメージが 1 つだけで
# あること（仕様 I3）を見るのに使う。
mtools_log_specs() {    # mtools_log_specs <logfile>
    [ -f "$1" ] || return 0
    LC_ALL=C /usr/bin/awk '{
        for (i = 1; i < NF; i++) if ($i == "-i") print $(i + 1)
    }' "$1" | sort -u
}

# --------------------------------------------------------- 98hdd の起動
# HOME を砂場へ向け、PATH 先頭に偽 mtools を置いて起動する。
# 標準出力と標準エラーをまとめて捕り、終了コードを h_last_status に残す。
h_last_status=0
h_last_output=""

run_98hdd() {           # run_98hdd <scratch> <args...>
    local scratch="$1"; shift
    local out
    out=$(cd "$scratch" && HOME="${scratch}/home" \
          PATH="${scratch}/bin:${PATH}" \
          MTOOLS_LOG="${scratch}/mtools.log" \
          bash "${HARNESS_SUT}" "$@" 2>&1)
    h_last_status=$?
    h_last_output="$out"
    printf '%s' "$out"
}
