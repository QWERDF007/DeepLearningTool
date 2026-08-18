#!/usr/bin/env python3
"""Build and run ordinary model module tests."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path


DEFAULT_MODEL_TEST_REGEX = (
    r"^dltool_model_.*_tests$|^tst_dltool_model_qml$|^tst_dltool_model_qml_registry$|"
    r"^tst_dltool_model_qml_smoke$"
)
REPOSITORY_ROOT = Path(__file__).resolve().parents[1]


def command_text(command: list[str]) -> str:
    if os.name == "nt":
        return subprocess.list2cmdline(command)
    return " ".join(command)


def run(command: list[str], environment: dict[str, str]) -> None:
    print(f"+ {command_text(command)}", flush=True)
    subprocess.run(command, cwd=REPOSITORY_ROOT, env=environment, check=True)


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
        "--test-regex",
        default=None,
        help="Override the model test selection regular expression",
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
        }
    )

    test_regex = args.test_regex or DEFAULT_MODEL_TEST_REGEX

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

        run(
            [
                "ctest",
                "--test-dir",
                str(build_dir),
                "-C",
                args.configuration,
                "-R",
                test_regex,
                "--output-on-failure",
            ],
            environment,
        )
    except FileNotFoundError as error:
        print(f"error: command not found: {error.filename}", file=sys.stderr)
        return 127
    except subprocess.CalledProcessError as error:
        return error.returncode

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
