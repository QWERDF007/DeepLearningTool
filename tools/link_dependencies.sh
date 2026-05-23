#!/usr/bin/env bash
set -euo pipefail

# DeepLearningTool Linux 依赖链接脚本。
# 不依赖 Python，直接使用 ln -s 创建构建目录中的符号链接。

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

warn() {
    echo "warning: $*" >&2
}

abs_path() {
    local path="$1"
    if command -v realpath >/dev/null 2>&1; then
        realpath -m "$path"
    elif [[ "$path" = /* ]]; then
        printf '%s\n' "$path"
    else
        printf '%s/%s\n' "$PWD" "$path"
    fi
}

link_path() {
    local target="$1"
    local link="$2"
    local target_abs
    local link_abs

    target_abs="$(abs_path "$target")"
    link_abs="$(abs_path "$link")"

    if [[ ! -e "$target_abs" ]]; then
        warn "skip missing path: $target_abs"
        return 0
    fi

    if [[ -L "$link_abs" ]]; then
        rm -f "$link_abs"
    elif [[ -e "$link_abs" ]]; then
        echo "existing path is not a symbolic link, refusing to overwrite: $link_abs" >&2
        return 1
    fi

    mkdir -p "$(dirname "$link_abs")"
    ln -s "$target_abs" "$link_abs"
    echo "create symlink $link_abs -> $target_abs"
}

read_sqlite_root() {
    local cmake_file="cmake/ConfigSQLite.cmake"
    [[ -f "$cmake_file" ]] || return 0

    sed -nE 's#^[[:space:]]*set[[:space:]]*\([[:space:]]*CMAKE_PREFIX_PATH[[:space:]]+"([^"]+)".*#\1#p' "$cmake_file" | tail -n 1
}

link_dltool() {
    link_path "build/dltool" "build/bin/dltool"

    if [[ ! -d "build/bin/dltool" ]]; then
        warn "skip dltool shared library links, missing build/bin/dltool"
        return 0
    fi

    find -L "build/bin/dltool" -type f \( -name "*.so" -o -name "*.so.*" -o -name "*.dylib" \) -print0 |
        while IFS= read -r -d '' library; do
            link_path "$library" "build/bin/$(basename "$library")"
        done
}

link_sqlite() {
    local sqlite_root
    sqlite_root="$(read_sqlite_root || true)"

    if [[ -z "$sqlite_root" ]]; then
        warn "skip sqlite link, CMAKE_PREFIX_PATH was not found in cmake/ConfigSQLite.cmake"
        return 0
    fi

    case "$(uname -s)" in
        Linux*)
            if [[ -d "$sqlite_root/lib" ]]; then
                find "$sqlite_root/lib" -maxdepth 1 -type f \( -name "libsqlite3.so" -o -name "libsqlite3.so.*" \) -print0 |
                    while IFS= read -r -d '' library; do
                        link_path "$library" "build/bin/$(basename "$library")"
                    done
            else
                warn "skip sqlite link, missing $sqlite_root/lib"
            fi
            ;;
        Darwin*)
            link_path "$sqlite_root/lib/libsqlite3.dylib" "build/bin/libsqlite3.dylib"
            ;;
        MINGW*|MSYS*|CYGWIN*)
            link_path "$sqlite_root/lib/sqlite3.dll" "build/bin/sqlite3.dll"
            ;;
        *)
            warn "skip sqlite link, unsupported platform: $(uname -s)"
            ;;
    esac
}

link_test() {
    if [[ -d "build/tests" ]]; then
        link_path "build/dltool" "build/tests/dltool"
    else
        warn "skip test link, missing build/tests"
    fi
}

link_dltool
echo "link dltool dll success"

link_sqlite
echo "link sqlite3 dll success"

link_test
echo "link test success"
