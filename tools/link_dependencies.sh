#!/usr/bin/env bash
set -euo pipefail

# DeepLearningTool Linux 依赖链接脚本。
# 不依赖 Python，直接使用 ln -s 创建构建目录中的符号链接。

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# 统一输出警告到 stderr，便于调用方区分正常日志和异常提示。
warn() {
    echo "warning: $*" >&2
}

# 解析绝对路径。优先使用 realpath；极简环境没有 realpath 时做基础兜底。
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

# 创建单个路径链接。
# 目标不存在时只提示并跳过，保持旧 Python 脚本的宽松行为。
# 如果链接位置已经存在真实文件或目录，则拒绝覆盖，避免误删用户手工放置的内容。
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

# 从 cmake/ConfigSQLite.cmake 读取 CMAKE_PREFIX_PATH。
# 项目当前用这个路径定位 sqlite 运行库。
read_sqlite_root() {
    local cmake_file="cmake/ConfigSQLite.cmake"
    [[ -f "$cmake_file" ]] || return 0

    sed -nE 's#^[[:space:]]*set[[:space:]]*\([[:space:]]*CMAKE_PREFIX_PATH[[:space:]]+"([^"]+)".*#\1#p' "$cmake_file" | tail -n 1
}

# 链接项目自身模块目录。
# build/bin/dltool 指向 build/dltool，随后把模块共享库链接到 build/bin 根目录。
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

# 链接 SQLite 运行库。
# Linux/macOS/Windows shell 环境的库文件名不同，因此按 uname 分支处理。
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

# 如果测试目录存在，则让测试可执行程序也能从运行目录找到 dltool 模块。
link_test() {
    if [[ -d "build/tests" ]]; then
        link_path "build/dltool" "build/tests/dltool"
    else
        warn "skip test link, missing build/tests"
    fi
}

# 按原脚本顺序执行三类链接。每一步成功后输出一条兼容旧日志的 success 信息。
link_dltool
echo "link dltool dll success"

link_sqlite
echo "link sqlite3 dll success"

link_test
echo "link test success"
