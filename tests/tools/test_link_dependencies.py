from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

import pytest

from tools.dependency_utils import remove_existing_dir_link


LINK_SCRIPT = ROOT / "tools" / "link_dependencies.py"


def test_remove_existing_dir_link_removes_empty_real_directory(tmp_path: Path) -> None:
    """Empty directories created by the application at startup must be reclaimable."""

    empty_dir = tmp_path / "config"
    empty_dir.mkdir()

    remove_existing_dir_link(empty_dir)

    assert not empty_dir.exists()


def test_remove_existing_dir_link_refuses_non_empty_directory(tmp_path: Path) -> None:
    """Non-empty real directories stay protected from accidental overwrite."""

    busy_dir = tmp_path / "config"
    busy_dir.mkdir()
    (busy_dir / "keep.txt").write_text("keep", encoding="utf-8")

    with pytest.raises(RuntimeError, match="refusing to overwrite"):
        remove_existing_dir_link(busy_dir)

    assert (busy_dir / "keep.txt").is_file()


def test_remove_existing_dir_link_removes_directory_symlink(tmp_path: Path) -> None:
    """Existing directory symlinks are removed without touching their target."""

    target = tmp_path / "real"
    target.mkdir()
    link = tmp_path / "linked"
    try:
        os.symlink(target, link, target_is_directory=True)
    except OSError:
        pytest.skip("mock filesystem does not allow directory symlinks")

    remove_existing_dir_link(link)

    assert not link.exists()
    assert target.is_dir()


def test_link_dependencies_tolerates_self_created_dirs_and_refreshes_dlls(tmp_path: Path) -> None:
    """The link script must survive app-created empty dirs and replace stale DLL copies."""

    build_dir = tmp_path / "build"
    module_dir = build_dir / "dltool" / "database"
    module_dir.mkdir(parents=True)
    (module_dir / "dltool_database.dll").write_bytes(b"fresh-dll")

    bin_dir = build_dir / "bin"
    bin_dir.mkdir(parents=True)
    # 模拟程序启动自建的空目录与手工复制留下的旧 DLL 实体副本。
    (bin_dir / "config").mkdir()
    (bin_dir / "dltool_database.dll").write_bytes(b"stale-dll")

    result = subprocess.run(
        [
            sys.executable,
            str(LINK_SCRIPT),
            "--build-dir",
            str(build_dir),
            "--config",
            "release",
            "--skip-external",
        ],
        cwd=str(ROOT),
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=180,
    )

    assert result.returncode == 0, result.stdout + result.stderr
    assert (bin_dir / "dltool").is_dir()
    assert (bin_dir / "config").exists()
    linked_dll = bin_dir / "dltool_database.dll"
    assert linked_dll.is_file()
    assert linked_dll.read_bytes() == b"fresh-dll"
