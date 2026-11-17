#!/bin/bash
# =============================================================================
# 98hdd — PC-98 HDDイメージ操作ツール
# =============================================================================

set -euo pipefail

HDD_IMAGE="/Volumes/x/HD10_512 200MB.hda"
HDD_OFFSET=360448
# HDD_DRIVE はもう使わない。mtools のドライブ文字（~/.mtoolsrc の drive p:）
# ではなく、-i "<イメージ>@@<オフセット>" でイメージを直に指すようになったため。
# 公開済みの ~/.98hddrc に残っていてもエラーにしないので、読み込みは許す。
HDD_DRIVE="p"
BASE_DIR="${HOME}/9801"
SEND_DIR="${BASE_DIR}/send"
PULL_DIR="${BASE_DIR}/hdd"

CONFIG_FILE="${HOME}/.98hddrc"
if [[ -f "$CONFIG_FILE" ]]; then source "$CONFIG_FILE"; fi


RED='\033[0;31m'; YEL='\033[0;33m'; GRN='\033[0;32m'; CYN='\033[0;36m'; RST='\033[0m'
info()  { echo -e "${CYN}[INFO]${RST}  $*"; }
ok()    { echo -e "${GRN}[OK]${RST}    $*"; }
warn()  { echo -e "${YEL}[WARN]${RST}  $*"; }
error() { echo -e "${RED}[ERROR]${RST} $*" >&2; }
die()   { error "$*"; exit 1; }

check_deps() {
    for cmd in mdir mcopy mmd nkf file iconv; do
        command -v "$cmd" &>/dev/null || die "コマンドが見つかりません: $cmd"
    done
}

run_mtools() { MTOOLS_SKIP_CHECK=1 "$@"; }

# ── カード（SD カード＝HDDイメージ）の定義と同定 ────────────────────────────
#
# 複数のカードを挿し替えて使う。98 が同時に認識するのは 1 枚のままなので、
# このツールも 1 回の起動で 1 枚だけを扱う。
#
# 同定は FAT のボリュームシリアル番号で行う。カードではなくイメージを同定する
# ので、読み書きの対象と粒度が一致する。mtools を起動せず、イメージ全体を
# 走査せずに読める。
CARDS_DIR="${HOME}/.98hdd/cards"

# FAT12/16 のブートセクタ内の位置。FAT32 は署名を 0x42、シリアルを 0x43 に
# 置くので、この 2 つで読むと FAT32 は署名が合わずに弾かれる。PC-98 の DOS は
# FAT32 を扱えないので、それが正しい振る舞いである。
#
# **この 2 つが位置の唯一の出所。** 2 箇所に散ると片方だけ直す欠陥が入る。
CARD_SIG_OFFSET=38          # 0x26 拡張ブート署名（0x29 でなければシリアル欄は無効）
CARD_SERIAL_OFFSET=39       # 0x27 シリアル 4 バイト（リトルエンディアン）

# 名前は 1 文字目が [A-Za-z0-9]、2 文字目以降が [A-Za-z0-9_-]。
# 1 文字目からハイフンを除くのは、名前が選択肢に化けるのを防ぐため
# （98hdd -c -x pull のような呼び方を生まない）。
# LC_ALL=C を明示するのは、UTF-8 ロケールでは A-Za-z の範囲が
# アクセント付き文字まで拾いうるため。
valid_card_name() {
    printf '%s' "${1:-}" | LC_ALL=C grep -q '^[A-Za-z0-9][A-Za-z0-9_-]*$'
}

# ブートセクタからボリュームシリアル番号を読む。XXXX-XXXX を標準出力へ。
# 署名が 0x29 でなければ何も出さずに 1 を返す。
#
# 確かめずに 4 バイト読むと、無関係なバイトをシリアルとして扱い、たまたま
# 一致すれば別のイメージへ書く。オフセットが FAT の先頭を指していない場合が
# まさにそれなので、fail closed にする。
read_serial() {             # read_serial <image> <offset>
    local img="${1:-}" off="${2:-}" sig serial
    [[ -f "$img" ]] || return 1
    [[ "$off" =~ ^[0-9]+$ ]] || return 1

    sig=$(LC_ALL=C od -An -j $(( off + CARD_SIG_OFFSET )) -N 1 -t x1 "$img" \
          2>/dev/null | LC_ALL=C /usr/bin/awk 'NR == 1 { print $1 }') || true
    [[ "$sig" == "29" ]] || return 1

    # NF == 4 を要求するのは、オフセットより短いファイルを弾くため。
    serial=$(LC_ALL=C od -An -j $(( off + CARD_SERIAL_OFFSET )) -N 4 -t x1 "$img" \
             2>/dev/null | LC_ALL=C /usr/bin/awk 'NR == 1 && NF == 4 {
                 printf "%s%s-%s%s", toupper($4), toupper($3), toupper($2), toupper($1)
             }') || true
    [[ -n "$serial" ]] || return 1
    printf '%s\n' "$serial"
}

# ブートセクタの署名をそのまま返す（E9 の文面に実測値を載せるため）。
read_boot_signature() {     # read_boot_signature <image> <offset>
    local img="${1:-}" off="${2:-}" sig
    [[ -f "$img" ]] || return 1
    # read_serial と同じ番兵。数値でないと算術展開が set -u 下で
    # bash の生エラーを利用者の画面へ漏らす。
    [[ "$off" =~ ^[0-9]+$ ]] || { printf '読めない\n'; return 1; }
    sig=$(LC_ALL=C od -An -j $(( off + CARD_SIG_OFFSET )) -N 1 -t x1 "$img" \
          2>/dev/null | LC_ALL=C /usr/bin/awk 'NR == 1 { print $1 }') || true
    printf '%s\n' "${sig:-読めない}"
}

# ディレクトリの実パス。存在しない末尾は文字列のまま残す。
# シンボリックリンク経由の衝突（仕様 E5）を捕まえるのに要る。片方だけ
# 解決すると見逃す。
real_dir() {                # real_dir <path>
    local p="${1:-}" tail=""
    while [[ -n "$p" && "$p" != "/" && ! -d "$p" ]]; do
        tail="/$(basename "$p")${tail}"
        p=$(dirname "$p")
    done
    if [[ -d "$p" ]]; then printf '%s%s\n' "$(cd "$p" && pwd -P)" "$tail"
    else printf '%s%s\n' "$p" "$tail"; fi
}

# カードの定義を読む。1 枚 1 行で name|image|offset|serial|pull_dir を出す。
#
# bash 3.2 に連想配列が無いので行指向にしてある。並行する添字配列は、
# 枚数と添字の対応を手で保つ必要があり、1 箇所ずれると別のカードの IMAGE に
# 別のカードの SERIAL を当てる経路ができる。
load_cards() {
    local f name count=0

    if [[ -d "$CARDS_DIR" ]]; then
        for f in "${CARDS_DIR}"/*.conf; do
            [[ -e "$f" ]] && count=$((count + 1))
        done
    fi

    # 定義が 1 枚も無ければ ~/.98hddrc から暗黙の default を組み立てる。
    # PULL_DIR は従来どおりの値をそのまま使う（hdd/default にはしない）。
    # 既存利用者のミラーを動かさないため。SERIAL が空なので照合もしない。
    if (( count == 0 )); then
        printf '%s|%s|%s|%s|%s\n' \
            "default" "$HDD_IMAGE" "$HDD_OFFSET" "" "$PULL_DIR"
        return 0
    fi

    local lines="" seen=""
    for f in "${CARDS_DIR}"/*.conf; do
        [[ -e "$f" ]] || continue
        name=$(basename "$f" .conf)
        if ! valid_card_name "$name"; then
            error "カード定義の名前に使えない文字があります: ${name}.conf"
            error "  1 文字目は英数字、2 文字目以降は英数字・_・- だけです。"
            return 1
        fi

        # **サブシェルで source する。** 親で source すると変数が次のカードへ
        # 漏れる。1 枚目に SERIAL があって 2 枚目に無い場合、2 枚目が 1 枚目の
        # 値を継いで必須変数の検査をすり抜ける。
        local got
        got=$( IMAGE=""; OFFSET=""; SERIAL=""; PULL_DIR=""
               # shellcheck source=/dev/null
               . "$f" >/dev/null 2>&1
               printf '%s|%s|%s|%s' "$IMAGE" "$OFFSET" "$SERIAL" "$PULL_DIR" ) || true

        # **分割する前に区切り文字の混入を見る。** 分割してしまうと値がずれ、
        # 「OFFSET が数値でない」のような無関係な文面になって誤りの在りかを
        # 隠す（実測で確認した）。4 欄なので | はちょうど 3 個のはず。
        local pipes
        pipes=$(printf '%s' "$got" | LC_ALL=C /usr/bin/tr -cd '|' | /usr/bin/wc -c \
                | /usr/bin/tr -d ' ')
        if [[ "$pipes" != "3" ]]; then
            error "カード ${name} の設定値に | が含まれています。"
            error "  この文字はカード情報の区切りに使っているので扱えません。"
            error "  IMAGE か PULL_DIR のパスを変えてください。"
            error "  ${f} を見てください。"
            return 1
        fi

        local c_img c_off c_ser c_pull
        IFS='|' read -r c_img c_off c_ser c_pull <<< "$got"

        local missing=""
        [[ -n "$c_img" ]] || missing="${missing} IMAGE"
        [[ -n "$c_off" ]] || missing="${missing} OFFSET"
        [[ -n "$c_ser" ]] || missing="${missing} SERIAL"
        if [[ -n "$missing" ]]; then
            error "カード ${name} の定義に足りない変数があります:${missing}"
            error "  ${f} を見てください。"
            return 1
        fi

        # **有無だけでなく形も確かめる。** 形が違うと下流で別の症状に化けて
        # 出る。実測した 3 通り: 数値でない OFFSET は算術展開が set -u 下で
        # bash の生エラーを利用者の画面に漏らす。書式の違う SERIAL は
        # 「カードが違う」と報告されて誤りの在りかを隠す。パスに | が入ると
        # 行の区切りと衝突して値が切れ、「イメージが無い」ことになる。
        if [[ ! "$c_off" =~ ^[0-9]+$ ]]; then
            error "カード ${name} の OFFSET が数値ではありません: ${c_off}"
            error "  FAT パーティション先頭へのバイト数を書いてください（例 360448）。"
            error "  ${f} を見てください。"
            return 1
        fi
        if [[ ! "$c_ser" =~ ^[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}$ ]]; then
            error "カード ${name} の SERIAL の書式が違います: ${c_ser}"
            error "  XXXX-XXXX の形（16 進 4 桁・ハイフン・16 進 4 桁）で書いてください。"
            error "  今挿さっているカードの値は 98hdd card show で見られます。"
            return 1
        fi

        [[ -n "$c_pull" ]] || c_pull="${BASE_DIR}/hdd/${name}"

        # 実パスで衝突を見る。設定の誤りでミラーが混ざるのを防ぐ。
        local rp other_name other_rp
        rp=$(real_dir "$c_pull")
        while IFS='|' read -r other_name other_rp; do
            [[ -n "$other_name" ]] || continue
            if [[ "$other_rp" == "$rp" ]]; then
                error "カード ${other_name} と ${name} の PULL_DIR が同じ場所を指しています:"
                error "  ${rp}"
                error "  別々のカードのミラーが混ざるので、どちらかを変えてください。"
                return 1
            fi
        done <<< "$seen"
        seen="${seen}${name}|${rp}
"
        lines="${lines}${name}|${c_img}|${c_off}|${c_ser}|${c_pull}
"
    done

    printf '%s' "$lines"
}

# 1 枚を選ぶ。選んだ行を標準出力へ出す。
#
#   -c <名前> あり → 段 1・段 2 を行わず、その 1 枚を照合するだけ
#   -c なし        → 段 1（IMAGE の存在）→ 段 2（シリアル一致）で絞る
#
# 段 2 が無いと最も普通の構成で使えなくなる。BlueSCSI のカードを同じ手順で
# 2 枚作れば、ラベルもイメージのファイル名も同じになるので、両方の定義が
# 同じパスを指す。段 1 だけでは「候補 2 つ＝曖昧」と判定されるが、1 枚ずつ
# 挿す運用では実際に存在するイメージは 1 つなので、シリアルで必ず絞れる。
select_card() {
    local want="" cards line
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -c)
                want="${2:-}"
                if [[ -z "$want" ]]; then
                    error "-c にカード名が要ります。"
                    error "  定義されているカードは 98hdd card list で見られます。"
                    return 1
                fi
                shift 2
                ;;
            *) shift ;;
        esac
    done

    cards=$(load_cards) || return 1

    local names=""
    while IFS='|' read -r n _rest; do
        [[ -n "$n" ]] && names="${names} ${n}"
    done <<< "$cards"

    if [[ -n "$want" ]]; then
        line=""
        while IFS= read -r l; do
            [[ -n "$l" ]] || continue
            [[ "${l%%|*}" == "$want" ]] && line="$l"
        done <<< "$cards"
        if [[ -z "$line" ]]; then
            error "カード ${want} は定義されていません。"
            error "  定義されているのは:${names}"
            return 1
        fi
        verify_card "$line" "$cards" || return 1
        printf '%s\n' "$line"
        return 0
    fi

    # 暗黙の default は段 1・段 2 を通さない。通すとイメージ不在のときに
    # E4（使えるカードが無い）になり、従来の E1（イメージが見つからない）と
    # 文面が変わる。照合は use_card が verify_card で行う。
    if [[ "$cards" == "default|"*"||"* ]]; then
        printf '%s' "$cards"
        return 0
    fi

    # 段 1: IMAGE が存在するものを候補にする。
    local cand="" c_name c_img c_off c_ser c_pull
    while IFS='|' read -r c_name c_img c_off c_ser c_pull; do
        [[ -n "$c_name" ]] || continue
        [[ -f "$c_img" ]] || continue
        cand="${cand}${c_name}|${c_img}|${c_off}|${c_ser}|${c_pull}
"
    done <<< "$cards"

    # 段 2: 宣言したシリアルと一致するものだけ残す。
    # SERIAL が空なのは暗黙の default だけで、そのときは照合しない。
    local kept="" kept_n=0 found="" unreadable="" kept_names=""
    while IFS='|' read -r c_name c_img c_off c_ser c_pull; do
        [[ -n "$c_name" ]] || continue
        local got
        got=$(read_serial "$c_img" "$c_off") || got=""
        if [[ -z "$got" ]]; then
            # イメージは在るのに FAT として読めない。「カードが見つからない」
            # と言うと誤解を招く — カードは在って、オフセットか形式が違う。
            unreadable="${unreadable}${c_name}|${c_img}|${c_off}|${c_ser}|${c_pull}
"
            continue
        fi
        found="${found} ${got}"
        if [[ "$got" == "$c_ser" ]]; then
            kept="${c_name}|${c_img}|${c_off}|${c_ser}|${c_pull}"
            kept_n=$((kept_n + 1))
            kept_names="${kept_names} ${c_name}"
        fi
    done <<< "$cand"

    if (( kept_n == 1 )); then
        printf '%s\n' "$kept"
        return 0
    fi

    if (( kept_n == 0 )); then
        # 読めない候補が在るなら、そちらを先に述べる。オフセットの誤りは
        # 「カードが無い」より手がかりが多い。
        if [[ -n "$unreadable" ]]; then
            local u
            while IFS= read -r u; do
                [[ -n "$u" ]] || continue
                verify_card "$u" "$cards" || true
            done <<< "$unreadable"
            return 1
        fi
        if [[ -n "$found" ]]; then
            error "挿さっているカードが、どの定義とも一致しません。"
            error "  読めたシリアル番号:${found}"
            error "  この値を ~/.98hdd/cards/<名前>.conf の SERIAL に書けば使えます。"
        else
            error "使えるカードが見つかりません。"
            error "  カードを挿すか、-c で明示してください。"
        fi
        error "  定義されているのは:${names}"
        return 1
    fi

    # 別々のイメージが 2 つ以上マウントされ、それぞれ自分の定義と一致している。
    # ここで「最初の 1 枚」を選ぶと、意図していないカードへ書く道が開く。
    error "カードが複数マウントされていて、どれを指すか決められません:${kept_names}"
    error "  -c <名前> で選んでください。"
    return 1
}

# 選んだカードを大域に置き、名前を 1 行告げる。
#
# **告げるのは最初の mtools 呼び出しより前。** 後に出すと、利用者が気づくのは
# 書き終わったあとになる。これが後置 `-c` を無視することの安全弁である
# （98hdd pull -c alpha と打って「カード: bravo（自動判定）」が出れば、
# その場で気づける）。
CARD_NAME=""; CARD_IMAGE=""; CARD_OFFSET=""; CARD_SERIAL=""
CARD_PULL_DIR=""; CARD_SPEC=""

use_card() {                # use_card [名前]
    local want="${1:-}" line how
    if [[ -n "$want" ]]; then line=$(select_card -c "$want") || return 1
    else line=$(select_card) || return 1; fi

    IFS='|' read -r CARD_NAME CARD_IMAGE CARD_OFFSET CARD_SERIAL CARD_PULL_DIR \
        <<< "$line"
    CARD_SPEC="${CARD_IMAGE}@@${CARD_OFFSET}"

    # 第 2 引数（カード一覧）は空でよい。これは「一致するカードを名指す」ため
    # だけに使われ、ここへ来る 3 経路はいずれもそこへ到達しない:
    # -c 経路は select_card の中で一覧つきの照合を済ませ、自動経路は段 2 で
    # シリアルが一致済み、暗黙 default は SERIAL が無いので署名の確認で終わる。
    # 空を渡すことで、1 回の起動で設定を 2 度読まずに済む。
    verify_card "$line" "" || return 1

    if [[ -n "$want" ]]; then how="-c 指定"
    elif [[ -z "$CARD_SERIAL" ]]; then how="カード定義が無いので ~/.98hddrc から"
    else how="自動判定"; fi
    info "カード: ${CARD_NAME}（${how}）"
}

# 選んだ 1 枚が本当にその定義のものかを確かめる。最初の mtools 呼び出しより
# 前に通す（仕様 I1）。読み取りも含めて、照合前には何もしない。
verify_card() {             # verify_card <line> <all-cards>
    local line="$1" all="${2:-}"
    local c_name c_img c_off c_ser c_pull
    IFS='|' read -r c_name c_img c_off c_ser c_pull <<< "$line"

    if [[ ! -f "$c_img" ]]; then
        error "カード ${c_name} のイメージが見つかりません:"
        error "  ${c_img}"
        return 1
    fi

    # **署名の確認は SERIAL の有無に関わらず行う。** 「この位置に FAT が
    # あるか」は同定とは別の問いで、暗黙の default でも確かめる必要がある。
    # HDD_OFFSET は従来 ~/.mtoolsrc 側が実効値だったので、食い違っていても
    # 動いていた。これからは ~/.98hddrc の値が使われるので、誤っていれば
    # ここで止める（mtools の non DOS media より手がかりが多い）。
    local got
    got=$(read_serial "$c_img" "$c_off") || got=""
    if [[ -z "$got" ]]; then
        local sig
        sig=$(read_boot_signature "$c_img" "$c_off")
        error "カード ${c_name} のイメージが FAT として読めません。"
        error "  ${c_img} のオフセット ${c_off} にある拡張ブート署名が 0x${sig} で、"
        error "  0x29 ではありません。オフセットが違っている可能性が高いです。"
        error "  （FAT の先頭を指していないと、この位置は別のデータになります）"
        return 1
    fi

    # ここから下は同定。SERIAL が無いのは暗黙の default だけで、
    # そのときは照合しない（従来の振る舞い）。
    [[ -n "$c_ser" ]] || return 0

    if [[ "$got" != "$c_ser" ]]; then
        error "カード ${c_name} ではありません。"
        error "  ${c_name} のシリアル番号: ${c_ser}"
        error "  実際に挿さっているもの  : ${got}"
        local o_name o_img o_off o_ser o_pull
        while IFS='|' read -r o_name o_img o_off o_ser o_pull; do
            [[ -n "$o_name" ]] || continue
            [[ "$o_ser" == "$got" ]] && \
                error "  この値はカード ${o_name} のものです。-c ${o_name} なら一致します。"
        done <<< "$all"
        error "  何も読み書きしていません。"
        return 1
    fi
    return 0
}

# ── テキストファイル判定（拡張子ホワイトリスト方式） ─────────────────────────
# 以前は file コマンドの内容推定を使っていたが、判定がバージョン・内容により
# 揺れてバイナリを nkf で破壊した実績があるため（2026-07-14 CRLF.EXE 事故）、
# 明示した拡張子のみテキスト扱いとする。判定は「ファイル名」で行う。
TEXT_EXTS="bat txt doc c h asm inc ini def bas err log md"
is_text_file() {
    local name ext e
    name=$(basename "$1")
    [[ "$name" == *.* ]] || return 1
    ext=$(echo "${name##*.}" | LC_ALL=C tr '[:upper:]' '[:lower:]')
    for e in $TEXT_EXTS; do
        [[ "$ext" == "$e" ]] && return 0
    done
    return 1
}

# ── 変換関数 ──────────────────────────────────────────────────────────────────
# PC-98 (Shift-JIS + CR+LF) → Mac (UTF-8 + LF)
convert_from_dos() {
    local src="$1" dst="$2"
    nkf -Lu -w "$src" > "$dst"
}

# Mac (UTF-8 + LF) → PC-98 (Shift-JIS + CR+LF)
convert_to_dos() {
    local src="$1" dst="$2"
    nkf -Lw -s "$src" > "$dst"
}

# ── 8.3形式変換 ───────────────────────────────────────────────────────────────
to_83() {
    local name="$1" base ext
    if [[ "$name" == *.* ]]; then
        base="${name%.*}"; ext="${name##*.}"
        ext=$(echo "$ext" | tr '[:lower:]' '[:upper:]')
        ext="${ext:0:3}"
    else
        base="$name"; ext=""
    fi
    base=$(echo "$base" | tr '[:lower:]' '[:upper:]' | tr -cd 'A-Z0-9_\-')
    base="${base:0:8}"
    [[ -n "$ext" ]] && echo "${base}.${ext}" || echo "$base"
}

# ── ファイル名衝突チェック ────────────────────────────────────────────────────
check_name_collision() {
    local base_dir="$1" collision=0
    local tmp_seen
    tmp_seen=$(mktemp "${TMPDIR:-/tmp}/98hdd.XXXXXXXX")

    while IFS= read -r src; do
        [[ -f "$src" ]] || continue
        local rel="${src#"$base_dir"/}"
        local dir_part file_part dos_name key
        dir_part=$(dirname "$rel"); file_part=$(basename "$rel")
        dos_name=$(to_83 "$file_part")

        if [[ "$dir_part" == "." ]]; then
            key="/$dos_name"
        else
            local dos_dir="" part
            while IFS= read -r part; do
                dos_dir="${dos_dir}/$(to_83 "$part")"
            done < <(echo "$dir_part" | tr '/' '\n')
            key="${dos_dir}/${dos_name}"
        fi

        if grep -qF "$key" "$tmp_seen" 2>/dev/null; then
            error "ファイル名の衝突: '$src' → '$key'"
            collision=1
        else
            echo "$key" >> "$tmp_seen"
        fi
    done < <(find "$base_dir" -type f | sort)

    rm -f "$tmp_seen"
    return $collision
}

# ── HDD空き容量チェック ───────────────────────────────────────────────────────
check_hdd_space() {
    local required_bytes="$1" free_line free_bytes
    free_line=$(run_mtools mdir -i "$CARD_SPEC" :: 2>/dev/null | grep "bytes free" | tail -1 || true)
    [[ -z "$free_line" ]] && { warn "空き容量取得失敗（スキップ）"; return 0; }
    free_bytes=$(echo "$free_line" | tr -d ' \t' | grep -oE '[0-9]+bytesfree' | grep -oE '^[0-9]+')
    info "HDD空き: ${free_bytes} bytes / 送信予定: ${required_bytes} bytes"
    (( required_bytes > free_bytes )) && die "容量不足: 必要 ${required_bytes} / 空き ${free_bytes} bytes"
    return 0
}

# ── send ──────────────────────────────────────────────────────────────────────
cmd_send() {
    local src_dir="${1:-$SEND_DIR}"
    # 送信先パス（省略時ルート）。各要素を8.3形式に正規化
    local target="" raw_target="${2:-}"
    if [[ -n "$raw_target" ]]; then
        local part
        while IFS= read -r part; do
            [[ -z "$part" ]] && continue
            target="${target}/$(to_83 "$part")"
        done < <(echo "$raw_target" | tr '/' '\n')
    fi
    [[ -d "$src_dir" ]] || die "送信元ディレクトリがありません: $src_dir"
    info "送信元: $src_dir  →  送信先: ${CARD_NAME}:${target}/"

    local all_files=()
    while IFS= read -r f; do all_files+=("$f"); done < <(find "$src_dir" -type f | sort)
    [[ ${#all_files[@]} -eq 0 ]] && { warn "送信するファイルがありません"; exit 0; }
    info "対象: ${#all_files[@]} ファイル"

    info "ファイル名チェック中..."
    check_name_collision "$src_dir" || die "ファイル名の衝突。送信中止"
    ok "ファイル名チェック: OK"

    # EXITトラップはトップレベルで実行されるため local にしない（set -u対策）
    tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/98hdd.XXXXXXXX")
    trap 'if [[ -n "${tmp_dir:-}" ]]; then rm -rf "$tmp_dir"; fi' EXIT

    info "変換中（テキスト: UTF-8+LF → Shift-JIS+CR+LF）..."
    local total_size=0
    for src in "${all_files[@]}"; do
        local rel="${src#"$src_dir"/}"
        local dst="${tmp_dir}/${rel}"
        mkdir -p "$(dirname "$dst")"

        if is_text_file "$src"; then
            info "  [TEXT] $rel"
            convert_to_dos "$src" "$dst"
        else
            info "  [BIN]  $rel"
            cp "$src" "$dst"
        fi

        local sz
        sz=$(wc -c < "$dst" | tr -d ' ')
        total_size=$(( total_size + sz ))
    done
    ok "変換完了 (合計 ${total_size} bytes)"

    info "容量チェック中..."
    check_hdd_space "$total_size"
    ok "容量チェック: OK"

    info "送信中..."
    # 作成済みディレクトリの記録（bash 3.2互換のため連想配列不使用）
    local created_dirs=""
    # mmd には -D s（名前衝突ならスキップ）が要る。既にあるディレクトリに mmd すると
    # mtools は "Long file name ... already exists" の対話プロンプトを出し、答えを
    # 標準入力ではなく /dev/tty から読む。ここは stderr を捨てているためプロンプトが
    # 画面に出ないまま無言で固まり、|| true にも到達しない。-D S（大文字）では直らない。
    # 送信先パスのディレクトリ階層を作成
    if [[ -n "$target" ]]; then
        local cumulative="" part
        while IFS= read -r part; do
            [[ -z "$part" ]] && continue
            cumulative="${cumulative}/${part}"
            run_mtools mmd -D s -i "$CARD_SPEC" "::${cumulative}" 2>/dev/null || true
            created_dirs="${created_dirs}|${cumulative}|"
        done < <(echo "${target#/}" | tr '/' '\n')
    fi
    for src in "${all_files[@]}"; do
        local rel="${src#"$src_dir"/}"
        local dir_part file_part dos_file tmp_src
        dir_part=$(dirname "$rel"); file_part=$(basename "$rel")
        dos_file=$(to_83 "$file_part")
        tmp_src="${tmp_dir}/${rel}"

        if [[ "$dir_part" != "." ]]; then
            local dos_dir="" cumulative="$target" part
            while IFS= read -r part; do
                local dp
                dp=$(to_83 "$part")
                cumulative="${cumulative}/${dp}"
                if [[ "$created_dirs" != *"|${cumulative}|"* ]]; then
                    run_mtools mmd -D s -i "$CARD_SPEC" "::${cumulative}" 2>/dev/null || true
                    created_dirs="${created_dirs}|${cumulative}|"
                fi
                dos_dir="$cumulative"
            done < <(echo "$dir_part" | tr '/' '\n')
            info "  $rel → ${CARD_NAME}:${dos_dir}/${dos_file}"
            run_mtools mcopy -o -i "$CARD_SPEC" "$tmp_src" "::${dos_dir}/${dos_file}"
        else
            info "  $file_part → ${CARD_NAME}:${target}/${dos_file}"
            run_mtools mcopy -o -i "$CARD_SPEC" "$tmp_src" "::${target}/${dos_file}"
        fi
    done
    ok "送信完了: ${#all_files[@]} ファイル"
}

# ── pull ──────────────────────────────────────────────────────────────────────
cmd_pull() {
    mkdir -p "$CARD_PULL_DIR"
    info "展開先: $CARD_PULL_DIR  ←  展開元: カード ${CARD_NAME}"

    local ts
    ts=$(date +%Y%m%d_%H%M%S)

    local hdd_files=()
    while IFS= read -r line; do
        local normalized
        # 小文字化はASCIIのみ（LC_ALL=C）。UTF-8ロケールのtrはCP850由来の
        # アクセント付き大文字まで変換して日本語ファイル名のバイトを壊す
        normalized=$(echo "$line" | sed 's|\\|/|g' | LC_ALL=C tr '[:upper:]' '[:lower:]')
        [[ "$normalized" == */ ]] && continue
        [[ -z "$normalized" ]] && continue
        hdd_files+=("$normalized")
    done < <(run_mtools mdir -s -b -i "$CARD_SPEC" :: 2>/dev/null)

    [[ ${#hdd_files[@]} -eq 0 ]] && { warn "HDDにファイルが見つかりません"; exit 0; }
    info "対象: ${#hdd_files[@]} ファイル"

    local n_new=0 n_upd=0 n_same=0
    for hdd_path in "${hdd_files[@]}"; do
        local rel dst_rel
        # mdir -i の出力は ::/PATH。243 行目で接頭辞ごと小文字化しているが
        # :: は英字を含まないので影響を受けない。cmd_fd と同じ剥がし方に
        # 揃える（1 ファイルにつき sed 1 起動が消える）。
        rel="${hdd_path#::/}"
        # 非ASCIIファイル名の復元: mtoolsはDOS名をCP850とみなしてUTF-8化して
        # 出力するため、CP850へ逆変換して元のShift-JISバイト列を復元してから
        # UTF-8へ変換する（nkfは-Sで入力エンコーディングを明示）
        dst_rel="$rel"
        if echo "$dst_rel" | LC_ALL=C grep -q '[^ -~]'; then
            local conv
            conv=$(echo "$dst_rel" | iconv -f UTF-8 -t CP850 2>/dev/null | nkf -S -w) || true
            [[ -n "$conv" ]] && dst_rel="$conv"
        fi
        local dst="${CARD_PULL_DIR}/${dst_rel}"
        mkdir -p "$(dirname "$dst")"

        # まず一時ファイルに取り出す（失敗時は既存ミラーに触れない）
        local tmp_raw
        tmp_raw=$(mktemp "${TMPDIR:-/tmp}/98hdd.XXXXXXXX")
        run_mtools mcopy -n -i "$CARD_SPEC" "::/${rel}" "$tmp_raw" 2>/dev/null || {
            # 変数名は必ず {} で囲む。bash 3.2 はUTF-8ロケールで全角括弧の
            # 先頭バイトを変数名の一部と解釈し、set -u と組み合わさると
            # このスキップ処理自体が unbound variable で異常終了する
            warn "  コピー失敗: ${hdd_path}（スキップ）"
            rm -f "$tmp_raw"
            continue
        }

        # テキスト判定（ファイル名ベース）して変換結果を一時ファイルに作る
        local tmp_out kind
        if is_text_file "$rel"; then
            kind="TEXT"
            tmp_out=$(mktemp "${TMPDIR:-/tmp}/98hdd.XXXXXXXX")
            convert_from_dos "$tmp_raw" "$tmp_out"
            rm -f "$tmp_raw"
        else
            kind="BIN"
            tmp_out="$tmp_raw"
        fi

        # 既存と内容が同一ならスキップ（.bakを作らない）
        if [[ -f "$dst" ]]; then
            if cmp -s "$tmp_out" "$dst"; then
                rm -f "$tmp_out"
                n_same=$((n_same+1))
                continue
            fi
            # 変更があった場合のみバックアップして置換
            local bak="${dst}.bak_${ts}"
            warn "  バックアップ: $(basename "$dst") → $(basename "$bak")"
            mv "$dst" "$bak"
            n_upd=$((n_upd+1))
        else
            n_new=$((n_new+1))
        fi

        info "  [$kind] $hdd_path → $dst"
        mv "$tmp_out" "$dst"
    done
    ok "展開完了: ${CARD_PULL_DIR} (新規 ${n_new} / 更新 ${n_upd} / 変更なし ${n_same})"
}

# ── fd（FDIイメージ展開） ─────────────────────────────────────────────────────
cmd_fd() {
    local img="${1:-}"
    [[ -n "$img" ]] || die "使い方: 98hdd fd <イメージ.fdi> [出力先ディレクトリ]"
    [[ -f "$img" ]] || die "イメージが見つかりません: $img"

    # FDIヘッダからデータ開始オフセットを取得（オフセット0x08の4バイトLE）
    local hdr_size
    hdr_size=$(od -An -j 8 -N 4 -t u4 "$img" | tr -d ' ')
    if ! [[ "$hdr_size" =~ ^[0-9]+$ ]] || (( hdr_size == 0 || hdr_size >= 65536 )); then
        error "FDIヘッダを解釈できません: $img (headersize=${hdr_size:-?})"
        return 1
    fi
    local src_spec="${img}@@${hdr_size}"

    # 出力先: 省略時はイメージと同じ場所に <イメージ名>/ を作成
    local base dest
    base=$(basename "$img"); base="${base%.*}"
    dest="${2:-$(dirname "$img")/$base}"

    local fd_files=()
    while IFS= read -r line; do
        local normalized
        normalized=$(echo "$line" | sed 's|\\|/|g')
        [[ "$normalized" == */ ]] && continue
        [[ -z "$normalized" ]] && continue
        fd_files+=("$normalized")
    done < <(run_mtools mdir -i "$src_spec" -s -b :: 2>/dev/null)

    if [[ ${#fd_files[@]} -eq 0 ]]; then
        if run_mtools mdir -i "$src_spec" :: &>/dev/null; then
            warn "空のイメージ: $img"
            return 0
        fi
        error "FATとして読めません（非DOSフォーマット?）: $img"
        return 1
    fi

    info "展開: $img → $dest/ (${#fd_files[@]} ファイル)"
    mkdir -p "$dest"
    local ts
    ts=$(date +%Y%m%d_%H%M%S)

    local fd_path
    for fd_path in "${fd_files[@]}"; do
        local rel dst_rel dst
        rel="${fd_path#::/}"
        dst_rel=$(echo "$rel" | LC_ALL=C tr '[:upper:]' '[:lower:]')
        # 非ASCIIファイル名の復元（CP850逆変換→Shift-JIS→UTF-8。pullと同様）
        if echo "$dst_rel" | LC_ALL=C grep -q '[^ -~]'; then
            local conv
            conv=$(echo "$dst_rel" | iconv -f UTF-8 -t CP850 2>/dev/null | nkf -S -w) || true
            [[ -n "$conv" ]] && dst_rel="$conv"
        fi
        dst="${dest}/${dst_rel}"
        mkdir -p "$(dirname "$dst")"

        # まず一時ファイルに取り出す（失敗時は既存ファイルに触れない）
        local tmp_raw
        tmp_raw=$(mktemp "${TMPDIR:-/tmp}/98hdd.XXXXXXXX")
        run_mtools mcopy -n -i "$src_spec" "::/${rel}" "$tmp_raw" 2>/dev/null || {
            warn "  コピー失敗: ${rel}（スキップ）"
            rm -f "$tmp_raw"
            continue
        }

        # 取り出しに成功してから既存ファイルをバックアップ
        if [[ -f "$dst" ]]; then
            local bak="${dst}.bak_${ts}"
            warn "  バックアップ: $(basename "$dst") → $(basename "$bak")"
            mv "$dst" "$bak"
        fi

        # テキスト判定（ファイル名ベース）して変換
        if is_text_file "$rel"; then
            info "  [TEXT] $rel → $dst"
            convert_from_dos "$tmp_raw" "$dst"
        else
            info "  [BIN]  $rel → $dst"
            mv "$tmp_raw" "$dst"
            continue
        fi
        rm -f "$tmp_raw"
    done
    ok "展開完了: $dest"
}

# ── ls ───────────────────────────────────────────────────────────────────────
cmd_ls() {
    local path="${1:-}"
    if [[ -z "$path" ]]; then run_mtools mdir -i "$CARD_SPEC" ::
    else
        [[ "$path" == /* ]] || path="/$path"
        run_mtools mdir -i "$CARD_SPEC" "::${path}"
    fi
}

# ── init ─────────────────────────────────────────────────────────────────────
cmd_init() {
    info "ディレクトリを初期化します"
    mkdir -p "$SEND_DIR" "$PULL_DIR"
    ok "作成済み: $SEND_DIR"
    ok "作成済み: $PULL_DIR"
    if [[ ! -f "$CONFIG_FILE" ]]; then
        cat > "$CONFIG_FILE" << RCEOF
# 98hdd 設定ファイル
HDD_IMAGE="/Volumes/x/HD10_512 200MB.hda"
HDD_OFFSET=360448
# HDD_DRIVE はもう使わない。mtools のドライブ文字（~/.mtoolsrc の drive p:）
# ではなく、-i "<イメージ>@@<オフセット>" でイメージを直に指すようになったため。
# 公開済みの ~/.98hddrc に残っていてもエラーにしないので、読み込みは許す。
HDD_DRIVE="p"
BASE_DIR="${HOME}/9801"
SEND_DIR="${BASE_DIR}/send"
PULL_DIR="${BASE_DIR}/hdd"
RCEOF
        ok "設定ファイル作成: $CONFIG_FILE"
    else
        info "設定ファイル既存: $CONFIG_FILE"
    fi
}

# ── card ─────────────────────────────────────────────────────────────────────
# 読み取りのみ。**選択経路を通さない。** 通すと、2 枚マウント中に一覧が
# 見られなくなる。一覧は「何が定義されているか」を見るもので、「どれを使うか」
# とは別の問いである。
cmd_card() {
    local sub="${1:-list}"
    shift || true
    case "$sub" in
        list) card_list ;;
        show) card_show "${1:-}" ;;
        *) error "card の副コマンドは list か show です: $sub"; return 1 ;;
    esac
}

card_list() {
    local cards name img off ser pull state got
    cards=$(load_cards) || return 1
    info "定義されているカード:"
    while IFS='|' read -r name img off ser pull; do
        [[ -n "$name" ]] || continue
        if [[ ! -f "$img" ]]; then state="イメージ不在"
        elif [[ -z "$ser" ]]; then state="照合しない（SERIAL 未定義）"
        else
            got=$(read_serial "$img" "$off") || got=""
            if [[ "$got" == "$ser" ]]; then state="一致"
            elif [[ -z "$got" ]]; then state="FAT として読めない"
            else state="不一致（実測 ${got}）"; fi
        fi
        printf '  %-12s %s\n' "$name" "$state"
        printf '               image    %s\n' "$img"
        printf '               offset   %s\n' "$off"
        printf '               serial   %s\n' "${ser:-（なし）}"
        printf '               pull_dir %s\n' "$pull"
    done <<< "$cards"
}

# **今挿さっているイメージの実測シリアルを出す。** これを .conf へ貼れば
# 定義が完成するので、カードを作るコマンドを設けずに済む。
card_show() {
    local want="${1:-}" cards name img off ser pull got
    cards=$(load_cards) || return 1
    local found=0
    while IFS='|' read -r name img off ser pull; do
        [[ -n "$name" ]] || continue
        if [[ -n "$want" && "$name" != "$want" ]]; then continue; fi
        found=1
        info "カード ${name}"
        printf '  image    %s\n' "$img"
        printf '  offset   %s\n' "$off"
        printf '  serial   %s（定義）\n' "${ser:-（なし）}"
        printf '  pull_dir %s\n' "$pull"
        if [[ -f "$img" ]]; then
            got=$(read_serial "$img" "$off") || got=""
            if [[ -n "$got" ]]; then
                printf '  serial   %s（実測。この値を .conf の SERIAL へ）\n' "$got"
            else
                printf '  serial   FAT として読めない（オフセットを確かめてください）\n'
            fi
        else
            printf '  （イメージが在りません。カードを挿すと実測値が出ます）\n'
        fi
    done <<< "$cards"
    if (( found == 0 )); then
        error "カード ${want} は定義されていません。"
        return 1
    fi
}

# ── help ─────────────────────────────────────────────────────────────────────
cmd_help() {
    cat << 'HELP'

  98hdd — PC-98 HDDイメージ操作ツール

  使い方:
    98hdd [-c <カード名>] <コマンド> ...
                        -c はコマンドの前に置く。後ろに置いた -c は無視される
                        （選んだカードは必ず画面に出るので、取り違えに気づける）
    98hdd init          ディレクトリと設定ファイルを初期化
    98hdd card list     定義されているカードと、挿さっているかを一覧
    98hdd card show [名前]
                        1 枚の設定と、今挿さっているイメージの実測シリアル
    98hdd send [送信元] [送信先]
                        送信元ディレクトリ（省略時 ~/9801/send/）の内容を
                        HDD上の送信先パス（省略時ルート）へ送信
                        例: 98hdd send pc98/DOS62 /FD/DOS62
    98hdd pull          HDDの内容を ~/9801/hdd/ へ展開
    98hdd ls [パス]     HDD上のディレクトリ一覧（例: 98hdd ls /QC）
    98hdd fd <イメージ.fdi> [出力先]
                        FDイメージ(FDI形式)の中身をファイルへ展開
                        出力先省略時はイメージと同じ場所に<イメージ名>/を作成
    98hdd help          このヘルプ

  文字コード・改行変換（nkf使用）:
    send: UTF-8 + LF  →  Shift-JIS + CR+LF
    pull: Shift-JIS + CR+LF  →  UTF-8 + LF
    バイナリ判定は file コマンドで自動判別（"text"を含む場合のみ変換）

  send の動作:
    1. ファイル名8.3形式衝突チェック（衝突時エラー停止）
    2. テキスト自動判定 → 変換 / バイナリはそのままコピー
    3. HDD空き容量チェック（不足時エラー停止・何も送らない）
    4. 再帰的にHDDへコピー

  pull の動作:
    1. HDDの全ファイルを展開
    2. 取り出し成功後、同名ファイルを .bak_YYYYMMDD_HHMMSS へ退避して上書き
       （取り出し失敗時は既存ファイルに触れずスキップ）
    3. テキスト自動判定 → 変換 / バイナリはそのままコピー

  ファイル名変換（8.3形式）:
    英数字・_・- 以外除去、大文字化、ベース8文字・拡張子3文字
    例: myLongName.c → MYLONGNA.C

  設定ファイル: ~/.98hddrc
    HDD_IMAGE   HDDイメージのパス
    HDD_DRIVE   mtoolsドライブ文字
    SEND_DIR    送信元ディレクトリ
    PULL_DIR    展開先ディレクトリ

HELP
}

# ── main ─────────────────────────────────────────────────────────────────────
main() {
    check_deps

    # -c はコマンドの前だけで解釈する。後ろに置いた -c は、send と ls が
    # 位置引数でパスを受け取るので一般には検出できない（パスとして "-c" を
    # 渡したい場合と区別がつかない）。無視する代わりに、選んだカードを必ず
    # 名前で告げることで取り違えに気づけるようにしてある（use_card）。
    local card_want=""
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -c)
                if [[ $# -lt 2 || -z "$2" ]]; then
                    error "-c にカード名が要ります。"
                    error "  定義されているカードは 98hdd card list で見られます。"
                    exit 1
                fi
                card_want="$2"
                shift 2
                ;;
            *) break ;;
        esac
    done

    local subcmd="${1:-help}"
    shift || true
    case "$subcmd" in
        init) cmd_init ;;
        card) cmd_card "$@" ;;
        # fd は FDI イメージを引数で受け取るので、カードの選択・同定は要らない。
        # ここで選択を走らせると、カードが挿さっていないだけで fd が使えなくなる。
        fd)   cmd_fd "$@" ;;
        help|-h|--help) cmd_help ;;
        send|pull|ls)
            # **最初の読み書きより前にカードを決めて照合する**（仕様 I1）。
            # 失敗したらここで終わるので、イメージも PULL_DIR も変わらない。
            use_card "$card_want" || exit 1
            case "$subcmd" in
                send) cmd_send "$@" ;;
                pull) cmd_pull ;;
                ls)   cmd_ls "$@" ;;
            esac
            ;;
        *) error "不明なコマンド: $subcmd"; cmd_help; exit 1 ;;
    esac
}

# 実行されたときだけ main を走らせる。source したときに走ると、関数単位の
# 試験が 1 本も書けない（source が全部実行してしまう）。
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then main "$@"; fi
