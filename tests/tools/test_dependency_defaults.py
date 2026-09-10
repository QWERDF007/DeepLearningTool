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

from tools.dependency_utils import (
    build_dll_variant_sets,
    dll_matches_config,
    expand_dependency_pattern,
    load_dependencies,
    platform_key,
    read_cmake_cache_value,
    resolve_dependency_root,
)
from tools.package_app import copy_file, copy_yaml_dependencies, project_version, verify_package


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
        str(dep["name"]): str(dep.get(f"{platform_key()}_default", dep.get("default", "")))
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
    executable_name = "dltool.exe" if os.name == "nt" else "dltool"
    (package_dir / executable_name).write_bytes(b"executable")
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


def test_dependency_utils_resolve_platform_specific_default(tmp_path: Path) -> None:
    """Platform-specific defaults override the generic fallback."""
    linux_root = tmp_path / "linux_root"
    windows_root = tmp_path / "windows_root"
    linux_root.mkdir()
    windows_root.mkdir()
    build_dir = tmp_path / "build"
    build_dir.mkdir()

    dep = {
        "name": "sample",
        "root": "SAMPLE_ROOT",
        "default": windows_root.as_posix(),
        "linux_default": linux_root.as_posix(),
        "cmake": "cmake/ConfigSample.cmake",
    }

    assert resolve_dependency_root(dep, build_dir, repo_root=tmp_path, platform="linux") == linux_root.resolve()
    assert resolve_dependency_root(dep, build_dir, repo_root=tmp_path, platform="windows") == windows_root.resolve()


def test_cmake_minimum_required_supports_environment_modification() -> None:
    """Verify root CMakeLists.txt requires CMake >= 3.22 for ENVIRONMENT_MODIFICATION support."""
    content = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"cmake_minimum_required\s*\(\s*VERSION\s+([0-9.]+)\s*\)", content)
    assert match is not None, "Missing cmake_minimum_required in root CMakeLists.txt"
    version_str = match.group(1)
    version_tuple = tuple(int(x) for x in version_str.split("."))
    assert version_tuple >= (3, 22), (
        f"cmake_minimum_required version {version_str} is less than 3.22, "
        "which is required for test ENVIRONMENT_MODIFICATION properties."
    )


def test_sanitizer_options_drive_compiler_and_linker_flags(tmp_path: Path) -> None:
    """Verify ENABLE_SANITIZER and DLT_ENABLE_SANITIZER options set compiler and linker flags."""
    if shutil.which("cmake") is None:
        pytest.skip("CMake is required")

    config_compiler = (ROOT / "cmake" / "ConfigCompiler.cmake").as_posix()

    for opt in ["-DENABLE_SANITIZER=ON", "-DDLT_ENABLE_SANITIZER=ON"]:
        opt_name = opt.replace("=", "_").replace("-", "").replace("D", "", 1)
        source = tmp_path / f"source_{opt_name}"
        build = tmp_path / f"build_{opt_name}"
        source.mkdir(parents=True)

        (source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.22)\n"
            "project(SanitizerProbe CXX C)\n"
            f'include("{config_compiler}")\n'
            'file(WRITE "${CMAKE_BINARY_DIR}/flags.txt" '
            '"CXX_FLAGS=${CMAKE_CXX_FLAGS}\\n"'
            '"EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}\\n"'
            '"COMPILER_ID=${CMAKE_CXX_COMPILER_ID}\\n")\n',
            encoding="utf-8",
        )

        subprocess.run(
            ["cmake", "-S", str(source), "-B", str(build), opt],
            check=True,
            capture_output=True,
            text=True,
        )

        flags_content = (build / "flags.txt").read_text(encoding="utf-8")
        flags_dict = dict(line.split("=", 1) for line in flags_content.splitlines() if "=" in line)
        compiler_id = flags_dict.get("COMPILER_ID", "")
        cxx_flags = flags_dict.get("CXX_FLAGS", "")
        linker_flags = flags_dict.get("EXE_LINKER_FLAGS", "")

        if "MSVC" in compiler_id:
            assert "/fsanitize=address" in cxx_flags, f"Expected /fsanitize=address in CXX_FLAGS for MSVC, got: {cxx_flags}"
        elif "Clang" in compiler_id or "GNU" in compiler_id:
            assert "-fsanitize=address" in cxx_flags, f"Expected -fsanitize=address in CXX_FLAGS, got: {cxx_flags}"
            assert "-fsanitize=address" in linker_flags, f"Expected -fsanitize=address in EXE_LINKER_FLAGS, got: {linker_flags}"


def test_test_cmake_and_fixtures_have_no_hardcoded_developer_paths() -> None:
    """Verify test fixtures, test CMakeLists, and test runners contain no hardcoded developer drive roots."""
    files_to_check = [
        ROOT / "tests" / "settings" / "CMakeLists.txt",
        ROOT / "tests" / "feature" / "CMakeLists.txt",
        ROOT / "tests" / "model" / "CMakeLists.txt",
        ROOT / "tests" / "model_qml" / "CMakeLists.txt",
        ROOT / "tests" / "model_support" / "TestFixture.cpp",
        ROOT / "tests" / "project" / "PersistentProjectFixture.cpp",
        ROOT / "tests" / "model_qml" / "main.cpp",
        ROOT / "tests" / "feature" / "test_FeatureLifecycle.cpp",
        ROOT / "tools" / "run_project_tests.py",
        ROOT / "tools" / "run_model_tests.py",
    ]

    forbidden_patterns = [
        r"F:/tmp\b",
        r"F:/Github\b",
        r"D:/Software/dev\b",
        r"F:/Projects/DeepLearningTool\b",
    ]

    violations = []
    for file_path in files_to_check:
        assert file_path.exists(), f"File {file_path} does not exist"
        content = file_path.read_text(encoding="utf-8")
        for line_no, line in enumerate(content.splitlines(), start=1):
            for pattern in forbidden_patterns:
                if re.search(pattern, line, re.IGNORECASE):
                    violations.append(f"{file_path.relative_to(ROOT)}:{line_no}: {line.strip()}")

    assert not violations, "Found hardcoded developer drive roots in test files:\n" + "\n".join(violations)


def test_plugin_library_install_rules_and_headers_have_no_absolute_path_leak() -> None:
    """Verify AddPluginLibrary installs headers without invalid source paths and without absolute path leaks."""
    content = (ROOT / "cmake" / "AddPluginLibrary.cmake").read_text(encoding="utf-8")
    assert "${CMAKE_CURRENT_SOURCE_DIR}/include/${PROJECT_NAME}/${PLUGIN_NAME}" not in content, (
        "AddPluginLibrary.cmake must not attempt to install non-existent include/${PROJECT_NAME}/${PLUGIN_NAME}"
    )
    assert "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>" in content, (
        "PLUGIN_HEADER must protect build include paths using $<BUILD_INTERFACE:...>"
    )
    assert "$<INSTALL_INTERFACE:" in content, (
        "PLUGIN_HEADER must provide $<INSTALL_INTERFACE:...>"
    )

    tool_cmake = (ROOT / "src" / "tool" / "CMakeLists.txt").read_text(encoding="utf-8")
    assert "install(TARGETS" in tool_cmake or "install(\n    TARGETS" in tool_cmake, (
        "src/tool/CMakeLists.txt must install the dltool executable"
    )

    root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    assert "install(DIRECTORY" in root_cmake, (
        "Root CMakeLists.txt must install config and runtime directories"
    )


def test_cmake_install_and_independent_consumer(tmp_path: Path) -> None:
    """Compile, link and run a public API consumer against the CMake installation."""
    build_dir = ROOT / "build"
    if not (build_dir / "CMakeCache.txt").is_file():
        pytest.skip("CMake build directory is required")
    cmake = shutil.which("cmake")
    if cmake is None:
        pytest.skip("CMake is required")

    install_prefix = tmp_path / "installed"
    subprocess.run(
        [cmake, "--install", str(build_dir), "--prefix", str(install_prefix), "--config", "Release"],
        check=True,
        capture_output=True,
        text=True,
    )

    exe_name = "dltool.exe" if os.name == "nt" else "dltool"
    assert (install_prefix / "bin" / exe_name).is_file()
    assert (install_prefix / "include" / "core" / "CoreDef.h").is_file()
    assert (install_prefix / "include" / "dltool" / "core" / "Export.h").is_file()
    assert (install_prefix / "bin" / "config" / "settings" / "SoftwareSetting.yaml").is_file()
    assert (install_prefix / "bin" / "python").is_dir()

    # Consume an exported function, not just header-only declarations.
    consumer_src = tmp_path / "consumer_src"
    consumer_build = tmp_path / "consumer_build"
    consumer_src.mkdir()

    (consumer_src / "main.cpp").write_text(
        "#include <common/GeometryKernel.h>\n"
        "#include <vector>\n"
        "int main() {\n"
        "    const auto polygon = dltool::common::geometry::rectangleToPolygon({9, 8}, {1, 2});\n"
        "    const std::vector<QPointF> expected{{1, 2}, {9, 2}, {9, 8}, {1, 8}};\n"
        "    return polygon == expected ? 0 : 1;\n"
        "}\n",
        encoding="utf-8",
    )

    qt6_core_dir = read_cmake_cache_value(build_dir / "CMakeCache.txt", "Qt6Core_DIR")
    qt6_opt = []
    if qt6_core_dir:
        prefix = Path(qt6_core_dir).parents[2]
        qt6_opt = [f"-DCMAKE_PREFIX_PATH={prefix.as_posix()}"]
    else:
        qt6_dir = read_cmake_cache_value(build_dir / "CMakeCache.txt", "Qt6_DIR")
        if qt6_dir:
            qt6_opt = [f"-DQt6_DIR={qt6_dir}"]

    inc_dir = (install_prefix / "include").as_posix()
    (consumer_src / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.22)\n"
        "project(InstalledConsumer CXX)\n"
        "set(CMAKE_CXX_STANDARD 20)\n"
        "find_package(Qt6 REQUIRED COMPONENTS Core)\n"
        f'include_directories("{inc_dir}")\n'
        "add_executable(consumer main.cpp)\n"
        f'find_library(COMMON_LIBRARY NAMES dltool_common PATHS "{install_prefix.as_posix()}/lib" NO_DEFAULT_PATH REQUIRED)\n'
        "target_link_libraries(consumer PRIVATE Qt6::Core ${COMMON_LIBRARY})\n"
        "enable_testing()\n"
        "add_test(NAME installed_public_api COMMAND consumer)\n",
        encoding="utf-8",
    )

    subprocess.run(
        [cmake, "-S", str(consumer_src), "-B", str(consumer_build), *qt6_opt],
        check=True,
        capture_output=True,
        text=True,
    )
    subprocess.run(
        [cmake, "--build", str(consumer_build), "--config", "Release"],
        check=True,
        capture_output=True,
        text=True,
    )
    consumer_exe = consumer_build / ("Release/consumer.exe" if os.name == "nt" else "consumer")
    if not consumer_exe.is_file():
        consumer_exe = consumer_build / ("consumer.exe" if os.name == "nt" else "consumer")
    assert consumer_exe.is_file()

    runtime_dir = install_prefix / "bin"
    env = os.environ.copy()
    if os.name == "nt":
        copy_yaml_dependencies(build_dir, runtime_dir, ROOT / "tools/dependencies.yaml", "release", allow_missing=True)
        qt_bin = Path(qt6_core_dir).parents[2] / "bin"
        for name in ("Qt6Core.dll", "Qt6Cored.dll"):
            candidate = qt_bin / name
            if candidate.is_file():
                copy_file(candidate, runtime_dir / candidate.name)
        system_root = os.environ["SystemRoot"]
        env = {
            "SystemRoot": system_root,
            "PATH": os.pathsep.join((str(runtime_dir), str(Path(system_root) / "System32"), system_root)),
        }
    else:
        env["LD_LIBRARY_PATH"] = str(install_prefix / "lib")
    ctest = Path(cmake).with_name("ctest.exe" if os.name == "nt" else "ctest")
    result = subprocess.run(
        [str(ctest), "--test-dir", str(consumer_build), "-C", "Release", "--output-on-failure"],
        cwd=runtime_dir,
        env=env,
        capture_output=True,
        text=True,
        timeout=60,
    )
    assert result.returncode == 0, result.stdout + result.stderr


def test_runtime_package_verification_fails_on_missing_dependencies(tmp_path: Path) -> None:
    """Verify runtime package verification clearly fails when dependencies or marker are missing."""
    build_dir = ROOT / "build"
    fake_install = tmp_path / "fake_install"
    fake_install.mkdir()

    # Empty package must fail verification
    with pytest.raises(RuntimeError) as exc_info:
        verify_package(fake_install, build_dir, require_qt_runtime=True, expected_version="0.0.2")
    assert "missing files" in str(exc_info.value)

    # Incomplete package with only executable must still fail on missing project and Qt DLLs
    exe_name = "dltool.exe" if os.name == "nt" else "dltool"
    (fake_install / exe_name).write_bytes(b"dummy_exe")
    with pytest.raises(RuntimeError) as exc_info:
        verify_package(fake_install, build_dir, require_qt_runtime=True, expected_version="0.0.2")
    assert "missing files" in str(exc_info.value)


def test_desktop_smoke_test_with_smoke_test_flag() -> None:
    """Verify dltool starts and exits cleanly in offscreen mode with --smoke-test."""
    build_dir = ROOT / "build"
    exe_name = "dltool.exe" if os.name == "nt" else "dltool"
    app_exe = build_dir / "bin" / exe_name
    if not app_exe.is_file():
        pytest.skip(f"{app_exe} is not built yet")

    qt6_core_dir = read_cmake_cache_value(build_dir / "CMakeCache.txt", "Qt6Core_DIR")
    qt_bin = ""
    if qt6_core_dir:
        qt_bin = str(Path(qt6_core_dir).parents[2] / "bin")

    env = os.environ.copy()
    if qt_bin:
        env["PATH"] = f"{qt_bin};{app_exe.parent};" + env.get("PATH", "")
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["QT_QUICK_BACKEND"] = "software"
    env["QSG_RHI_BACKEND"] = "software"
    env["QML_DISABLE_DISK_CACHE"] = "1"

    proc = subprocess.run(
        [str(app_exe), "--smoke-test"],
        env=env,
        capture_output=True,
        text=True,
        timeout=15,
    )
    assert proc.returncode == 0, f"App smoke test failed with code {proc.returncode}: {proc.stderr}"


def test_isolated_installed_package_desktop_smoke_test() -> None:
    """Verify dltool packaged into an isolated directory matches current build and executes cleanly in a pure environment."""
    exe_name = "dltool.exe" if os.name == "nt" else "dltool"
    build_exe = ROOT / "build" / "bin" / exe_name
    assert build_exe.is_file(), f"Build binary must exist at {build_exe}; build project before running isolated package test"

    package_dir = ROOT / "install" / "test_isolated"
    app_exe = package_dir / exe_name

    import hashlib

    def compute_sha256(path: Path) -> str:
        hasher = hashlib.sha256()
        with open(path, "rb") as f:
            while chunk := f.read(65536):
                hasher.update(chunk)
        return hasher.hexdigest()

    build_bin_dir = ROOT / "build" / "bin"
    if os.name == "nt":
        project_binaries = list(build_bin_dir.glob("dltool_*.dll")) + list(build_bin_dir.glob("quickui.dll"))
    else:
        project_binaries = list(build_bin_dir.glob("libdltool_*.so")) + list(build_bin_dir.glob("libquickui.so"))

    def project_binary_mismatch() -> bool:
        if not app_exe.is_file() or compute_sha256(app_exe) != compute_sha256(build_exe):
            return True
        for b_file in project_binaries:
            target = (package_dir / "lib" / b_file.name) if not os.name == "nt" and (package_dir / "lib" / b_file.name).is_file() else (package_dir / b_file.name)
            if not target.is_file() or compute_sha256(target) != compute_sha256(b_file):
                return True
        return False

    # 若安装目录不存在、缺少 marker、或可执行文件及任一工程库哈希与当前构建不一致，触发 packaging 脚本生成/同步最新构建
    if not (package_dir / ".dltool_package").is_file() or project_binary_mismatch():
        package_script = ROOT / "tools" / "package_app.py"
        pack_cmd = [
            sys.executable,
            str(package_script),
            "--build-dir",
            "build",
            "--install-dir",
            str(package_dir),
            "--config",
            "release",
            "--allow-missing-dependencies",
        ]
        if (package_dir / ".dltool_package").is_file():
            pack_cmd.append("--no-clean")
        pack_res = subprocess.run(
            pack_cmd,
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            timeout=180,
        )
        assert pack_res.returncode == 0, f"Packaging failed: {pack_res.stderr}\nStdout: {pack_res.stdout}"

    assert app_exe.is_file(), f"Packaged executable not found at {app_exe}"
    build_exe_hash = compute_sha256(build_exe)
    app_exe_hash = compute_sha256(app_exe)
    assert app_exe_hash == build_exe_hash, (
        f"Packaged executable SHA-256 ({app_exe_hash}) does not match current build ({build_exe_hash})"
    )

    # 验证当前构建生成的全部工程核心动态库与安装目录中的动态库 SHA-256 完全逐位匹配
    if os.name == "nt":
        assert len(project_binaries) >= 8, f"Expected at least 8 project DLLs in {build_bin_dir}, found {len(project_binaries)}"
        for build_dll in project_binaries:
            pkg_dll = package_dir / build_dll.name
            assert pkg_dll.is_file(), f"Project DLL {build_dll.name} missing from package directory {package_dir}"
            assert compute_sha256(pkg_dll) == compute_sha256(build_dll), (
                f"Packaged DLL {build_dll.name} SHA-256 does not match current build output"
            )
    else:
        for build_so in project_binaries:
            pkg_so = (package_dir / "lib" / build_so.name) if (package_dir / "lib" / build_so.name).is_file() else (package_dir / build_so.name)
            assert pkg_so.is_file(), f"Project shared object {build_so.name} missing from package directory {package_dir}"
            assert compute_sha256(pkg_so) == compute_sha256(build_so), (
                f"Packaged SO {build_so.name} SHA-256 does not match current build output"
            )

    # 验证 package marker 存在且与当前代码库 project VERSION 一致
    version = project_version()

    marker_file = package_dir / ".dltool_package"
    assert marker_file.is_file(), f"Package marker .dltool_package not found in {package_dir}"
    marker_content = marker_file.read_text(encoding="utf-8")
    assert f"version={version}" in marker_content, f"Marker version does not match project version {version}"
    assert "config=release" in marker_content

    # 构造完全纯净的环境变量：彻底排除构建树、外部 Python、源码树与无关 PATH
    clean_env: dict[str, str] = {}
    if os.name == "nt":
        sys_root = os.environ.get("SystemRoot", r"C:\Windows")
        sys32 = os.path.join(sys_root, "System32")
        clean_env["SystemRoot"] = sys_root
        clean_env["PATH"] = f"{package_dir};{sys32};{sys_root}"
        clean_env["QT_QUICK_BACKEND"] = "software"
        clean_env["QSG_RHI_BACKEND"] = "software"
        clean_env["QML_DISABLE_DISK_CACHE"] = "1"
    else:
        clean_env["PATH"] = f"{package_dir}:/usr/bin:/bin"
        clean_env["LD_LIBRARY_PATH"] = f"{package_dir}:{package_dir / 'lib'}"
        clean_env["QT_QPA_PLATFORM"] = "offscreen"
        clean_env["QML_DISABLE_DISK_CACHE"] = "1"

    # 以 package_dir 作为独立 cwd 启动烟测，验证应用在完全隔离环境下正常加载 QML/数据库并安全退出
    proc = subprocess.run(
        [str(app_exe), "--smoke-test"],
        cwd=str(package_dir),
        env=clean_env,
        capture_output=True,
        text=True,
        timeout=25,
    )
    assert proc.returncode == 0, (
        f"Isolated package smoke test failed with code {proc.returncode}: {proc.stderr}\n"
        f"Stdout: {proc.stdout}"
    )
