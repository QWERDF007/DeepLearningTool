"""DeepLearningTool 运行时依赖链接脚本。

开发阶段用它把 build/dltool 中的项目 DLL 和 dependencies.yaml 中声明的第三方
运行库链接到 build/bin，方便直接从构建目录启动程序或运行测试。默认链接 release 版本。
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

if str(Path(__file__).resolve().parent) not in sys.path:
    sys.path.insert(0, str(Path(__file__).resolve().parent))

from dependency_utils import (
    REPO_ROOT,
    build_dll_variant_sets,
    dependency_destinations,
    dependency_matches_config,
    dependency_patterns,
    dll_matches_config,
    expand_dependency_pattern,
    immediate_project_dlls,
    is_project_dll,
    link_dir,
    link_file,
    load_dependencies,
    platform_key,
    resolve_dependency_root,
    resolve_project_path,
    warn,
)


def parse_args() -> argparse.Namespace:
    """解析命令行参数。"""

    parser = argparse.ArgumentParser(description="Link DeepLearningTool runtime dependencies.")
    parser.add_argument("--build-dir", "-BuildDir", default="build")
    parser.add_argument("--config", "-Config", choices=["release", "debug"], default="release")
    parser.add_argument("--dependencies", default="tools/dependencies.yaml")
    parser.add_argument("--skip-external", action="store_true")
    return parser.parse_args()


def link_project_outputs(build_dir: Path, config: str) -> None:
    """把项目模块输出链接到 build/bin。

    Args:
        build_dir: CMake 构建目录。
        config: 目标配置，支持 release 或 debug。
    """

    module_root = build_dir / "dltool"
    bin_dir = build_dir / "bin"
    if not module_root.is_dir():
        warn(f"skip dltool links, missing {module_root}")
        return

    # On Windows the executable is dltool.exe, so this directory link is safe.
    # On Unix the executable itself is build/bin/dltool, which conflicts with
    # the historical module-link destination.  Unix builds already keep the
    # modules under build/dltool and use the CMake runtime paths to find them.
    if os.name != "nt":
        print("skip dltool module directory link on non-Windows (build/bin/dltool is the executable)")
        return

    link_dir(module_root, bin_dir / "dltool")

    # 项目 DLL 从模块输出目录链接到 build/bin；release 模式会过滤成对的 *d.dll。
    candidates = immediate_project_dlls(module_root)
    debug_names, release_names = build_dll_variant_sets(candidates)
    for dll in candidates:
        if not dll_matches_config(dll, config, debug_names, release_names):
            continue
        link_file(dll, bin_dir / dll.name)


def link_external_dependencies(build_dir: Path, dependency_file: Path, config: str) -> None:
    """按 dependencies.yaml 把第三方运行库链接到目标目录。"""

    platform = platform_key()
    dependencies = load_dependencies(dependency_file)
    for dep in dependencies:
        # YAML 中 config: debug 的条目不会进入默认 release 链接。
        if not dependency_matches_config(dep, config):
            continue

        patterns = dependency_patterns(dep, platform)
        if not patterns:
            continue

        destinations = [resolve_project_path(dest) for dest in dependency_destinations(dep)]
        if not destinations:
            warn(f"skip dependency {dep.get('name', '<unnamed>')}, no destinations configured")
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
            # 外部依赖不接受 dltool_*，避免覆盖项目模块自己的输出。
            if platform == "windows" and is_project_dll(runtime):
                continue
            if platform == "windows" and not dll_matches_config(runtime, config, debug_names, release_names):
                continue
            for destination in destinations:
                link_file(runtime, destination / runtime.name)


def link_tests(build_dir: Path) -> None:
    """给测试输出目录补充 dltool 模块目录链接。"""

    tests_dir = build_dir / "tests"
    if tests_dir.is_dir():
        link_dir(build_dir / "dltool", tests_dir / "dltool")
    else:
        warn(f"skip test link, missing {tests_dir}")


def link_settings_config(build_dir: Path) -> None:
    source = REPO_ROOT / "config"
    if not source.is_dir():
        warn(f"skip settings config link, missing {source}")
        return
    link_dir(source, build_dir / "bin" / "config")


def link_easytrain_python_runtime(build_dir: Path) -> None:
    source = REPO_ROOT / "3rdparty" / "EasyTrain" / "src" / "python"
    if not source.is_dir():
        warn(f"skip EasyTrain python runtime link, missing {source}")
        return
    link_dir(source, build_dir / "bin" / "python")


def main() -> int:
    """执行依赖链接主流程。"""

    args = parse_args()
    build_dir = resolve_project_path(args.build_dir)
    dependency_file = resolve_project_path(args.dependencies)

    link_project_outputs(build_dir, args.config)
    print("link dltool dll success")

    link_settings_config(build_dir)
    print("link settings config success")

    link_easytrain_python_runtime(build_dir)
    print("link EasyTrain python runtime success")

    if args.skip_external:
        print("skip external dependencies")
    elif dependency_file.is_file():
        link_external_dependencies(build_dir, dependency_file, args.config)
        print("link external dependencies success")
    else:
        warn(f"skip external dependency links, missing {dependency_file}")

    link_tests(build_dir)
    print("link test success")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
