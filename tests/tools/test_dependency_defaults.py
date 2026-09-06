from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

import pytest

from tools.dependency_utils import build_dll_variant_sets, dll_matches_config, expand_dependency_pattern, load_dependencies, resolve_dependency_root
from tools.package_app import verify_package


ROOT = Path(__file__).resolve().parents[2]


def test_dependency_configs_have_no_hardcoded_paths() -> None:
    """Verify Configxxx.cmake files contain no hardcoded local filesystem paths."""
    modules = [
        "ConfigQT.cmake",
        "ConfigSQLite.cmake",
        "ConfigOpenCV.cmake",
        "ConfigInferRT.cmake",
        "ConfigCUDA.cmake",
    ]

    for module_name in modules:
        content = (ROOT / "cmake" / module_name).read_text(encoding="utf-8")
        assert not re.search(r"[A-Za-z]:/(?:Software|Project|Users|Program Files)", content, re.IGNORECASE), (
            f"{module_name} must not contain hardcoded local paths"
        )


def test_dependencies_yaml_contains_expected_defaults() -> None:
    """Verify dependencies.yaml contains the expected dependencies with valid defaults."""
    deps = {
        str(dep["name"]): dep
        for dep in load_dependencies(ROOT / "tools" / "dependencies.yaml")
    }

    expected = ["qt", "sqlite", "opencv", "inferrt", "cuda", "tensorrt", "faiss", "mkl"]
    for name in expected:
        assert name in deps, f"Missing {name} in tools/dependencies.yaml"
        assert deps[name].get("default"), f"Missing default path for {name} in dependencies.yaml"


def test_cmake_default_reader_returns_manifest_values(tmp_path: Path) -> None:
    """Test dlt_dependency_default reads values from tools/dependencies.yaml."""
    if shutil.which("cmake") is None:
        pytest.skip("CMake is required")

    source = tmp_path / "source"
    build = tmp_path / "build"
    source.mkdir()

    defaults_module = (ROOT / "cmake" / "ConfigDependencyDefaults.cmake").as_posix()
    manifest_file = (ROOT / "tools" / "dependencies.yaml").as_posix()

    (source / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(DefaultsProbe NONE)\n"
        f'set(DLT_DEPENDENCY_MANIFEST "{manifest_file}")\n'
        f'include("{defaults_module}")\n'
        "dlt_dependency_default(sqlite _sqlite)\n"
        "dlt_dependency_default(opencv _opencv)\n"
        "dlt_dependency_default(qt _qt)\n"
        "dlt_dependency_default(inferrt _inferrt)\n"
        'file(WRITE "${CMAKE_BINARY_DIR}/defaults.txt" "${_sqlite}\\n${_opencv}\\n${_qt}\\n${_inferrt}\\n")\n',
        encoding="utf-8",
    )

    subprocess.run(
        ["cmake", "-S", str(source), "-B", str(build)],
        check=True,
        capture_output=True,
        text=True,
    )

    lines = (build / "defaults.txt").read_text(encoding="utf-8").splitlines()
    assert len(lines) == 4
    deps = {
        str(dep["name"]): str(dep.get("default", ""))
        for dep in load_dependencies(ROOT / "tools" / "dependencies.yaml")
    }
    assert lines[0] == deps["sqlite"]
    assert lines[1] == deps["opencv"]
    assert lines[2] == deps["qt"]
    assert lines[3] == deps["inferrt"]


def test_cmake_precedence_rules(tmp_path: Path, monkeypatch) -> None:
    """Test precedence: -D > ENV > dependencies.yaml > CMakeCache."""
    if shutil.which("cmake") is None:
        pytest.skip("CMake is required")

    defaults_module = (ROOT / "cmake" / "ConfigDependencyDefaults.cmake").as_posix()

    mock_default = tmp_path / "from_yaml"
    mock_env = tmp_path / "from_env"
    mock_cmd = tmp_path / "from_cmd"
    mock_default.mkdir()
    mock_env.mkdir()
    mock_cmd.mkdir()

    mock_manifest = tmp_path / "manifest.yaml"
    mock_manifest.write_text(
        f"dependencies:\n  - name: test_pkg\n    default: {mock_default.as_posix()}\n",
        encoding="utf-8",
    )

    source = tmp_path / "source"
    build = tmp_path / "build"
    source.mkdir()

    (source / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(PrecedenceProbe NONE)\n"
        f'set(DLT_DEPENDENCY_MANIFEST "{mock_manifest.as_posix()}")\n'
        f'include("{defaults_module}")\n'
        "dlt_dependency_resolve_path(_val _origin test_pkg VARIABLES TEST_ROOT ENVIRONMENT_VARIABLES TEST_ROOT)\n"
        'file(WRITE "${CMAKE_BINARY_DIR}/res.txt" "${_val}\\n${_origin}\\n")\n',
        encoding="utf-8",
    )

    # 1. Default (no -D, no ENV)
    monkeypatch.delenv("TEST_ROOT", raising=False)
    subprocess.run(["cmake", "-S", str(source), "-B", str(build)], check=True, capture_output=True, text=True)
    val, origin = (build / "res.txt").read_text(encoding="utf-8").splitlines()
    assert Path(val).resolve() == mock_default.resolve()
    assert origin == "project-default"

    # 2. ENV overrides default
    monkeypatch.setenv("TEST_ROOT", str(mock_env))
    subprocess.run(["cmake", "-S", str(source), "-B", str(build)], check=True, capture_output=True, text=True)
    val, origin = (build / "res.txt").read_text(encoding="utf-8").splitlines()
    assert Path(val).resolve() == mock_env.resolve()
    assert origin == "environment"

    # 3. Command-line -D overrides ENV and default
    subprocess.run(
        ["cmake", "-S", str(source), "-B", str(build), f"-DTEST_ROOT={mock_cmd.as_posix()}"],
        check=True,
        capture_output=True,
        text=True,
    )
    val, origin = (build / "res.txt").read_text(encoding="utf-8").splitlines()
    assert Path(val).resolve() == mock_cmd.resolve()
    assert origin == "user"


def test_dependency_utils_resolve_dependency_root(tmp_path: Path, monkeypatch) -> None:
    """Test resolve_dependency_root in dependency_utils.py."""
    default_root = tmp_path / "default_root"
    env_root = tmp_path / "env_root"
    default_root.mkdir()
    env_root.mkdir()

    build_dir = tmp_path / "build"
    build_dir.mkdir()

    dep = {
        "name": "sample",
        "root": "SAMPLE_ROOT",
        "default": default_root.as_posix(),
        "cmake": "cmake/ConfigSample.cmake",
    }

    # 1. Resolves default
    monkeypatch.delenv("SAMPLE_ROOT", raising=False)
    res = resolve_dependency_root(dep, build_dir, repo_root=tmp_path)
    assert res == default_root.resolve()

    # 2. Resolves from env
    monkeypatch.setenv("SAMPLE_ROOT", str(env_root))
    res = resolve_dependency_root(dep, build_dir, repo_root=tmp_path)
    assert res == env_root.resolve()


def test_dependency_pattern_expands_build_configuration_directory(tmp_path: Path) -> None:
    """Expand manifest build_config placeholders to the platform directory name."""
    release_dir = tmp_path / "runtime" / "Release"
    release_dir.mkdir(parents=True)
    expected = release_dir / "runtime.dll"
    expected.write_bytes(b"runtime")

    matches = expand_dependency_pattern(
        tmp_path,
        "runtime/{build_config}/*.dll",
        "release",
    )

    assert matches == [expected.resolve()]


def test_verify_package_checks_required_windows_runtime(tmp_path: Path) -> None:
    """Verify a Windows package contains the executable, modules, configs, and Qt runtime."""
    build_dir = tmp_path / "build"
    module_dir = build_dir / "dltool" / "core"
    module_dir.mkdir(parents=True)
    (module_dir / "dltool_core.dll").write_bytes(b"module")

    package_dir = tmp_path / "package"
    (package_dir / "config" / "settings").mkdir(parents=True)
    (package_dir / "config" / "models").mkdir(parents=True)
    (package_dir / "python").mkdir(parents=True)
    (package_dir / "dltool.exe").write_bytes(b"executable")
    (package_dir / "dltool_core.dll").write_bytes(b"module")
    (package_dir / ".dltool_package").write_text("version=0.0.1\n", encoding="utf-8")
    for name in ("Qt6Core.dll", "Qt6Gui.dll", "Qt6Qml.dll", "Qt6Quick.dll"):
        (package_dir / name).write_bytes(b"qt")

    verify_package(package_dir, build_dir, require_qt_runtime=True)


def test_release_filter_excludes_debug_suffix_variants() -> None:
    """Exclude paired _debug.dll files from a release dependency set."""
    paths = [Path("tbb12.dll"), Path("tbb12_debug.dll")]
    debug_names, release_names = build_dll_variant_sets(paths)

    assert "tbb12_debug.dll" in debug_names
    assert "tbb12.dll" in release_names
    assert not dll_matches_config(paths[1], "release", debug_names, release_names)
