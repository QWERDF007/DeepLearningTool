#!/usr/bin/env bash
set -euo pipefail

# DeepLearningTool dependency link script for Unix-like shells.
# Runtime dependency names are listed in tools/dependencies.

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

trim() {
    local value="$1"
    value="${value#"${value%%[![:space:]]*}"}"
    value="${value%"${value##*[![:space:]]}"}"
    printf '%s' "$value"
}

platform_key() {
    case "$(uname -s)" in
        Linux*) echo "linux" ;;
        Darwin*) echo "macos" ;;
        MINGW*|MSYS*|CYGWIN*) echo "windows" ;;
        *) echo "unknown" ;;
    esac
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

read_cmake_set() {
    local cmake_file="$1"
    local name="$2"
    [[ -f "$cmake_file" ]] || return 0

    sed -nE "s#^[[:space:]]*set[[:space:]]*\\([[:space:]]*${name}[[:space:]]+\"([^\"]+)\".*#\\1#p" "$cmake_file" | tail -n 1
}

link_dltool() {
    link_path "build/dltool" "build/bin/dltool"

    if [[ ! -d "build/bin/dltool" ]]; then
        warn "skip dltool shared library links, missing build/bin/dltool"
        return 0
    fi

    case "$(platform_key)" in
        windows)
            find -L "build/bin/dltool" -type f -name "dltool_*.dll" -print0 |
                while IFS= read -r -d '' library; do
                    link_path "$library" "build/bin/$(basename "$library")"
                done
            ;;
        *)
            find -L "build/bin/dltool" -type f \( -name "libdltool_*.so" -o -name "libdltool_*.so.*" -o -name "libdltool_*.dylib" \) -print0 |
                while IFS= read -r -d '' library; do
                    link_path "$library" "build/bin/$(basename "$library")"
                done
            ;;
    esac
}

dependency_section=""
dependency_cmake=""
dependency_root_spec=""
dependency_root_var=""
dependency_root=""
dependency_enabled=1
dependency_dests=()

is_direct_root() {
    local root="$1"
    [[ "$root" == *":"* || "$root" == *"/"* || "$root" == *"\\"* || "$root" == .* || "$root" == "~"* ]]
}

link_dependency_file() {
    local runtime="$1"
    local name
    local dest

    name="$(basename "$runtime")"
    for dest in "${dependency_dests[@]}"; do
        link_path "$runtime" "$dest/$name"
    done
}

link_dependency_pattern() {
    local pattern="$1"
    local runtime
    local matches

    [[ "$dependency_enabled" == "1" ]] || return 0

    if [[ -z "$dependency_root" ]]; then
        warn "skip dependency $dependency_section pattern $pattern, root was not configured"
        return 0
    fi

    if [[ "${#dependency_dests[@]}" -eq 0 ]]; then
        warn "skip dependency $dependency_section pattern $pattern, destination was not configured"
        return 0
    fi

    shopt -s nullglob
    matches=( "$dependency_root"/$pattern )
    shopt -u nullglob

    if [[ "${#matches[@]}" -eq 0 && "$pattern" != *"*"* && "$pattern" != *"?"* ]]; then
        link_dependency_file "$dependency_root/$pattern"
        return 0
    fi

    for runtime in "${matches[@]}"; do
        [[ -f "$runtime" ]] || continue
        link_dependency_file "$runtime"
    done
}

process_dependency_line() {
    local line="$1"
    local key
    local value

    line="${line%$'\r'}"
    line="$(trim "$line")"
    [[ -n "$line" ]] || return 0
    [[ "${line:0:1}" != "#" && "${line:0:1}" != ";" ]] || return 0

    if [[ "$line" == \[*\] ]]; then
        dependency_section="${line:1:${#line}-2}"
        dependency_cmake=""
        dependency_root_spec=""
        dependency_root_var=""
        dependency_root=""
        dependency_enabled=1
        dependency_dests=()
        return 0
    fi

    [[ "$line" == *"="* ]] || return 0
    key="$(trim "${line%%=*}")"
    value="$(trim "${line#*=}")"

    case "$key" in
        cmake)
            dependency_cmake="$value"
            ;;
        root)
            dependency_root_spec="$value"
            dependency_root_var="$value"
            dependency_root=""

            if is_direct_root "$dependency_root_spec" || [[ -z "$dependency_cmake" ]]; then
                dependency_root="$dependency_root_spec"
                dependency_enabled=1
                return 0
            fi

            dependency_root="$(read_cmake_set "$dependency_cmake" "$dependency_root_var" || true)"
            if [[ -z "$dependency_root" ]]; then
                warn "skip dependency $dependency_section, $dependency_root_var was not found in $dependency_cmake"
                dependency_enabled=0
            else
                dependency_enabled=1
            fi
            ;;
        dest)
            dependency_dests+=("$value")
            ;;
        all)
            link_dependency_pattern "$value"
            ;;
        windows|linux|macos)
            if [[ "$key" == "$(platform_key)" ]]; then
                link_dependency_pattern "$value"
            fi
            ;;
    esac
}

link_external_dependencies() {
    local dependencies_file="tools/dependencies"
    local line

    if [[ ! -f "$dependencies_file" ]]; then
        warn "skip external dependency links, missing $dependencies_file"
        return 0
    fi

    while IFS= read -r line || [[ -n "$line" ]]; do
        process_dependency_line "$line"
    done < "$dependencies_file"
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

link_external_dependencies
echo "link external dependencies success"

link_test
echo "link test success"
