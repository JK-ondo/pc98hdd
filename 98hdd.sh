#!/bin/bash
# =============================================================================
# 98hdd — PC-98 HDDイメージ操作ツール
# =============================================================================

set -euo pipefail

HDD_IMAGE="/Volumes/x/HD10_512 200MB.hda"
HDD_OFFSET=360448
HDD_DRIVE="p"
BASE_DIR="${HOME}/9801"
SEND_DIR="${BASE_DIR}/send"
PULL_DIR="${BASE_DIR}/hdd"

CONFIG_FILE="${HOME}/.98hddrc"
if [[ -f "$CONFIG_FILE" ]]; then source "$CONFIG_FILE"; fi

DRIVE="${HDD_DRIVE}:"

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

check_image() {
    [[ -f "$HDD_IMAGE" ]] || die "HDDイメージが見つかりません: $HDD_IMAGE"
}

run_mtools() { MTOOLS_SKIP_CHECK=1 "$@"; }

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
    tmp_seen=$(mktemp)

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
    free_line=$(run_mtools mdir "$DRIVE" 2>/dev/null | grep "bytes free" | tail -1 || true)
    [[ -z "$free_line" ]] && { warn "空き容量取得失敗（スキップ）"; return 0; }
    free_bytes=$(echo "$free_line" | tr -d ' \t' | grep -oE '[0-9]+bytesfree' | grep -oE '^[0-9]+')
    info "HDD空き: ${free_bytes} bytes / 送信予定: ${required_bytes} bytes"
    (( required_bytes > free_bytes )) && die "容量不足: 必要 ${required_bytes} / 空き ${free_bytes} bytes"
    return 0
}

# ── send ──────────────────────────────────────────────────────────────────────
cmd_send() {
    check_image
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
    info "送信元: $src_dir  →  送信先: ${DRIVE}${target}/"

    local all_files=()
    while IFS= read -r f; do all_files+=("$f"); done < <(find "$src_dir" -type f | sort)
    [[ ${#all_files[@]} -eq 0 ]] && { warn "送信するファイルがありません"; exit 0; }
    info "対象: ${#all_files[@]} ファイル"

    info "ファイル名チェック中..."
    check_name_collision "$src_dir" || die "ファイル名の衝突。送信中止"
    ok "ファイル名チェック: OK"

    # EXITトラップはトップレベルで実行されるため local にしない（set -u対策）
    tmp_dir=$(mktemp -d)
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
            run_mtools mmd -D s "${DRIVE}${cumulative}" 2>/dev/null || true
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
                    run_mtools mmd -D s "${DRIVE}${cumulative}" 2>/dev/null || true
                    created_dirs="${created_dirs}|${cumulative}|"
                fi
                dos_dir="$cumulative"
            done < <(echo "$dir_part" | tr '/' '\n')
            info "  $rel → ${DRIVE}${dos_dir}/${dos_file}"
            run_mtools mcopy -o "$tmp_src" "${DRIVE}${dos_dir}/${dos_file}"
        else
            info "  $file_part → ${DRIVE}${target}/${dos_file}"
            run_mtools mcopy -o "$tmp_src" "${DRIVE}${target}/${dos_file}"
        fi
    done
    ok "送信完了: ${#all_files[@]} ファイル"
}

# ── pull ──────────────────────────────────────────────────────────────────────
cmd_pull() {
    check_image
    mkdir -p "$PULL_DIR"
    info "展開先: $PULL_DIR  ←  展開元: $DRIVE"

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
    done < <(run_mtools mdir -s -b "$DRIVE" 2>/dev/null)

    [[ ${#hdd_files[@]} -eq 0 ]] && { warn "HDDにファイルが見つかりません"; exit 0; }
    info "対象: ${#hdd_files[@]} ファイル"

    local n_new=0 n_upd=0 n_same=0
    for hdd_path in "${hdd_files[@]}"; do
        local rel dst_rel
        rel=$(echo "$hdd_path" | sed "s|^${HDD_DRIVE}:/||i")
        # 非ASCIIファイル名の復元: mtoolsはDOS名をCP850とみなしてUTF-8化して
        # 出力するため、CP850へ逆変換して元のShift-JISバイト列を復元してから
        # UTF-8へ変換する（nkfは-Sで入力エンコーディングを明示）
        dst_rel="$rel"
        if echo "$dst_rel" | LC_ALL=C grep -q '[^ -~]'; then
            local conv
            conv=$(echo "$dst_rel" | iconv -f UTF-8 -t CP850 2>/dev/null | nkf -S -w) || true
            [[ -n "$conv" ]] && dst_rel="$conv"
        fi
        local dst="${PULL_DIR}/${dst_rel}"
        mkdir -p "$(dirname "$dst")"

        # まず一時ファイルに取り出す（失敗時は既存ミラーに触れない）
        local tmp_raw
        tmp_raw=$(mktemp)
        run_mtools mcopy -n "${DRIVE}/${rel}" "$tmp_raw" 2>/dev/null || {
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
            tmp_out=$(mktemp)
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
    ok "展開完了: ${PULL_DIR} (新規 ${n_new} / 更新 ${n_upd} / 変更なし ${n_same})"
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
        tmp_raw=$(mktemp)
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
    check_image
    local path="${1:-}"
    if [[ -z "$path" ]]; then run_mtools mdir "$DRIVE"
    else
        [[ "$path" == /* ]] || path="/$path"
        run_mtools mdir "${DRIVE}${path}"
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

# ── help ─────────────────────────────────────────────────────────────────────
cmd_help() {
    cat << 'HELP'

  98hdd — PC-98 HDDイメージ操作ツール

  使い方:
    98hdd init          ディレクトリと設定ファイルを初期化
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
    local subcmd="${1:-help}"
    shift || true
    case "$subcmd" in
        init) cmd_init ;;
        send) cmd_send "$@" ;;
        pull) cmd_pull ;;
        ls)   cmd_ls "$@" ;;
        fd)   cmd_fd "$@" ;;
        help|-h|--help) cmd_help ;;
        *) error "不明なコマンド: $subcmd"; cmd_help; exit 1 ;;
    esac
}

main "$@"
