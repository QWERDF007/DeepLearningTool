from __future__ import annotations

import sys

from tools import run_model_tests


def test_model_script_defaults_to_model_and_qml_labels(monkeypatch) -> None:
    commands: list[list[str]] = []

    monkeypatch.setattr(run_model_tests, "run", lambda command, environment: commands.append(command))
    monkeypatch.setattr(sys, "argv", ["run_model_tests.py", "--skip-build"])

    assert run_model_tests.main() == 0
    assert commands == [
        [
            "ctest",
            "--test-dir",
            str(run_model_tests.REPOSITORY_ROOT / "build"),
            "-C",
            "Release",
            "-L",
            run_model_tests.DEFAULT_MODEL_TEST_LABEL_REGEX,
            "--output-on-failure",
        ]
    ]


def test_model_script_allows_explicit_test_name_regex(monkeypatch) -> None:
    commands: list[list[str]] = []

    monkeypatch.setattr(run_model_tests, "run", lambda command, environment: commands.append(command))
    monkeypatch.setattr(
        sys,
        "argv",
        ["run_model_tests.py", "--skip-build", "--test-regex", "^dltool_model_evaluation_tests$"],
    )

    assert run_model_tests.main() == 0
    assert commands[0][-3:-1] == ["-R", "^dltool_model_evaluation_tests$"]
