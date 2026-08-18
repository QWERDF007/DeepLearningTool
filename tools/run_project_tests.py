#!/usr/bin/env python3
"""Build and run project-level integration tests."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


DEFAULT_PROJECT_TEST_REGEX = (
    r"^dltool_model_(project_creation|data_(creation|import|export)|"
    r"patchcore_(model|train|predict|evaluation))_test$"
)
PROJECT_LAYER_REGEX = {
    "full": DEFAULT_PROJECT_TEST_REGEX,
    "project-setup": r"^dltool_model_(project_creation|python_environment)_test$",
    "project-creation": r"^dltool_model_project_creation_test$",
    "python": r"^dltool_model_python_environment_test$",
    "data": r"^dltool_model_data_(creation|import|export|roundtrip)_test$",
    "data-creation": r"^dltool_model_data_creation_test$",
    "data-import": r"^dltool_model_data_import_test$",
    "data-export": r"^dltool_model_data_export_test$",
    "data-roundtrip": r"^dltool_model_data_roundtrip_test$",
    "model": r"^dltool_model_patchcore_(model|train|predict|evaluation)_test$",
    "model-creation": r"^dltool_model_patchcore_model_test$",
    "model-train": r"^dltool_model_patchcore_train_test$",
    "model-predict": r"^dltool_model_patchcore_predict_test$",
    "model-evaluation": r"^dltool_model_patchcore_evaluation_test$",
}
REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PYTHON_ENV = r"D:/Software/anaconda3/envs/py312"
DEFAULT_PROJECT_ROOT = r"F:/tmp/pro"
DEFAULT_PROJECT_NAME = "测试项目"
DEFAULT_DATASET_NAME = "测试数据集"


def command_text(command: list[str]) -> str:
    if os.name == "nt":
        return subprocess.list2cmdline(command)
    return " ".join(command)


def run(command: list[str], environment: dict[str, str]) -> None:
    print(f"+ {command_text(command)}", flush=True)
    subprocess.run(command, cwd=REPOSITORY_ROOT, env=environment, check=True)


def reset_project_root(project_root: str) -> None:
    """Remove the configured test project so the selected flow starts clean."""

    root = Path(project_root).expanduser().resolve()
    if root.parent == root or root == Path(root.anchor):
        raise ValueError(f"refusing to reset filesystem root: {root}")
    if root.is_symlink():
        raise ValueError(f"refusing to reset symlink project root: {root}")
    if not root.exists():
        return

    print(f"+ reset project root {root}", flush=True)
    shutil.rmtree(root)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--configuration",
        default="Release",
        help="CMake configuration to build and test (default: Release)",
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=Path("build"),
        help="CMake build directory relative to the repository root (default: build)",
    )
    parser.add_argument(
        "--parallel",
        type=int,
        default=4,
        help="Maximum build parallelism (default: 4)",
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="Run CTest without rebuilding first",
    )
    parser.add_argument(
        "--project-layer",
        choices=tuple(PROJECT_LAYER_REGEX),
        default="full",
        help="Project-level layer to run (default: full)",
    )
    parser.add_argument(
        "--test-regex",
        default=None,
        help="Override the project test selection regular expression",
    )
    parser.add_argument(
        "--python-env",
        default=os.environ.get("DLT_TEST_PYTHON_ENV", DEFAULT_PYTHON_ENV),
        help="Python environment directory passed to tests (default: D:/Software/anaconda3/envs/py312)",
    )
    parser.add_argument(
        "--project-root",
        default=os.environ.get("DLT_TEST_PROJECT_ROOT", DEFAULT_PROJECT_ROOT),
        help="Project test directory (default: F:/tmp/pro)",
    )
    parser.add_argument(
        "--project-name",
        default=os.environ.get("DLT_TEST_PROJECT_NAME", DEFAULT_PROJECT_NAME),
        help="Project name and .dlpro file stem (default: 测试项目)",
    )
    parser.add_argument(
        "--dataset-name",
        default=os.environ.get("DLT_TEST_DATASET_NAME", DEFAULT_DATASET_NAME),
        help="Dataset name used by the project flow (default: 测试数据集)",
    )
    parser.add_argument(
        "--recreate-project",
        action="store_true",
        help="Clear the project root before running the selected project test",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    build_dir = args.build_dir
    if not build_dir.is_absolute():
        build_dir = REPOSITORY_ROOT / build_dir

    environment = os.environ.copy()
    environment.update(
        {
            "QT_QPA_PLATFORM": "offscreen",
            "QT_QUICK_BACKEND": "software",
            "QSG_RHI_BACKEND": "software",
            "QML_DISABLE_DISK_CACHE": "1",
            "DLT_TEST_TMP_ROOT": "F:/tmp",
            "DLT_TEST_PYTHON_ENV": str(args.python_env),
            "DLT_TEST_PROJECT_ROOT": str(args.project_root),
            "DLT_TEST_PROJECT_NAME": str(args.project_name),
            "DLT_TEST_DATASET_NAME": str(args.dataset_name),
        }
    )

    test_regex = args.test_regex or PROJECT_LAYER_REGEX[args.project_layer]

    try:
        if not args.skip_build:
            run(
                [
                    "cmake",
                    "--build",
                    str(build_dir),
                    "--config",
                    args.configuration,
                    "--parallel",
                    str(max(1, args.parallel)),
                ],
                environment,
            )

        if args.project_layer != "python" and (args.project_layer == "full" or args.recreate_project):
            reset_project_root(args.project_root)

        ctest_command = [
            "ctest",
            "--test-dir",
            str(build_dir),
            "-C",
            args.configuration,
            "-R",
            test_regex,
            "--output-on-failure",
        ]
        if args.project_layer != "full":
            ctest_command.extend(["--fixture-exclude-any", ".*"])
        run(ctest_command, environment)
    except FileNotFoundError as error:
        print(f"error: command not found: {error.filename}", file=sys.stderr)
        return 127
    except (OSError, ValueError) as error:
        print(f"error: unable to prepare project test root: {error}", file=sys.stderr)
        return 2
    except subprocess.CalledProcessError as error:
        return error.returncode

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
