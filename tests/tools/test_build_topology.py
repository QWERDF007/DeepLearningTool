from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def _source_subdirectories() -> list[str]:
    return re.findall(
        r"^\s*add_subdirectory\(([^)\s]+)\)",
        (ROOT / "src" / "CMakeLists.txt").read_text(encoding="utf-8"),
        re.MULTILINE,
    )


def test_source_subdirectories_follow_the_domain_dependency_order() -> None:
    positions = {name: index for index, name in enumerate(_source_subdirectories())}

    assert positions["common"] < positions["core"]
    assert positions["core"] < positions["database"]
    assert positions["database"] < positions["ui"]
    assert positions["ui"] < positions["parameter"]
    assert positions["parameter"] < positions["settings"]
    assert positions["settings"] < positions["data"]
    assert positions["data"] < positions["model"]
    assert positions["model"] < positions["feature"]
    assert positions["feature"] < positions["project"]
    assert positions["project"] < positions["tool"]
