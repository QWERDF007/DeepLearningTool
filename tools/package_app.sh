#!/usr/bin/env bash
set -euo pipefail

# DeepLearningTool Linux 打包脚本。
# 目标：把 build 目录中的可执行程序、项目 QML 模块、Qt QML/插件目录和 ELF 依赖复制到 install 目录。
# Linux 下动态库默认不会从可执行文件所在目录查找，因此脚本会额外生成 run_dltool.sh 启动脚本。

PROJECT_NAME="dltool"
MARKER_FILE=".dltool_package"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="build"
INSTALL_DIR="install"
CONFIG="auto"
QT_ROOT=""
SKIP_QT=0
INCLUDE_DEBUG=0
NO_CLEAN=0
FORCE_CLEAN=0
COPY_SYSTEM_LIBS=1

usage() {
    cat <<'USAGE'
Usage:
  bash tools/package_app.sh [options]

Options:
  --build-dir <path>, -BuildDir <path>       CMake build directory. Default: build
  --install-dir <path>, -InstallDir <path>   Package output directory. Default: install
  --config <auto|debug|release>, -Config     Build config hint. Default: auto
  --qt-root <path>, -QtRoot <path>           Qt install root, for example /opt/Qt/6.8.0/gcc_64
  --skip-qt, -SkipQt                         Do not copy Qt qml/plugins directories
  --include-debug, -IncludeDebug             Keep debug symbol files when copying project outputs
  --skip-system-libs                         Do not copy libraries from /lib and /usr/lib
  --copy-system-libs                         Copy system libraries except glibc/loader core files
  --no-clean, -NoClean                       Do not clean install directory before copying
  --force-clean, -ForceClean                 Clean install directory even if it has no package marker
  --help, -h                                 Show this help

Examples:
  bash tools/package_app.sh
  bash tools/package_app.sh --config release --qt-root /opt/Qt/6.8.0/gcc_64
  bash tools/package_app.sh -BuildDir out/build -InstallDir out/install
USAGE
}

die() {
    echo "error: $*" >&2
    exit 1
}

warn() {
    echo "warning: $*" >&2
}

resolve_project_path() {
    local path="$1"
    if [[ "$path" = /* ]]; then
        if command -v realpath >/dev/null 2>&1; then
            realpath -m "$path"
        else
            printf '%s\n' "$path"
        fi
        return
    fi

    if command -v realpath >/dev/null 2>&1; then
        realpath -m "$REPO_ROOT/$path"
    else
        printf '%s/%s\n' "$REPO_ROOT" "$path"
    fi
}

read_cmake_cache_value() {
    local cache_file="$1"
    local key="$2"

    [[ -f "$cache_file" ]] || return 1
    sed -nE "s#^${key}(:[^=]+)?=(.*)#\2#p" "$cache_file" | tail -n 1
}

read_cmake_set() {
    local cmake_file="$1"
    local name="$2"

    [[ -f "$cmake_file" ]] || return 1
    sed -nE "s#^[[:space:]]*set[[:space:]]*\([[:space:]]*${name}[[:space:]]+\"([^\"]+)\"[[:space:]]*\).*#\1#p" "$cmake_file" | tail -n 1
}

copy_file() {
    local source="$1"
    local destination="$2"

    mkdir -p "$(dirname "$destination")"
    cp -fL "$source" "$destination"
    chmod --reference="$source" "$destination" 2>/dev/null || true
    echo "copy $source -> $destination"
}

clear_install_directory() {
    local path="$1"
    local clean="$2"
    local force="$3"

    if [[ "$clean" -eq 0 ]]; then
        mkdir -p "$path"
        return
    fi

    local marker="$path/$MARKER_FILE"
    if [[ -d "$path" ]] &&
        [[ -n "$(find "$path" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]] &&
        [[ ! -f "$marker" ]] &&
        [[ "$force" -eq 0 ]]; then
        echo "install dir exists and has no $MARKER_FILE; skip cleaning: $path"
        echo "pass --force-clean to remove existing contents before packaging"
        return
    fi

    if [[ -d "$path" ]]; then
        find "$path" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
    else
        mkdir -p "$path"
    fi
}

find_executable() {
    local build_path="$1"
    local config_name="$2"
    local candidates=(
        "$build_path/bin/$PROJECT_NAME"
        "$build_path/bin/$config_name/$PROJECT_NAME"
        "$build_path/$config_name/bin/$PROJECT_NAME"
    )

    for candidate in "${candidates[@]}"; do
        if [[ -x "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return
        fi
    done

    local match
    match="$(find "$build_path" -type f -name "$PROJECT_NAME" -perm -111 -print 2>/dev/null | sort | head -n 1)"
    [[ -n "$match" ]] || die "cannot find executable $PROJECT_NAME under $build_path"
    printf '%s\n' "$match"
}

get_detected_config() {
    local build_path="$1"
    local requested="$2"

    case "$requested" in
        debug|release)
            printf '%s\n' "$requested"
            return
            ;;
        auto)
            ;;
        *)
            die "invalid config: $requested"
            ;;
    esac

    local build_type
    build_type="$(read_cmake_cache_value "$build_path/CMakeCache.txt" "CMAKE_BUILD_TYPE" || true)"
    build_type="$(printf '%s' "$build_type" | tr '[:upper:]' '[:lower:]')"

    case "$build_type" in
        debug)
            printf 'debug\n'
            return
            ;;
        release|relwithdebinfo|minsizerel)
            printf 'release\n'
            return
            ;;
    esac

    if find "$build_path/$PROJECT_NAME" -type f -name "lib${PROJECT_NAME}_*d.so*" -print -quit 2>/dev/null | grep -q .; then
        printf 'debug\n'
    else
        printf 'release\n'
    fi
}

copy_directory_filtered() {
    local source="$1"
    local destination="$2"
    local include_debug="$3"

    [[ -d "$source" ]] || die "cannot find directory: $source"
    mkdir -p "$destination"

    while IFS= read -r -d '' item; do
        local relative="${item#"$source"/}"
        local target="$destination/$relative"

        if [[ -d "$item" && ! -L "$item" ]]; then
            mkdir -p "$target"
            continue
        fi

        local name
        name="$(basename "$item")"
        case "$name" in
            *.a|*.la|*.o|*.obj|*.exp|*.lib|*.ilk)
                continue
                ;;
            *.debug)
                [[ "$include_debug" -eq 1 ]] || continue
                ;;
        esac

        mkdir -p "$(dirname "$target")"
        cp -a "$item" "$target"
    done < <(find "$source" -mindepth 1 -print0)

    echo "copy $source -> $destination"
}

copy_project_runtime() {
    local build_path="$1"
    local output_path="$2"
    local include_debug="$3"
    local module_root="$build_path/$PROJECT_NAME"
    local project_qml_root="$output_path/qml/$PROJECT_NAME"
    local lib_dir="$output_path/lib"

    [[ -d "$module_root" ]] || die "cannot find QML module output: $module_root"

    copy_directory_filtered "$module_root" "$project_qml_root" "$include_debug"
    mkdir -p "$lib_dir"

    while IFS= read -r -d '' so_file; do
        cp -a "$so_file" "$lib_dir/"
        echo "copy $so_file -> $lib_dir/$(basename "$so_file")"
    done < <(find "$module_root" \( -type f -o -type l \) -name "*.so*" -print0)

    local bin_dir="$build_path/bin"
    if [[ -d "$bin_dir" ]]; then
        while IFS= read -r -d '' so_file; do
            cp -a "$so_file" "$lib_dir/"
            echo "copy $so_file -> $lib_dir/$(basename "$so_file")"
        done < <(find "$bin_dir" -maxdepth 1 \( -type f -o -type l \) -name "*.so*" -print0)
    fi
}

normalize_qt_root_from_cmake_dir() {
    local value="$1"
    value="${value//\\//}"

    case "$value" in
        */lib/cmake/*)
            printf '%s\n' "${value%%/lib/cmake/*}"
            ;;
        */lib/*)
            printf '%s\n' "${value%%/lib/*}"
            ;;
        *)
            printf '%s\n' "$value"
            ;;
    esac
}

query_qt_path() {
    local key="$1"

    if command -v qtpaths6 >/dev/null 2>&1; then
        qtpaths6 --query "$key" 2>/dev/null || true
    elif command -v qtpaths >/dev/null 2>&1; then
        qtpaths --query "$key" 2>/dev/null || true
    fi
}

first_existing_dir() {
    local candidate

    for candidate in "$@"; do
        [[ -n "$candidate" && -d "$candidate" ]] || continue
        printf '%s\n' "$candidate"
        return
    done

    return 0
}

find_qt_roots() {
    local build_path="$1"
    local roots=()
    local value

    for env_name in Qt6_ROOT QT6_ROOT Qt_ROOT QT_ROOT QTDIR; do
        value="${!env_name:-}"
        [[ -n "$value" ]] && roots+=("$value")
    done

    value="$(read_cmake_set "$REPO_ROOT/cmake/ConfigQT.cmake" "Qt6_ROOT" || true)"
    [[ -n "$value" ]] && roots+=("$value")

    for key in Qt6Core_DIR Qt6_DIR; do
        value="$(read_cmake_cache_value "$build_path/CMakeCache.txt" "$key" || true)"
        [[ -n "$value" ]] && roots+=("$(normalize_qt_root_from_cmake_dir "$value")")
    done

    if command -v qtpaths6 >/dev/null 2>&1; then
        value="$(qtpaths6 --install-prefix 2>/dev/null || true)"
        [[ -n "$value" ]] && roots+=("$value")
    elif command -v qtpaths >/dev/null 2>&1; then
        value="$(qtpaths --install-prefix 2>/dev/null || true)"
        [[ -n "$value" ]] && roots+=("$value")
    fi

    printf '%s\n' "${roots[@]}" | awk 'NF && !seen[$0]++'
}

copy_qt_runtime() {
    local build_path="$1"
    local output_path="$2"
    local explicit_qt_root="$3"

    if [[ "$SKIP_QT" -eq 1 ]]; then
        echo "skip Qt qml/plugins copy"
        return
    fi

    local qt_root=""
    if [[ -n "$explicit_qt_root" ]]; then
        qt_root="$explicit_qt_root"
    else
        while IFS= read -r candidate; do
            if [[ -d "$candidate" ]]; then
                qt_root="$candidate"
                break
            fi
        done < <(find_qt_roots "$build_path")
    fi

    if [[ -z "$qt_root" || ! -d "$qt_root" ]]; then
        warn "Qt root was not found; ldd dependencies were still copied, but Qt qml/plugins may be missing"
        warn "pass --qt-root <path> if this package cannot start"
        return
    fi

    echo "use Qt root: $qt_root"

    local plugin_dir
    local qml_dir
    local translations_dir

    plugin_dir="$(first_existing_dir \
        "$qt_root/plugins" \
        "$qt_root/lib/qt6/plugins" \
        "$qt_root/lib64/qt6/plugins" \
        "$qt_root/lib/x86_64-linux-gnu/qt6/plugins" \
        "$(query_qt_path QT_INSTALL_PLUGINS)")"

    qml_dir="$(first_existing_dir \
        "$qt_root/qml" \
        "$qt_root/lib/qt6/qml" \
        "$qt_root/lib64/qt6/qml" \
        "$qt_root/lib/x86_64-linux-gnu/qt6/qml" \
        "$(query_qt_path QT_INSTALL_QML)")"

    translations_dir="$(first_existing_dir \
        "$qt_root/translations" \
        "$qt_root/lib/qt6/translations" \
        "$qt_root/lib64/qt6/translations" \
        "$qt_root/lib/x86_64-linux-gnu/qt6/translations" \
        "$(query_qt_path QT_INSTALL_TRANSLATIONS)")"

    if [[ -n "$plugin_dir" ]]; then
        mkdir -p "$output_path/plugins"
        cp -a "$plugin_dir/." "$output_path/plugins/"
        echo "copy $plugin_dir -> $output_path/plugins"
    else
        warn "Qt plugin directory was not found"
    fi

    if [[ -n "$qml_dir" ]]; then
        mkdir -p "$output_path/qml"
        cp -a "$qml_dir/." "$output_path/qml/"
        echo "copy $qml_dir -> $output_path/qml"
    else
        warn "Qt QML directory was not found"
    fi

    if [[ -n "$translations_dir" ]]; then
        mkdir -p "$output_path/translations"
        cp -a "$translations_dir/." "$output_path/translations/"
        echo "copy $translations_dir -> $output_path/translations"
    fi
}

list_ldd_dependencies() {
    local elf_file="$1"

    ldd "$elf_file" 2>/dev/null | awk '
        /=> not found/ {
            print "MISSING:" $1
            next
        }
        /=>/ {
            for (i = 1; i <= NF; i++) {
                if ($i == "=>") {
                    print $(i + 1)
                    next
                }
            }
        }
        /^[[:space:]]*\// {
            print $1
        }
    '
}

should_skip_dependency() {
    local dependency="$1"
    local base
    base="$(basename "$dependency")"

    case "$base" in
        linux-vdso*|ld-linux*|libc.so.*|libm.so.*|libdl.so.*|libpthread.so.*|librt.so.*|libresolv.so.*|libutil.so.*|libnsl.so.*)
            return 0
            ;;
    esac

    if [[ "$COPY_SYSTEM_LIBS" -eq 0 ]]; then
        case "$dependency" in
            /lib/*|/lib64/*|/usr/lib/*|/usr/lib64/*)
                return 0
                ;;
        esac
    fi

    return 1
}

is_elf_file() {
    local path="$1"
    file "$path" 2>/dev/null | grep -q 'ELF'
}

copy_elf_dependencies() {
    local output_path="$1"
    local lib_dir="$output_path/lib"
    local processed="$output_path/.ldd_processed"
    local changed=1

    if ! command -v ldd >/dev/null 2>&1; then
        warn "ldd was not found; skip ELF dependency scan"
        return
    fi
    if ! command -v file >/dev/null 2>&1; then
        warn "file was not found; skip ELF dependency scan"
        return
    fi

    mkdir -p "$lib_dir"
    : > "$processed"

    while [[ "$changed" -eq 1 ]]; do
        changed=0

        while IFS= read -r -d '' elf_file; do
            is_elf_file "$elf_file" || continue
            grep -Fxq "$elf_file" "$processed" && continue
            printf '%s\n' "$elf_file" >> "$processed"

            while IFS= read -r dependency; do
                if [[ "$dependency" == MISSING:* ]]; then
                    warn "missing dependency for $elf_file: ${dependency#MISSING:}"
                    continue
                fi
                [[ "$dependency" = /* ]] || continue
                should_skip_dependency "$dependency" && continue

                local target="$lib_dir/$(basename "$dependency")"
                if [[ ! -e "$target" ]]; then
                    copy_file "$dependency" "$target"
                    changed=1
                fi
            done < <(LD_LIBRARY_PATH="$lib_dir:$output_path:${LD_LIBRARY_PATH:-}" list_ldd_dependencies "$elf_file")
        done < <(find "$output_path" -type f \( -perm -111 -o -name "*.so" -o -name "*.so.*" \) -print0)
    done

    rm -f "$processed"
}

patch_rpath_if_possible() {
    local output_path="$1"
    local lib_dir="$output_path/lib"

    if ! command -v patchelf >/dev/null 2>&1; then
        warn "patchelf was not found; run $output_path/run_${PROJECT_NAME}.sh to set library paths"
        return
    fi

    while IFS= read -r -d '' elf_file; do
        is_elf_file "$elf_file" || continue

        local elf_dir
        local relative_lib
        local rpath
        elf_dir="$(dirname "$elf_file")"

        relative_lib=""
        if command -v realpath >/dev/null 2>&1; then
            relative_lib="$(realpath --relative-to="$elf_dir" "$lib_dir" 2>/dev/null || true)"
        fi

        if [[ "$relative_lib" = "." ]]; then
            rpath="\$ORIGIN"
        elif [[ -n "$relative_lib" ]]; then
            rpath="\$ORIGIN/$relative_lib"
        else
            rpath="\$ORIGIN:\$ORIGIN/lib:\$ORIGIN/../lib:\$ORIGIN/../../lib:\$ORIGIN/../../../lib"
        fi

        chmod u+w "$elf_file" 2>/dev/null || true
        patchelf --set-rpath "$rpath" "$elf_file" || warn "failed to set rpath for $elf_file"
    done < <(find "$output_path" -type f \( -perm -111 -o -name "*.so" -o -name "*.so.*" \) -print0)
}

write_launcher() {
    local output_path="$1"
    local launcher="$output_path/run_${PROJECT_NAME}.sh"

    cat > "$launcher" <<'EOF'
#!/usr/bin/env bash
set -e

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export LD_LIBRARY_PATH="$APP_DIR/lib:$APP_DIR:${LD_LIBRARY_PATH:-}"
export QML2_IMPORT_PATH="$APP_DIR/qml:$APP_DIR:${QML2_IMPORT_PATH:-}"
export QML_IMPORT_PATH="$APP_DIR/qml:$APP_DIR:${QML_IMPORT_PATH:-}"
export QT_PLUGIN_PATH="$APP_DIR/plugins:${QT_PLUGIN_PATH:-}"

exec "$APP_DIR/dltool" "$@"
EOF

    chmod +x "$launcher"
}

write_qt_conf() {
    local output_path="$1"

    cat > "$output_path/qt.conf" <<'EOF'
[Paths]
Prefix=.
Libraries=lib
Plugins=plugins
Qml2Imports=qml
Translations=translations
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir|-BuildDir)
            [[ $# -ge 2 ]] || die "$1 requires a value"
            BUILD_DIR="$2"
            shift 2
            ;;
        --build-dir=*)
            BUILD_DIR="${1#*=}"
            shift
            ;;
        --install-dir|-InstallDir)
            [[ $# -ge 2 ]] || die "$1 requires a value"
            INSTALL_DIR="$2"
            shift 2
            ;;
        --install-dir=*)
            INSTALL_DIR="${1#*=}"
            shift
            ;;
        --config|-Config)
            [[ $# -ge 2 ]] || die "$1 requires a value"
            CONFIG="$2"
            shift 2
            ;;
        --config=*)
            CONFIG="${1#*=}"
            shift
            ;;
        --qt-root|-QtRoot)
            [[ $# -ge 2 ]] || die "$1 requires a value"
            QT_ROOT="$2"
            shift 2
            ;;
        --qt-root=*)
            QT_ROOT="${1#*=}"
            shift
            ;;
        --skip-qt|-SkipQt)
            SKIP_QT=1
            shift
            ;;
        --include-debug|-IncludeDebug)
            INCLUDE_DEBUG=1
            shift
            ;;
        --skip-system-libs)
            COPY_SYSTEM_LIBS=0
            shift
            ;;
        --copy-system-libs)
            COPY_SYSTEM_LIBS=1
            shift
            ;;
        --no-clean|-NoClean)
            NO_CLEAN=1
            shift
            ;;
        --force-clean|-ForceClean)
            FORCE_CLEAN=1
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            die "unknown argument: $1"
            ;;
    esac
done

RESOLVED_BUILD_DIR="$(resolve_project_path "$BUILD_DIR")"
RESOLVED_INSTALL_DIR="$(resolve_project_path "$INSTALL_DIR")"

[[ -d "$RESOLVED_BUILD_DIR" ]] || die "build dir does not exist: $RESOLVED_BUILD_DIR"

DETECTED_CONFIG="$(get_detected_config "$RESOLVED_BUILD_DIR" "$CONFIG")"
CONFIG_DIR_NAME="Release"
if [[ "$DETECTED_CONFIG" = "debug" ]]; then
    CONFIG_DIR_NAME="Debug"
fi

SOURCE_EXE="$(find_executable "$RESOLVED_BUILD_DIR" "$CONFIG_DIR_NAME")"

clear_install_directory "$RESOLVED_INSTALL_DIR" "$((1 - NO_CLEAN))" "$FORCE_CLEAN"

PACKAGED_EXE="$RESOLVED_INSTALL_DIR/$(basename "$SOURCE_EXE")"
copy_file "$SOURCE_EXE" "$PACKAGED_EXE"
copy_project_runtime "$RESOLVED_BUILD_DIR" "$RESOLVED_INSTALL_DIR" "$INCLUDE_DEBUG"
copy_qt_runtime "$RESOLVED_BUILD_DIR" "$RESOLVED_INSTALL_DIR" "$QT_ROOT"
copy_elf_dependencies "$RESOLVED_INSTALL_DIR"
patch_rpath_if_possible "$RESOLVED_INSTALL_DIR"
write_launcher "$RESOLVED_INSTALL_DIR"
write_qt_conf "$RESOLVED_INSTALL_DIR"

cat > "$RESOLVED_INSTALL_DIR/$MARKER_FILE" <<EOF
generated_by=tools/package_app.sh
build_dir=$RESOLVED_BUILD_DIR
config=$DETECTED_CONFIG
qt_root=$QT_ROOT
EOF

echo ""
echo "package complete: $RESOLVED_INSTALL_DIR"
echo "run on Linux: $RESOLVED_INSTALL_DIR/run_${PROJECT_NAME}.sh"
