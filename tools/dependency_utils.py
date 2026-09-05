"""打包和依赖链接脚本共享的工具函数。"""

from __future__ import annotations

import glob
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any, Iterable

import yaml

PROJECT_NAME = "dltool"
PROJECT_DLL_PREFIX = f"{PROJECT_NAME}_"
MARKER_FILE = ".dltool_package"
CONFIG_DIR_NAMES = {"debug", "release", "relwithdebinfo", "minsizerel"}
REPO_ROOT = Path(__file__).resolve().parents[1]


def warn(message: str) -> None:
    """向 stderr 输出警告信息。"""

    print(f"warning: {message}", file=sys.stderr)


def platform_key() -> str:
    """返回 dependencies.yaml 使用的平台键名。"""

    if os.name == "nt":
        return "windows"
    if sys.platform == "darwin":
        return "macos"
    if sys.platform.startswith("linux"):
        return "linux"
    return sys.platform


def resolve_project_path(path: str | Path, repo_root: Path = REPO_ROOT) -> Path:
    """把用户输入路径解析为绝对路径。

    相对路径按仓库根目录解析，不要求路径已经存在。
    """

    raw = Path(os.path.expanduser(str(path)))
    if raw.is_absolute():
        return raw.resolve(strict=False)
    return (repo_root / raw).resolve(strict=False)


def normalize_list(value: Any) -> list[str]:
    """把 YAML 中的标量或列表统一转换为字符串列表。"""

    if value is None:
        return []
    if isinstance(value, list):
        return [str(item) for item in value]
    return [str(value)]


def load_yaml_file(path: Path) -> dict[str, Any]:
    """读取 YAML 文件并校验顶层对象是字典。"""

    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError(f"invalid YAML root in {path}")
    return data


def load_dependencies(path: Path) -> list[dict[str, Any]]:
    """读取 dependencies.yaml 中的依赖条目列表。"""

    # dependencies.yaml 的顶层固定为 dependencies: [...]，每个元素描述一个运行库来源。
    data = load_yaml_file(path)
    deps = data.get("dependencies", [])
    if not isinstance(deps, list):
        raise ValueError(f"dependencies must be a list in {path}")
    return [dep for dep in deps if isinstance(dep, dict)]


def dependency_matches_config(dep: dict[str, Any], config: str) -> bool:
    """判断依赖条目是否适用于当前构建配置。"""

    dep_config = str(dep.get("config", "all")).lower()
    return dep_config in {"", "all", "any"} or dep_config == config.lower()


def dependency_patterns(dep: dict[str, Any], platform: str) -> list[str]:
    """返回当前平台需要处理的文件模式列表。"""

    return normalize_list(dep.get("all")) + normalize_list(dep.get(platform))


def dependency_destinations(dep: dict[str, Any]) -> list[str]:
    """返回依赖链接目标目录列表。"""

    return normalize_list(dep.get("destinations", dep.get("dest")))


def is_direct_root(value: str) -> bool:
    """判断 root 字段是否已经是路径而不是 CMake 变量名。"""

    return (
        ":" in value
        or "/" in value
        or "\\" in value
        or value.startswith(".")
        or value.startswith("~")
    )


def read_cmake_cache_value(cache_file: Path, key: str) -> str | None:
    """从 CMakeCache.txt 读取指定变量值。"""

    if not cache_file.exists():
        return None
    pattern = re.compile(rf"^{re.escape(key)}(?::[^=]+)?=(.*)$", re.IGNORECASE)
    for line in cache_file.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = pattern.match(line.strip())
        if match:
            value = match.group(1).strip()
            return None if value.endswith("-NOTFOUND") else value
    return None


def read_cmake_set(cmake_file: Path, name: str) -> str | None:
    """从简单的 CMake set(...) 语句中读取变量值。"""

    if not cmake_file.exists():
        return None
    pattern = re.compile(rf"^\s*set\s*\(\s*{re.escape(name)}\s+(.+?)\s*\)", re.IGNORECASE)
    for line in cmake_file.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = pattern.match(line)
        if not match:
            continue
        rest = match.group(1).strip()
        if rest.startswith('"'):
            end = rest.find('"', 1)
            if end >= 0:
                return rest[1:end]
        return rest.split()[0].strip('"')
    return None


def read_cmake_set_expanded(cmake_file: Path, name: str) -> str | None:
    """读取 CMake 变量，并展开有限层级的 ${VAR} 引用。"""

    value = read_cmake_set(cmake_file, name)
    if not value:
        return None
    for _ in range(8):
        match = re.search(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}", value)
        if not match:
            break
        ref_value = read_cmake_set(cmake_file, match.group(1))
        if not ref_value:
            break
        value = value.replace(match.group(0), ref_value)
    return value


def _candidate_env_names(dep: dict[str, Any], root_spec: str) -> list[str]:
    """构造依赖 root 可尝试读取的环境变量名列表。"""

    names = [root_spec]
    names.extend(normalize_list(dep.get("env")))
    unique: list[str] = []
    seen: set[str] = set()
    for name in names:
        key = name.upper()
        if key not in seen:
            unique.append(name)
            seen.add(key)
    return unique


def resolve_dependency_root(
    dep: dict[str, Any],
    build_dir: Path,
    repo_root: Path = REPO_ROOT,
    platform: str | None = None,
) -> Path | None:
    """解析依赖条目的 root 目录。

    解析顺序为：直接路径、环境变量、平台特定 root 缓存、YAML default 字段、
    CMakeCache.txt、YAML 指定的 CMake 配置文件。
    """

    if platform is None:
        platform = platform_key()

    platform_root = dep.get(f"{platform}_root") if platform else None
    root_spec = str(platform_root if platform_root not in (None, "") else dep.get("root", "")).strip()
    if not root_spec:
        return None

    if is_direct_root(root_spec) or not dep.get("cmake"):
        return resolve_project_path(root_spec, repo_root)

    for env_name in _candidate_env_names(dep, root_spec):
        value = os.environ.get(env_name)
        if value:
            return resolve_project_path(value, repo_root)

    if platform_root not in (None, ""):
        cache_value = read_cmake_cache_value(build_dir / "CMakeCache.txt", root_spec)
        if cache_value:
            cache_root = resolve_project_path(cache_value, repo_root)
            if cache_root.exists():
                return cache_root

    default_value = dep.get("default")
    if default_value:
        candidate = resolve_project_path(str(default_value), repo_root)
        if candidate.exists():
            return candidate

    cache_value = read_cmake_cache_value(build_dir / "CMakeCache.txt", root_spec)
    if cache_value:
        return resolve_project_path(cache_value, repo_root)

    if default_value:
        return resolve_project_path(str(default_value), repo_root)

    cmake_file = resolve_project_path(str(dep["cmake"]), repo_root)
    cmake_value = read_cmake_set_expanded(cmake_file, root_spec)
    if cmake_value:
        return resolve_project_path(cmake_value, repo_root)

    return None


def expand_dependency_pattern(root: Path, pattern: str) -> list[Path]:
    """在依赖根目录下展开文件匹配模式。"""

    full_pattern = str(root / pattern)
    matches = [Path(match).resolve(strict=False) for match in glob.glob(full_pattern)]
    if matches:
        return [path for path in matches if path.is_file()]
    if "*" not in pattern and "?" not in pattern:
        candidate = (root / pattern).resolve(strict=False)
        if candidate.is_file():
            return [candidate]
    return []


def copy_file(source: Path, destination: Path) -> None:
    """复制单个文件并保留元数据。"""

    source = source.resolve(strict=True)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    print(f"copy {source} -> {destination}")


def remove_existing_file(path: Path) -> None:
    """删除已有文件或文件链接，拒绝删除真实目录。"""

    if path.exists() or path.is_symlink():
        if path.is_dir() and not path.is_symlink():
            raise RuntimeError(f"existing path is a directory, refusing to overwrite as file link: {path}")
        path.unlink()


def remove_existing_dir_link(path: Path) -> None:
    """删除已有目录链接，拒绝删除真实目录。"""

    if not path.exists() and not path.is_symlink():
        return
    is_junction = bool(getattr(path, "is_junction", lambda: False)())
    if not is_junction and os.name == "nt":
        try:
            st = os.stat(path, follow_symlinks=False)
            is_junction = bool(getattr(st, "st_file_attributes", 0) & 0x400)
        except OSError:
            pass
    if is_junction:
        path.rmdir()
        return
    if path.is_symlink():
        path.unlink()
        return
    raise RuntimeError(f"existing path is not a link, refusing to overwrite: {path}")


def link_file(source: Path, link: Path) -> None:
    """创建文件链接，符号链接失败时回退到硬链接。"""

    source = source.resolve(strict=True)
    link.parent.mkdir(parents=True, exist_ok=True)
    # 只覆盖已有文件或链接；如果目标是目录会拒绝，避免误破坏构建目录结构。
    remove_existing_file(link)
    try:
        os.symlink(source, link)
        print(f"create symlink {link} -> {source}")
    except OSError:
        try:
            os.link(source, link)
            print(f"create hardlink {link} -> {source}")
        except OSError:
            shutil.copy2(source, link)
            print(f"copy file {link} -> {source}")


def link_dir(source: Path, link: Path) -> None:
    """创建目录链接，Windows 下符号链接失败时回退到 junction。"""

    source = source.resolve(strict=True)
    link.parent.mkdir(parents=True, exist_ok=True)
    # 目录链接只允许覆盖符号链接或 Windows junction，不覆盖真实目录。
    remove_existing_dir_link(link)
    try:
        os.symlink(source, link, target_is_directory=True)
        print(f"create symlink {link} -> {source}")
    except OSError:
        if os.name != "nt":
            raise
        result = subprocess.run(
            ["cmd", "/c", "mklink", "/J", str(link), str(source)],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        if result.returncode != 0:
            raise RuntimeError(result.stdout.strip() or f"failed to create junction {link} -> {source}")
        print(f"create junction {link} -> {source}")


def build_dll_variant_sets(paths: Iterable[Path]) -> tuple[set[str], set[str]]:
    """根据同目录候选文件识别成对的 debug/release DLL 名称。"""

    # 只有 release/debug 成对存在时，才把 xxxd.dll 判断为 debug 变体。
    # 这样不会误伤 dltool_grid.dll 这类本身以 d 结尾的 release DLL。
    names = {path.name.lower() for path in paths if path.suffix.lower() == ".dll"}
    debug_names: set[str] = set()
    release_names: set[str] = set()
    for name in names:
        if not name.endswith("d.dll"):
            continue
        release_name = f"{name[:-5]}.dll"
        if release_name in names:
            debug_names.add(name)
            release_names.add(release_name)
    return debug_names, release_names


def dll_matches_config(path: Path, config: str, debug_names: set[str], release_names: set[str]) -> bool:
    """判断 DLL 是否匹配目标配置。"""

    name = path.name.lower()
    if not name.endswith(".dll"):
        return True
    if config.lower() == "debug":
        return name not in release_names
    if name in debug_names:
        return False
    return True


def is_project_dll(path: Path) -> bool:
    """判断文件是否是 DeepLearningTool 项目模块 DLL。"""

    name = path.name.lower()
    return name.startswith(PROJECT_DLL_PREFIX) and name.endswith(".dll")


def immediate_project_dlls(module_root: Path) -> list[Path]:
    """列出 build/dltool 下一级模块目录中的项目 DLL。"""

    if not module_root.is_dir():
        return []
    return sorted(path for path in module_root.glob(f"*/{PROJECT_DLL_PREFIX}*.dll") if path.is_file())


def should_skip_config_dir(path: Path) -> bool:
    """判断相对路径是否位于 CMake 配置名目录中。"""

    return any(part.lower() in CONFIG_DIR_NAMES for part in path.parts)
