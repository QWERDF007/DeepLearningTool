"""DeepLearningTool 发布包生成脚本。

该脚本只打包 release 运行时：从 CMake build 目录收集可执行文件、项目 DLL、
YAML 中声明的第三方运行库、系统运行库和 Qt 运行时，输出到 install 目录。
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

if str(Path(__file__).resolve().parent) not in sys.path:
    sys.path.insert(0, str(Path(__file__).resolve().parent))

from dependency_utils import (
    MARKER_FILE,
    PROJECT_DLL_PREFIX,
    PROJECT_NAME,
    REPO_ROOT,
    build_dll_variant_sets,
    copy_file,
    dependency_matches_config,
    dependency_patterns,
    dll_matches_config,
    expand_dependency_pattern,
    immediate_project_dlls,
    is_project_dll,
    load_dependencies,
    platform_key,
    read_cmake_cache_value,
    read_cmake_set_expanded,
    resolve_dependency_root,
    resolve_project_path,
    should_skip_config_dir,
    warn,
)

# build/bin 里可能有 link_dependencies.py 创建的第三方链接。
# 打包时只允许复制本项目本地构建、但不属于 dltool_* 的少数 DLL。
LOCAL_BIN_DLL_NAMES = {"quickui.dll"}
PROJECT_NAME_LONG = "DeepLearningTool"


def parse_args() -> argparse.Namespace:
    """解析命令行参数。"""

    parser = argparse.ArgumentParser(description="Package DeepLearningTool release runtime.")
    parser.add_argument("--build-dir", "-BuildDir", default="build")
    parser.add_argument("--install-dir", "-InstallDir", default=None)
    parser.add_argument("--config", "-Config", choices=["release"], default="release")
    parser.add_argument("--dependencies", default="tools/dependencies.yaml")
    parser.add_argument("--windeployqt", "-WinDeployQt", default="")
    parser.add_argument("--qt-root", "-QtRoot", default="")
    parser.add_argument("--skip-windeployqt", "-SkipWinDeployQt", "--skip-qt", action="store_true")
    parser.add_argument("--skip-dependencies", "--skip-manifest-dependencies", action="store_true")
    parser.add_argument("--include-pdb", "-IncludePdb", "--include-debug", "-IncludeDebug", action="store_true")
    parser.add_argument("--include-qml-module-dir", "-IncludeQmlModuleDir", action="store_true")
    parser.add_argument("--no-clean", "-NoClean", action="store_true")
    parser.add_argument("--force-clean", "-ForceClean", action="store_true")
    parser.add_argument("--skip-system-libs", action="store_true")
    return parser.parse_args()


def project_version(repo_root: Path = REPO_ROOT) -> str:
    """从根 CMakeLists.txt 读取 project VERSION。"""

    cmake_lists = repo_root / "CMakeLists.txt"
    text = cmake_lists.read_text(encoding="utf-8", errors="ignore")
    match = re.search(r"VERSION\s+(\d+\.\d+\.\d+)", text)
    if not match:
        raise RuntimeError(f"cannot find project VERSION in {cmake_lists}")
    return match.group(1)


def default_install_dir() -> Path:
    """默认发布包输出目录：install/DeepLearningTool-<version>。"""

    return REPO_ROOT / "install" / f"{PROJECT_NAME_LONG}-{project_version()}"


def find_executable(build_dir: Path) -> Path:
    """在构建目录中查找应用程序可执行文件。

    Args:
        build_dir: CMake 构建目录。

    Returns:
        解析后的可执行文件路径。
    """

    exe_name = f"{PROJECT_NAME}.exe" if os.name == "nt" else PROJECT_NAME
    candidates = [
        build_dir / "bin" / exe_name,
        build_dir / "bin" / "Release" / exe_name,
        build_dir / "Release" / "bin" / exe_name,
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve(strict=True)
    matches = sorted(build_dir.rglob(exe_name), key=lambda path: len(str(path)))
    if matches:
        return matches[0].resolve(strict=True)
    raise RuntimeError(f"cannot find {exe_name} under {build_dir}")


def target_architecture(build_dir: Path) -> str:
    """推断 Windows 发布包需要的目标架构名称。"""

    platform = read_cmake_cache_value(build_dir / "CMakeCache.txt", "CMAKE_GENERATOR_PLATFORM")
    if platform:
        value = platform.lower()
        if value == "win32":
            return "x86"
        if value in {"x64", "x86", "arm64"}:
            return value
    machine = os.environ.get("PROCESSOR_ARCHITECTURE", "").upper()
    if machine == "ARM64":
        return "arm64"
    if machine == "AMD64":
        return "x64"
    return "x86"


def clear_install_directory(path: Path, clean: bool, force: bool) -> None:
    """按安全规则准备发布包输出目录。

    Args:
        path: 输出目录。
        clean: 是否清理已有内容。
        force: 是否允许清理没有 marker 的非空目录。
    """

    if not clean:
        path.mkdir(parents=True, exist_ok=True)
        return
    marker = path / MARKER_FILE
    # 没有 marker 的目录可能不是上一次打包产物，默认拒绝清理，避免误删用户文件。
    if path.is_dir() and any(path.iterdir()) and not marker.exists() and not force:
        raise RuntimeError(
            f"install dir exists and has no {MARKER_FILE}: {path}; "
            "pass --force-clean to remove it or --no-clean to reuse it"
        )
    if path.exists():
        for child in path.iterdir():
            if child.is_dir() and not child.is_symlink():
                shutil.rmtree(child)
            else:
                child.unlink()
    else:
        path.mkdir(parents=True)


def copy_directory_filtered(source: Path, destination: Path, include_pdb: bool, config: str) -> None:
    """复制目录树，并过滤中间文件、符号文件和不匹配配置的 DLL。"""

    dlls = list(source.rglob("*.dll"))
    debug_names, release_names = build_dll_variant_sets(dlls)
    for item in source.rglob("*"):
        rel = item.relative_to(source)
        if should_skip_config_dir(rel):
            continue
        target = destination / rel
        if item.is_dir():
            target.mkdir(parents=True, exist_ok=True)
            continue
        suffix = item.suffix.lower()
        if suffix in {".exp", ".lib", ".ilk", ".obj"}:
            continue
        if suffix == ".pdb" and not include_pdb:
            continue
        if suffix == ".dll" and not dll_matches_config(item, config, debug_names, release_names):
            continue
        copy_file(item, target)
    print(f"copy {source} -> {destination}")


def copy_project_runtime(build_dir: Path, install_dir: Path, include_pdb: bool, include_qml_module_dir: bool, config: str) -> None:
    """复制项目自身运行时文件。

    Windows 下项目 DLL 只从模块输出目录复制，避免 build/bin 中的旧链接污染发布包。
    """

    module_root = build_dir / PROJECT_NAME
    if not module_root.is_dir():
        raise RuntimeError(f"cannot find QML module output: {module_root}")

    if include_qml_module_dir:
        qml_target = install_dir / PROJECT_NAME if os.name == "nt" else install_dir / "qml" / PROJECT_NAME
        copy_directory_filtered(module_root, qml_target, include_pdb, config)

    platform = platform_key()
    if platform == "windows":
        # 项目 DLL 只从 build/dltool/<module> 取，避免 build/bin 中的旧链接覆盖 release 产物。
        project_dlls = immediate_project_dlls(module_root)
        debug_names, release_names = build_dll_variant_sets(project_dlls)
        for dll in project_dlls:
            if dll_matches_config(dll, config, debug_names, release_names):
                copy_file(dll, install_dir / dll.name)

        bin_dlls = sorted((build_dir / "bin").glob("*.dll")) if (build_dir / "bin").is_dir() else []
        debug_names, release_names = build_dll_variant_sets(bin_dlls)
        for dll in bin_dlls:
            if is_project_dll(dll):
                continue
            if dll.name.lower() not in LOCAL_BIN_DLL_NAMES:
                continue
            if dll_matches_config(dll, config, debug_names, release_names):
                copy_file(dll, install_dir / dll.name)

        if include_pdb:
            for pdb in sorted((build_dir / "bin").glob("*.pdb")):
                copy_file(pdb, install_dir / pdb.name)
        return

    lib_dir = install_dir / "lib"
    lib_dir.mkdir(parents=True, exist_ok=True)
    for so_file in sorted(module_root.rglob("*.so*")):
        copy_file(so_file, lib_dir / so_file.name)
    bin_dir = build_dir / "bin"
    if bin_dir.is_dir():
        for so_file in sorted(bin_dir.glob("*.so*")):
            copy_file(so_file, lib_dir / so_file.name)


def copy_settings_config(install_dir: Path) -> None:
    source = REPO_ROOT / "config" / "settings"
    if not source.is_dir():
        warn(f"settings config directory was not found: {source}")
        return
    target = install_dir / "config" / "settings"
    shutil.copytree(source, target, dirs_exist_ok=True)
    print(f"copy {source} -> {target}")


def copy_model_configs(install_dir: Path) -> None:
    source = REPO_ROOT / "config" / "models"
    if not source.is_dir():
        warn(f"model config directory was not found: {source}")
        return
    target = install_dir / "config" / "models"
    shutil.copytree(source, target, dirs_exist_ok=True)
    print(f"copy {source} -> {target}")


def copy_easytrain_python_runtime(install_dir: Path) -> None:
    source = REPO_ROOT / "3rdparty" / "EasyTrain" / "src" / "python"
    if not source.is_dir():
        warn(f"EasyTrain python runtime directory was not found: {source}")
        return
    target = install_dir / "python"
    shutil.copytree(source, target, dirs_exist_ok=True, ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "*.pyo"))
    print(f"copy {source} -> {target}")


def copy_yaml_dependencies(build_dir: Path, install_dir: Path, dependency_file: Path, config: str) -> None:
    """按 YAML 清单复制第三方运行库。"""

    # 第三方运行库统一由 dependencies.yaml 声明，release 打包会跳过 config: debug 的条目。
    platform = platform_key()
    target_dir = install_dir if platform == "windows" else install_dir / "lib"
    target_dir.mkdir(parents=True, exist_ok=True)

    for dep in load_dependencies(dependency_file):
        if not dependency_matches_config(dep, config):
            continue
        patterns = dependency_patterns(dep, platform)
        if not patterns:
            continue
        root = resolve_dependency_root(dep, build_dir)
        if root is None:
            warn(f"skip dependency {dep.get('name', '<unnamed>')}, root {dep.get('root')} was not found")
            continue
        matched: list[Path] = []
        for pattern in patterns:
            matches = expand_dependency_pattern(root, pattern)
            if not matches and "*" not in pattern and "?" not in pattern:
                warn(f"dependency file was not found: {root / pattern}")
            matched.extend(matches)
        debug_names, release_names = build_dll_variant_sets(matched)
        for runtime in matched:
            if platform == "windows" and not dll_matches_config(runtime, config, debug_names, release_names):
                continue
            copy_file(runtime, target_dir / runtime.name)


def version_key(path: Path) -> tuple[int, ...]:
    """把路径名中的版本号转换为可排序的数字元组。"""

    parts = []
    for part in re.split(r"[._-]", path.name):
        try:
            parts.append(int(part))
        except ValueError:
            parts.append(0)
    return tuple(parts)


def visual_studio_roots() -> list[Path]:
    """查找本机可能存在的 Visual Studio 安装根目录。"""

    roots: list[Path] = []
    vcinstall = os.environ.get("VCINSTALLDIR")
    if vcinstall:
        roots.append(Path(vcinstall).resolve(strict=False).parent)
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if vswhere.is_file():
        result = subprocess.run(
            [str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        if result.stdout.strip():
            roots.append(Path(result.stdout.strip()))
    for year in ("2022", "2019"):
        for edition in ("Community", "Professional", "Enterprise", "BuildTools"):
            candidate = Path(r"C:\Program Files (x86)\Microsoft Visual Studio") / year / edition
            if candidate.is_dir():
                roots.append(candidate)
            candidate = Path(r"C:\Program Files\Microsoft Visual Studio") / year / edition
            if candidate.is_dir():
                roots.append(candidate)
    unique: list[Path] = []
    seen: set[str] = set()
    for root in roots:
        key = str(root).lower()
        if key not in seen:
            unique.append(root)
            seen.add(key)
    return unique


def copy_msvc_runtime(install_dir: Path, architecture: str) -> None:
    """复制 MSVC redistributable 运行库到发布包根目录。"""

    for root in visual_studio_roots():
        redist = root / "VC" / "Redist" / "MSVC"
        if not redist.is_dir():
            continue
        versions = sorted((p for p in redist.iterdir() if p.is_dir()), key=version_key, reverse=True)
        for version in versions:
            runtime_dir = version / architecture
            if not runtime_dir.is_dir():
                continue
            copied = False
            for dll in sorted(runtime_dir.rglob("*.dll")):
                copy_file(dll, install_dir / dll.name)
                copied = True
            if copied:
                return
    warn("MSVC runtime DLLs were not found")


def copy_windows_sdk_dependencies(install_dir: Path, architecture: str) -> None:
    """复制 Windows SDK 中的 DirectX 编译运行库。"""

    kits_root = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Windows Kits" / "10" / "bin"
    if not kits_root.is_dir():
        warn("Windows SDK bin directory was not found")
        return
    versions = sorted((p for p in kits_root.iterdir() if p.is_dir()), key=version_key, reverse=True)
    if not versions:
        warn("Windows SDK bin directory was not found")
        return
    kit_bin = versions[0]
    for name in ("dxcompiler.dll", "dxil.dll"):
        candidate = kit_bin / architecture / name
        if candidate.is_file():
            copy_file(candidate, install_dir / name)


def qt_roots(build_dir: Path) -> list[Path]:
    """按优先级收集 Qt 安装根目录候选项。"""

    roots: list[Path] = []
    # 优先使用 dependencies.yaml 清单和 CMake cache 中的 Qt6，最后才回退到环境变量和 PATH。
    # 这样可以避免 PATH 中旧 Qt 的 windeployqt 被误用。
    dep_file = REPO_ROOT / "tools" / "dependencies.yaml"
    if dep_file.is_file():
        try:
            for dep in load_dependencies(dep_file):
                if dep.get("name") in ("qt", "qt6"):
                    default_val = dep.get("default")
                    if default_val:
                        roots.append(Path(default_val))
        except Exception:
            pass
    config_root = read_cmake_set_expanded(REPO_ROOT / "cmake" / "ConfigQT.cmake", "Qt6_ROOT")
    if config_root:
        roots.append(Path(config_root))
    for key in ("Qt6Core_DIR", "Qt6_DIR"):
        value = read_cmake_cache_value(build_dir / "CMakeCache.txt", key)
        if not value:
            continue
        normalized = value.replace("\\", "/")
        marker = "/lib/"
        if marker in normalized:
            roots.append(Path(normalized.split(marker)[0]))
    for env_name in ("Qt6_ROOT", "QT6_ROOT", "Qt_ROOT", "QT_ROOT", "QTDIR"):
        value = os.environ.get(env_name)
        if value:
            roots.append(Path(value))
    unique: list[Path] = []
    seen: set[str] = set()
    for root in roots:
        resolved = resolve_project_path(root)
        key = str(resolved).lower()
        if key not in seen:
            unique.append(resolved)
            seen.add(key)
    return unique


def find_windeployqt(build_dir: Path, explicit: str) -> Path | None:
    """定位 Windows Qt 部署工具 windeployqt.exe。"""

    if explicit:
        path = resolve_project_path(explicit)
        return path if path.is_file() else None
    for root in qt_roots(build_dir):
        candidate = root / "bin" / "windeployqt.exe"
        if candidate.is_file():
            return candidate
    found = shutil.which("windeployqt.exe")
    if found:
        return Path(found)
    return None


def invoke_windeployqt(build_dir: Path, install_dir: Path, exe: Path, explicit: str, skip: bool) -> None:
    """运行 windeployqt 部署 Windows Qt 运行时。"""

    if skip:
        print("skip windeployqt")
        return
    tool = find_windeployqt(build_dir, explicit)
    if tool is None:
        raise RuntimeError("cannot find windeployqt; pass --windeployqt <path>")
    qml_dir = build_dir / PROJECT_NAME
    command = [str(tool), "--dir", str(install_dir), "--qmldir", str(qml_dir), "--release", str(exe)]
    print("run " + " ".join(command))
    subprocess.run(command, check=True)


def write_windows_marker(install_dir: Path, build_dir: Path, architecture: str, skip_dependencies: bool, include_qml_module_dir: bool) -> None:
    """写入 Windows 发布包 marker 文件。"""

    lines = [
        "generated_by=tools/package_app.py",
        f"build_dir={build_dir}",
        "config=release",
        f"architecture={architecture}",
        f"skip_dependencies={int(skip_dependencies)}",
        f"include_qml_module_dir={int(include_qml_module_dir)}",
    ]
    (install_dir / MARKER_FILE).write_text("\n".join(lines) + "\n", encoding="utf-8")


def first_existing_dir(candidates: list[Path]) -> Path | None:
    """返回候选列表中第一个存在的目录。"""

    for candidate in candidates:
        if candidate.is_dir():
            return candidate
    return None


def copy_unix_qt_runtime(build_dir: Path, install_dir: Path, qt_root_arg: str) -> None:
    """复制 Linux/macOS 发布包所需的 Qt QML、插件和翻译目录。"""

    if qt_root_arg:
        qt_root = resolve_project_path(qt_root_arg)
    else:
        qt_root = next((root for root in qt_roots(build_dir) if root.is_dir()), None)
    if qt_root is None:
        warn("Qt root was not found; pass --qt-root if the package cannot start")
        return

    plugin_dir = first_existing_dir([
        qt_root / "plugins",
        qt_root / "lib" / "qt6" / "plugins",
        qt_root / "lib64" / "qt6" / "plugins",
        qt_root / "lib" / "x86_64-linux-gnu" / "qt6" / "plugins",
    ])
    qml_dir = first_existing_dir([
        qt_root / "qml",
        qt_root / "lib" / "qt6" / "qml",
        qt_root / "lib64" / "qt6" / "qml",
        qt_root / "lib" / "x86_64-linux-gnu" / "qt6" / "qml",
    ])
    translations_dir = first_existing_dir([
        qt_root / "translations",
        qt_root / "lib" / "qt6" / "translations",
        qt_root / "lib64" / "qt6" / "translations",
        qt_root / "lib" / "x86_64-linux-gnu" / "qt6" / "translations",
    ])

    for src, dst_name in ((plugin_dir, "plugins"), (qml_dir, "qml"), (translations_dir, "translations")):
        if src and src.is_dir():
            shutil.copytree(src, install_dir / dst_name, dirs_exist_ok=True, symlinks=True)
            print(f"copy {src} -> {install_dir / dst_name}")


def write_unix_launcher(install_dir: Path) -> None:
    """生成设置运行时搜索路径的 Unix 启动脚本。"""

    launcher = install_dir / f"run_{PROJECT_NAME}.sh"
    launcher.write_text(
        "#!/usr/bin/env bash\n"
        "set -e\n"
        'APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"\n'
        'export LD_LIBRARY_PATH="$APP_DIR/lib:$APP_DIR:${LD_LIBRARY_PATH:-}"\n'
        'export QML2_IMPORT_PATH="$APP_DIR/qml:$APP_DIR:${QML2_IMPORT_PATH:-}"\n'
        'export QML_IMPORT_PATH="$APP_DIR/qml:$APP_DIR:${QML_IMPORT_PATH:-}"\n'
        'export QT_PLUGIN_PATH="$APP_DIR/plugins:${QT_PLUGIN_PATH:-}"\n'
        'exec "$APP_DIR/dltool" "$@"\n',
        encoding="utf-8",
    )
    launcher.chmod(0o755)


def write_qt_conf(install_dir: Path) -> None:
    """生成 Qt 运行时路径配置文件。"""

    (install_dir / "qt.conf").write_text(
        "[Paths]\nPrefix=.\nLibraries=lib\nPlugins=plugins\nQml2Imports=qml\nTranslations=translations\n",
        encoding="utf-8",
    )


def cleanup_release_debug_artifacts(install_dir: Path) -> None:
    """删除 release 发布包中不需要的调试辅助文件。"""

    # windeployqt --release 仍可能复制 QML 调试插件；发布包中不需要这些 qmldbg_* 文件。
    for tool_dir in [path for path in install_dir.rglob("qmltooling") if path.is_dir()]:
        for dll in tool_dir.rglob("qmldbg_*.dll"):
            if dll.is_file() or dll.is_symlink():
                dll.unlink()
                print(f"remove {dll}")
        for subdir in sorted((path for path in tool_dir.rglob("*") if path.is_dir()), key=lambda path: len(path.parts), reverse=True):
            try:
                subdir.rmdir()
            except OSError:
                pass
        try:
            tool_dir.rmdir()
            print(f"remove {tool_dir}")
        except OSError:
            pass


def command_exists(name: str) -> bool:
    """判断命令是否能在 PATH 中找到。"""

    return shutil.which(name) is not None


def is_elf_file(path: Path) -> bool:
    """判断文件是否是 ELF 文件。"""

    if not command_exists("file"):
        return False
    result = subprocess.run(
        ["file", str(path)],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    return result.returncode == 0 and "ELF" in result.stdout


def ldd_dependencies(path: Path) -> tuple[list[Path], list[str]]:
    """解析 ldd 输出，返回已解析依赖和缺失依赖名称。"""

    result = subprocess.run(
        ["ldd", str(path)],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    dependencies: list[Path] = []
    missing: list[str] = []
    for line in result.stdout.splitlines():
        stripped = line.strip()
        if "=> not found" in stripped:
            missing.append(stripped.split()[0])
            continue
        if "=>" in stripped:
            value = stripped.split("=>", 1)[1].strip().split()[0]
        else:
            value = stripped.split()[0] if stripped.startswith("/") else ""
        if value.startswith("/"):
            dependencies.append(Path(value))
    return dependencies, missing


def should_skip_unix_dependency(path: Path, copy_system_libs: bool) -> bool:
    """判断 Unix 动态库是否应从发布包中跳过。"""

    name = path.name
    if name.startswith(("linux-vdso", "ld-linux")):
        return True
    if name.startswith(("libc.so.", "libm.so.", "libdl.so.", "libpthread.so.", "librt.so.", "libresolv.so.", "libutil.so.", "libnsl.so.")):
        return True
    if not copy_system_libs:
        normalized = str(path).replace("\\", "/")
        return normalized.startswith(("/lib/", "/lib64/", "/usr/lib/", "/usr/lib64/"))
    return False


def copy_elf_dependencies(install_dir: Path, copy_system_libs: bool) -> None:
    """递归扫描发布包中的 ELF 文件并复制其动态库依赖。"""

    if not command_exists("ldd") or not command_exists("file"):
        warn("ldd or file was not found; skip ELF dependency scan")
        return

    lib_dir = install_dir / "lib"
    processed: set[Path] = set()
    changed = True
    while changed:
        changed = False
        candidates = [
            path
            for path in install_dir.rglob("*")
            if path.is_file() and (os.access(path, os.X_OK) or path.name.endswith(".so") or ".so." in path.name)
        ]
        for elf_file in candidates:
            resolved = elf_file.resolve(strict=False)
            if resolved in processed or not is_elf_file(elf_file):
                continue
            processed.add(resolved)
            dependencies, missing = ldd_dependencies(elf_file)
            for name in missing:
                warn(f"missing dependency for {elf_file}: {name}")
            for dependency in dependencies:
                if should_skip_unix_dependency(dependency, copy_system_libs):
                    continue
                target = lib_dir / dependency.name
                if target.exists():
                    continue
                copy_file(dependency, target)
                changed = True


def patch_rpath_if_possible(install_dir: Path) -> None:
    """在 patchelf 可用时给发布包内 ELF 文件设置相对 rpath。"""

    if not command_exists("patchelf"):
        warn(f"patchelf was not found; run {install_dir / f'run_{PROJECT_NAME}.sh'} to set library paths")
        return
    lib_dir = install_dir / "lib"
    for elf_file in install_dir.rglob("*"):
        if not elf_file.is_file() or not is_elf_file(elf_file):
            continue
        try:
            relative_lib = os.path.relpath(lib_dir, elf_file.parent).replace("\\", "/")
        except ValueError:
            relative_lib = "lib"
        rpath = "$ORIGIN" if relative_lib == "." else f"$ORIGIN/{relative_lib}"
        result = subprocess.run(["patchelf", "--set-rpath", rpath, str(elf_file)])
        if result.returncode != 0:
            warn(f"failed to set rpath for {elf_file}")


def main() -> int:
    """执行 release 打包主流程。"""

    args = parse_args()
    build_dir = resolve_project_path(args.build_dir)
    install_dir = resolve_project_path(args.install_dir) if args.install_dir else default_install_dir()
    dependency_file = resolve_project_path(args.dependencies)

    if not build_dir.is_dir():
        raise RuntimeError(f"build dir does not exist: {build_dir}")

    source_exe = find_executable(build_dir)
    clear_install_directory(install_dir, not args.no_clean, args.force_clean)

    packaged_exe = install_dir / source_exe.name
    copy_file(source_exe, packaged_exe)
    copy_project_runtime(build_dir, install_dir, args.include_pdb, args.include_qml_module_dir, "release")
    copy_settings_config(install_dir)
    copy_model_configs(install_dir)
    copy_easytrain_python_runtime(install_dir)

    # 外部运行库先按 YAML 复制，再由平台相关逻辑补 Qt、MSVC 或 ELF 依赖。
    if args.skip_dependencies:
        print("skip dependencies")
    elif dependency_file.is_file():
        copy_yaml_dependencies(build_dir, install_dir, dependency_file, "release")
    else:
        warn(f"skip dependencies, missing {dependency_file}")

    if platform_key() == "windows":
        arch = target_architecture(build_dir)
        if args.skip_system_libs:
            print("skip system libraries")
        else:
            copy_msvc_runtime(install_dir, arch)
            copy_windows_sdk_dependencies(install_dir, arch)
        invoke_windeployqt(build_dir, install_dir, packaged_exe, args.windeployqt, args.skip_windeployqt)
        cleanup_release_debug_artifacts(install_dir)
        write_windows_marker(install_dir, build_dir, arch, args.skip_dependencies, args.include_qml_module_dir)
        print(f"\npackage complete: {install_dir}")
        print(f"double-click to run: {packaged_exe}")
    else:
        if not args.skip_windeployqt:
            copy_unix_qt_runtime(build_dir, install_dir, args.qt_root)
        copy_elf_dependencies(install_dir, not args.skip_system_libs)
        patch_rpath_if_possible(install_dir)
        write_unix_launcher(install_dir)
        write_qt_conf(install_dir)
        (install_dir / MARKER_FILE).write_text(
            f"generated_by=tools/package_app.py\nbuild_dir={build_dir}\nconfig=release\n",
            encoding="utf-8",
        )
        print(f"\npackage complete: {install_dir}")
        print(f"run on Linux/macOS: {install_dir / f'run_{PROJECT_NAME}.sh'}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
