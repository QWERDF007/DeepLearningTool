"""Tests for offline documentation packaging and verification in package_app.py."""

from __future__ import annotations

import shutil
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.package_app import (
    build_documentation,
    copy_documentation,
    extract_css_references,
    parse_srcset_urls,
    verify_documentation_integrity,
    verify_package,
)


def test_verify_documentation_integrity_valid_manual(tmp_path: Path) -> None:
    """A valid manual with index, search, searchindex and local images must pass."""
    doc_dir = tmp_path / "doc"
    doc_dir.mkdir()
    img_dir = doc_dir / "_images"
    img_dir.mkdir()
    (img_dir / "test.svg").write_text("<svg></svg>", encoding="utf-8")

    (doc_dir / "index.html").write_text(
        '<!DOCTYPE html><html><head><title>Test</title></head>'
        '<body><img src="_images/test.svg"><a href="sub.html#sub-sec">Sub</a></body></html>',
        encoding="utf-8",
    )
    (doc_dir / "sub.html").write_text(
        '<!DOCTYPE html><html><head><title>Sub</title></head>'
        '<body><section id="sub-sec"><h2>Section</h2><a href="index.html">Home</a></section></body></html>',
        encoding="utf-8",
    )
    (doc_dir / "search.html").write_text("<!DOCTYPE html><html><body>Search</body></html>", encoding="utf-8")
    (doc_dir / "searchindex.js").write_text("Search.setIndex({})", encoding="utf-8")

    problems = verify_documentation_integrity(doc_dir)
    assert not problems, f"Expected no problems, got: {problems}"


def test_verify_documentation_integrity_missing_entries(tmp_path: Path) -> None:
    """Missing index, search or searchindex must be caught."""
    doc_dir = tmp_path / "doc"
    doc_dir.mkdir()
    (doc_dir / "index.html").write_text("<html></html>", encoding="utf-8")

    problems = verify_documentation_integrity(doc_dir)
    assert any("search.html" in p for p in problems)
    assert any("searchindex.js" in p for p in problems)


def test_verify_documentation_integrity_empty_file(tmp_path: Path) -> None:
    """Empty entry file must be caught."""
    doc_dir = tmp_path / "doc"
    doc_dir.mkdir()
    (doc_dir / "index.html").write_text("", encoding="utf-8")
    (doc_dir / "search.html").write_text("ok", encoding="utf-8")
    (doc_dir / "searchindex.js").write_text("ok", encoding="utf-8")

    problems = verify_documentation_integrity(doc_dir)
    assert any("empty documentation entry: index.html" in p for p in problems)


def test_verify_documentation_integrity_forbidden_sources_folder(tmp_path: Path) -> None:
    """_sources directory containing source files must not leak into release package."""
    doc_dir = tmp_path / "doc"
    doc_dir.mkdir()
    (doc_dir / "index.html").write_text("<html></html>", encoding="utf-8")
    (doc_dir / "search.html").write_text("ok", encoding="utf-8")
    (doc_dir / "searchindex.js").write_text("ok", encoding="utf-8")
    (doc_dir / "_sources").mkdir()

    problems = verify_documentation_integrity(doc_dir)
    assert any("_sources" in p for p in problems)


def test_verify_documentation_integrity_missing_local_asset(tmp_path: Path) -> None:
    """References to non-existent local images or assets must fail verification."""
    doc_dir = tmp_path / "doc"
    doc_dir.mkdir()
    (doc_dir / "index.html").write_text(
        '<html><body><img src="_images/not_found.png"></body></html>',
        encoding="utf-8",
    )
    (doc_dir / "search.html").write_text("ok", encoding="utf-8")
    (doc_dir / "searchindex.js").write_text("ok", encoding="utf-8")

    problems = verify_documentation_integrity(doc_dir)
    assert any("not_found.png" in p for p in problems)


def test_verify_documentation_integrity_broken_page_link(tmp_path: Path) -> None:
    """Internal links to non-existent pages must be caught."""
    doc_dir = tmp_path / "doc"
    doc_dir.mkdir()
    (doc_dir / "index.html").write_text(
        '<html><body><a href="missing_guide.html">Missing</a></body></html>',
        encoding="utf-8",
    )
    (doc_dir / "search.html").write_text("ok", encoding="utf-8")
    (doc_dir / "searchindex.js").write_text("ok", encoding="utf-8")

    problems = verify_documentation_integrity(doc_dir)
    assert any("missing target: missing_guide.html" in p for p in problems)


def test_verify_documentation_integrity_missing_anchor(tmp_path: Path) -> None:
    """Links to non-existent anchors (cross-page and same-page) must be caught."""
    doc_dir = tmp_path / "doc"
    doc_dir.mkdir()
    (doc_dir / "index.html").write_text(
        '<html><body><a href="sub.html#bad_anchor">Bad Anchor</a><a href="#missing_local">Local</a></body></html>',
        encoding="utf-8",
    )
    (doc_dir / "sub.html").write_text(
        '<html><body><section id="existing_anchor">Hello</section></body></html>',
        encoding="utf-8",
    )
    (doc_dir / "search.html").write_text("ok", encoding="utf-8")
    (doc_dir / "searchindex.js").write_text("ok", encoding="utf-8")

    problems = verify_documentation_integrity(doc_dir)
    assert any("missing anchor #bad_anchor in sub.html" in p for p in problems)
    assert any("missing anchor #missing_local" in p for p in problems)


def test_verify_documentation_integrity_remote_assets_and_attributes(tmp_path: Path) -> None:
    """Remote scripts, stylesheets (regardless of attribute order), and remote images must be flagged."""
    doc_dir = tmp_path / "doc"
    doc_dir.mkdir()
    (doc_dir / "index.html").write_text(
        '<html><head>'
        '<script src="https://cdn.example.com/analytics.js"></script>'
        '<link href="http://cdn.example.com/style.css" rel="stylesheet">'
        '</head><body><img src="https://example.com/logo.png"></body></html>',
        encoding="utf-8",
    )
    (doc_dir / "search.html").write_text("ok", encoding="utf-8")
    (doc_dir / "searchindex.js").write_text("ok", encoding="utf-8")

    problems = verify_documentation_integrity(doc_dir)
    assert any("analytics.js" in p for p in problems)
    assert any("style.css" in p for p in problems)
    assert any("logo.png" in p for p in problems)


def test_verify_documentation_integrity_css_remote_font_and_imports(tmp_path: Path) -> None:
    """CSS files with remote @import, remote fonts or missing assets must be flagged."""
    doc_dir = tmp_path / "doc"
    doc_dir.mkdir()
    css_dir = doc_dir / "_static"
    css_dir.mkdir()
    (css_dir / "style.css").write_text(
        '@import "https://fonts.googleapis.com/css?family=Roboto";\n'
        '@font-face { font-family: RemoteFont; src: url("https://fonts.gstatic.com/s/font.woff2"); }\n'
        '.icon { background: url("missing_icon.png"); }\n',
        encoding="utf-8",
    )
    (doc_dir / "index.html").write_text(
        '<html><head><link rel="stylesheet" href="_static/style.css"></head><body>ok</body></html>',
        encoding="utf-8",
    )
    (doc_dir / "search.html").write_text("ok", encoding="utf-8")
    (doc_dir / "searchindex.js").write_text("ok", encoding="utf-8")

    problems = verify_documentation_integrity(doc_dir)
    assert any("imports remote stylesheet" in p for p in problems)
    assert any("references remote resource" in p for p in problems)
    assert any("references missing asset: missing_icon.png" in p for p in problems)


def test_verify_documentation_integrity_asset_with_query_string(tmp_path: Path) -> None:
    """Missing assets with query strings must still be detected as missing."""
    doc_dir = tmp_path / "doc"
    doc_dir.mkdir()
    (doc_dir / "index.html").write_text(
        '<html><head><script src="_static/missing.js?v=20261015"></script></head><body>ok</body></html>',
        encoding="utf-8",
    )
    (doc_dir / "search.html").write_text("ok", encoding="utf-8")
    (doc_dir / "searchindex.js").write_text("ok", encoding="utf-8")

    problems = verify_documentation_integrity(doc_dir)
    assert any("references missing local asset: _static/missing.js?v=20261015" in p for p in problems)


def test_copy_documentation_missing_build_raises(tmp_path: Path) -> None:
    """copy_documentation raises RuntimeError if build/bin/doc is missing."""
    build_dir = tmp_path / "build"
    build_dir.mkdir()
    install_dir = tmp_path / "install"
    install_dir.mkdir()

    with pytest.raises(RuntimeError, match="documentation directory was not found"):
        copy_documentation(build_dir, install_dir)


def test_copy_documentation_cleans_and_excludes_buildinfo(tmp_path: Path) -> None:
    """copy_documentation cleans target directory and excludes .doctrees and .buildinfo."""
    build_dir = tmp_path / "build"
    src_doc = build_dir / "bin" / "doc"
    src_doc.mkdir(parents=True)
    (src_doc / "index.html").write_text("<h1>Manual</h1>", encoding="utf-8")
    (src_doc / ".buildinfo").write_text("buildinfo", encoding="utf-8")
    (src_doc / ".doctrees").mkdir()
    (src_doc / ".doctrees" / "dummy.pickle").write_text("pickle", encoding="utf-8")

    install_dir = tmp_path / "install"
    old_target = install_dir / "doc"
    old_target.mkdir(parents=True)
    (old_target / "stale_file.html").write_text("old", encoding="utf-8")

    copy_documentation(build_dir, install_dir)

    target = install_dir / "doc"
    assert (target / "index.html").is_file()
    assert not (target / "stale_file.html").exists(), "Old files must not remain after copy"
    assert not (target / ".buildinfo").exists(), ".buildinfo should be excluded"
    assert not (target / ".doctrees").exists(), ".doctrees should be excluded"


def test_build_documentation_cleans_stale_and_fails_on_warning(tmp_path: Path) -> None:
    """build_documentation cleans target and turns warnings into fatal errors (-W --keep-going)."""
    fake_repo = tmp_path / "repo"
    manual_src = fake_repo / "docs" / "manual"
    manual_src.mkdir(parents=True)
    (manual_src / "conf.py").write_text(
        'project = "Test"\nextensions = ["myst_parser"]\n',
        encoding="utf-8",
    )
    (manual_src / "index.md").write_text(
        "# Index\n\n![missing](nonexistent_image.png)\n",
        encoding="utf-8",
    )

    build_dir = tmp_path / "build"
    doc_out = build_dir / "bin" / "doc"
    doc_out.mkdir(parents=True)
    (doc_out / "stale_leftover.html").write_text("stale", encoding="utf-8")

    # Because image is missing, sphinx with -W --keep-going must raise RuntimeError
    with pytest.raises(RuntimeError, match="Sphinx documentation build failed"):
        build_documentation(build_dir, repo_root=fake_repo)

    # The stale file must have been cleared before building
    assert not (doc_out / "stale_leftover.html").exists(), "Stale files must be purged before building"


def test_verify_documentation_integrity_srcset_remote_and_missing(tmp_path: Path) -> None:
    """srcset attributes with remote assets or missing local assets must be caught."""
    doc_dir = tmp_path / "doc"
    doc_dir.mkdir()
    (doc_dir / "index.html").write_text(
        '<html><body>'
        '<img src="dummy.png" srcset="https://cdn.example.com/logo.png 1x, missing_local.png 2x">'
        '</body></html>',
        encoding="utf-8",
    )
    (doc_dir / "dummy.png").write_text("x", encoding="utf-8")
    (doc_dir / "search.html").write_text("ok", encoding="utf-8")
    (doc_dir / "searchindex.js").write_text("ok", encoding="utf-8")

    problems = verify_documentation_integrity(doc_dir)
    assert any("loads remote asset in srcset: https://cdn.example.com/logo.png" in p for p in problems)
    assert any("references missing local asset in srcset: missing_local.png" in p for p in problems)


def test_verify_documentation_integrity_inline_and_block_styles(tmp_path: Path) -> None:
    """Inline style="..." and <style> blocks loading remote resources must be flagged."""
    doc_dir = tmp_path / "doc"
    doc_dir.mkdir()
    (doc_dir / "index.html").write_text(
        '<html><head>'
        '<style>'
        '  @import /* comment */ "https://fonts.example.com/style.css";\n'
        '  /* url(https://false-positive.com/fake.png) */\n'
        '  .header { background: url("missing_bg.png"); }\n'
        '</style>'
        '</head><body>'
        '<div style="background-image: url(\'https://cdn.example.com/inline.png\');"></div>'
        '</body></html>',
        encoding="utf-8",
    )
    (doc_dir / "search.html").write_text("ok", encoding="utf-8")
    (doc_dir / "searchindex.js").write_text("ok", encoding="utf-8")

    problems = verify_documentation_integrity(doc_dir)
    assert any("imports remote stylesheet: https://fonts.example.com/style.css" in p for p in problems)
    assert any("references remote resource: https://cdn.example.com/inline.png" in p for p in problems)
    assert any("references missing asset: missing_bg.png" in p for p in problems)
    # Ensure comment in CSS didn't trigger false positive
    assert not any("false-positive" in p for p in problems)


def test_verify_documentation_integrity_asset_outside_doc_dir(tmp_path: Path) -> None:
    """Assets pointing outside the doc directory must be caught even if the file exists on the host."""
    doc_dir = tmp_path / "doc"
    doc_dir.mkdir()
    outside_file = tmp_path / "host_file.png"
    outside_file.write_text("outside", encoding="utf-8")

    (doc_dir / "index.html").write_text(
        '<html><body><img src="../host_file.png"><a href="../host_file.png">Link</a></body></html>',
        encoding="utf-8",
    )
    (doc_dir / "search.html").write_text("ok", encoding="utf-8")
    (doc_dir / "searchindex.js").write_text("ok", encoding="utf-8")

    problems = verify_documentation_integrity(doc_dir)
    assert any("references asset outside documentation directory: ../host_file.png" in p for p in problems)
    assert any("links to page outside documentation directory: ../host_file.png" in p for p in problems)


def test_extract_css_references_and_comment_bypass() -> None:
    """CSS tokenizer handles comments, spaces, quotes, and prevents @import bypasses."""
    # Comment without spaces before/after
    refs = extract_css_references('@import/*comment*/"https://cdn.example.com/a.css";')
    assert refs == [("import", "https://cdn.example.com/a.css")]

    # Multiple comments
    refs = extract_css_references('@import/*c1*//*c2*/url("https://cdn.example.com/b.css");')
    assert refs == [("import", "https://cdn.example.com/b.css")]

    # Direct import without spaces
    refs = extract_css_references('@import"direct.css";')
    assert refs == [("import", "direct.css")]

    # Comments enclosing import or url must NOT be extracted
    refs = extract_css_references('/* @import "fake.css"; */\n/* url(https://fake.com/bg.png) */')
    assert refs == []

    # url inside string property value must NOT be extracted
    refs = extract_css_references('div { content: "url(https://fake-in-string.com)"; }')
    assert refs == []

    # Valid background url
    refs = extract_css_references('div { background: url("valid.png"); }')
    assert refs == [("url", "valid.png")]


def test_parse_srcset_urls_data_uri_and_descriptors() -> None:
    """srcset parser correctly extracts data: URIs containing commas without corrupting tokens."""
    val = "data:image/png;base64,iVBORw0KGgoAAAANSUhEUg== 1x, second.png 2x, third.jpg"
    urls = parse_srcset_urls(val)
    assert urls == [
        "data:image/png;base64,iVBORw0KGgoAAAANSUhEUg==",
        "second.png",
        "third.jpg",
    ]


def test_verify_documentation_integrity_css_comment_bypass_and_srcset_data_uri(tmp_path: Path) -> None:
    """Negative and positive verification for @import/*comment*/ bypass and data URI in srcset."""
    doc_dir = tmp_path / "doc"
    doc_dir.mkdir()
    (doc_dir / "valid.png").write_text("x", encoding="utf-8")

    (doc_dir / "index.html").write_text(
        '<html><head>'
        '<style>@import/*bypass*/"https://cdn.example.com/bypass.css";</style>'
        '</head><body>'
        '<img src="valid.png" srcset="data:image/png;base64,iVBORw0KGgoAAAANSUhEUg== 1x, valid.png 2x">'
        '</body></html>',
        encoding="utf-8",
    )
    (doc_dir / "search.html").write_text("ok", encoding="utf-8")
    (doc_dir / "searchindex.js").write_text("ok", encoding="utf-8")

    problems = verify_documentation_integrity(doc_dir)
    # The remote import must be intercepted
    assert any("imports remote stylesheet: https://cdn.example.com/bypass.css" in p for p in problems)
    # The data: URI must NOT cause any false positive missing local asset errors
    assert not any("base64" in p for p in problems)
    assert not any("references missing local asset in srcset" in p for p in problems)


def test_documentation_constants_ssot() -> None:
    """Document constants must match config/doc_contract.json and across modules."""
    import json
    from tools.build_docs import CONTRACT_FILE, DOC_DIR_NAME, DOC_ENTRY_FILE, DOC_REL_PATH
    from tools.package_app import DOC_DIR_NAME as PKG_DIR, DOC_ENTRY_FILE as PKG_ENTRY, DOC_REL_PATH as PKG_REL

    assert CONTRACT_FILE.is_file()
    with open(CONTRACT_FILE, "r", encoding="utf-8") as f:
        contract = json.load(f)

    assert contract["doc_dir_name"] == DOC_DIR_NAME == PKG_DIR == "doc"
    assert contract["doc_entry_file"] == DOC_ENTRY_FILE == PKG_ENTRY == "index.html"
    assert DOC_REL_PATH == PKG_REL == "doc/index.html"


def test_build_docs_runs_integrity_check(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """build_docs must automatically run verify_documentation_integrity after Sphinx."""
    from tools.build_docs import build_docs

    source_dir = tmp_path / "src"
    source_dir.mkdir()
    (source_dir / "conf.py").write_text("", encoding="utf-8")
    output_dir = tmp_path / "out"

    # Mock subprocess.run to simulate Sphinx creating an incomplete output directory
    import subprocess
    def mock_run(cmd: list[str]) -> subprocess.CompletedProcess[str]:
        output_dir.mkdir(parents=True, exist_ok=True)
        # Only create index.html, leave search.html and searchindex.js missing
        (output_dir / "index.html").write_text("<html></html>", encoding="utf-8")
        return subprocess.CompletedProcess(cmd, 0)

    monkeypatch.setattr(subprocess, "run", mock_run)

    with pytest.raises(RuntimeError, match="documentation integrity verification failed"):
        build_docs(source_dir=source_dir, output_dir=output_dir)


def test_windows_marker_records_package_completeness(tmp_path: Path) -> None:
    """Marker must record state=complete for full release and state=incomplete when skipped."""
    from tools.package_app import MARKER_FILE, write_windows_marker

    install_dir = tmp_path / "install"
    install_dir.mkdir()
    build_dir = tmp_path / "build"
    build_dir.mkdir()

    write_windows_marker(install_dir, build_dir, "x64", "1.0.0", skip_dependencies=False, include_qml_module_dir=True, state="complete")
    content = (install_dir / MARKER_FILE).read_text(encoding="utf-8")
    assert "state=complete" in content

    write_windows_marker(install_dir, build_dir, "x64", "1.0.0", skip_dependencies=True, include_qml_module_dir=True, state="incomplete")
    content_incomplete = (install_dir / MARKER_FILE).read_text(encoding="utf-8")
    assert "state=incomplete" in content_incomplete


def test_production_main_preserves_incomplete_marker_when_verify_fails(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    """Production main() must write incomplete marker before verify_package and retain it when verify fails."""
    import tools.package_app as pkg_app

    build_dir = tmp_path / "build"
    (build_dir / "bin").mkdir(parents=True, exist_ok=True)
    (build_dir / "bin" / "dltool.exe").write_text("binary", encoding="utf-8")
    (build_dir / "bin" / "dltool").write_text("binary", encoding="utf-8")

    install_dir = tmp_path / "install"
    dep_file = tmp_path / "dependencies.yaml"
    dep_file.write_text("dependencies: []\n", encoding="utf-8")

    monkeypatch.setattr(sys, "argv", [
        "package_app.py",
        "--build-dir", str(build_dir),
        "--install-dir", str(install_dir),
        "--dependencies", str(dep_file),
        "--force-clean",
    ])

    # Mock heavy external boundaries (both Windows and Unix)
    monkeypatch.setattr(pkg_app, "copy_project_runtime", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "copy_settings_config", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "copy_model_configs", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "copy_easytrain_python_runtime", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "build_documentation", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "copy_documentation", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "copy_msvc_runtime", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "copy_windows_sdk_dependencies", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "invoke_windeployqt", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "cleanup_release_debug_artifacts", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "target_architecture", lambda *a, **kw: "x64")
    monkeypatch.setattr(pkg_app, "copy_unix_qt_runtime", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "copy_elf_dependencies", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "patch_rpath_if_possible", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "write_unix_launcher", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "write_qt_conf", lambda *a, **kw: None)

    # Simulate verify_package failing (e.g. missing critical runtime DLL)
    class SimulatedVerifyFailure(RuntimeError):
        pass

    def mock_failing_verify(*a, **kw):
        raise SimulatedVerifyFailure("Simulated missing runtime library in verification")

    monkeypatch.setattr(pkg_app, "verify_package", mock_failing_verify)

    with pytest.raises(SimulatedVerifyFailure):
        pkg_app.main()

    # The marker created by main() on disk MUST be state=incomplete
    marker_content = (install_dir / pkg_app.MARKER_FILE).read_text(encoding="utf-8")
    assert "state=incomplete" in marker_content
    assert "state=complete" not in marker_content


def test_production_main_marks_incomplete_when_dependencies_missing_under_allow_missing(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    """Production main() must mark release as incomplete when dependencies are missing even with --allow-missing."""
    import tools.package_app as pkg_app

    build_dir = tmp_path / "build"
    (build_dir / "bin").mkdir(parents=True, exist_ok=True)
    (build_dir / "bin" / "dltool.exe").write_text("binary", encoding="utf-8")
    (build_dir / "bin" / "dltool").write_text("binary", encoding="utf-8")

    install_dir = tmp_path / "install"
    dep_file = tmp_path / "dependencies.yaml"
    dep_file.write_text(
        """
dependencies:
  - name: missing_runtime
    root: F:/definitely_absent_path
    all:
      - missing_runtime.*
    windows:
      - missing_runtime.dll
    linux:
      - libmissing_runtime.so*
    macos:
      - libmissing_runtime.dylib
""",
        encoding="utf-8",
    )

    monkeypatch.setattr(sys, "argv", [
        "package_app.py",
        "--build-dir", str(build_dir),
        "--install-dir", str(install_dir),
        "--dependencies", str(dep_file),
        "--allow-missing-dependencies",
        "--force-clean",
    ])

    # Mock heavy external boundaries (both Windows and Unix)
    monkeypatch.setattr(pkg_app, "copy_project_runtime", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "copy_settings_config", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "copy_model_configs", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "copy_easytrain_python_runtime", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "build_documentation", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "copy_documentation", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "copy_msvc_runtime", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "copy_windows_sdk_dependencies", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "invoke_windeployqt", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "cleanup_release_debug_artifacts", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "target_architecture", lambda *a, **kw: "x64")
    monkeypatch.setattr(pkg_app, "verify_package", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "copy_unix_qt_runtime", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "copy_elf_dependencies", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "patch_rpath_if_possible", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "write_unix_launcher", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "write_qt_conf", lambda *a, **kw: None)

    ret = pkg_app.main()
    assert ret == 0

    # The marker written by main() MUST be state=incomplete
    marker_content = (install_dir / pkg_app.MARKER_FILE).read_text(encoding="utf-8")
    assert "state=incomplete" in marker_content
    assert "state=complete" not in marker_content


def test_production_main_commits_complete_marker_on_clean_success(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    """Production main() must commit state=complete marker only when verification and all dependencies pass."""
    import tools.package_app as pkg_app

    build_dir = tmp_path / "build"
    (build_dir / "bin").mkdir(parents=True, exist_ok=True)
    (build_dir / "bin" / "dltool.exe").write_text("binary", encoding="utf-8")
    (build_dir / "bin" / "dltool").write_text("binary", encoding="utf-8")

    install_dir = tmp_path / "install"
    dep_file = tmp_path / "dependencies.yaml"
    dep_file.write_text("dependencies: []\n", encoding="utf-8")

    monkeypatch.setattr(sys, "argv", [
        "package_app.py",
        "--build-dir", str(build_dir),
        "--install-dir", str(install_dir),
        "--dependencies", str(dep_file),
        "--force-clean",
    ])

    # Mock heavy external boundaries (both Windows and Unix)
    monkeypatch.setattr(pkg_app, "copy_project_runtime", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "copy_settings_config", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "copy_model_configs", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "copy_easytrain_python_runtime", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "build_documentation", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "copy_documentation", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "copy_msvc_runtime", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "copy_windows_sdk_dependencies", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "invoke_windeployqt", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "cleanup_release_debug_artifacts", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "target_architecture", lambda *a, **kw: "x64")
    monkeypatch.setattr(pkg_app, "verify_package", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "copy_unix_qt_runtime", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "copy_elf_dependencies", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "patch_rpath_if_possible", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "write_unix_launcher", lambda *a, **kw: None)
    monkeypatch.setattr(pkg_app, "write_qt_conf", lambda *a, **kw: None)

    ret = pkg_app.main()
    assert ret == 0

    # The marker written by main() MUST be state=complete
    marker_content = (install_dir / pkg_app.MARKER_FILE).read_text(encoding="utf-8")
    assert "state=complete" in marker_content
    assert "state=incomplete" not in marker_content


def test_verify_documentation_integrity_directory_masquerading_as_asset(tmp_path: Path) -> None:
    """A directory created where an asset file (e.g. script/image/css) is expected must fail verification."""
    doc_dir = tmp_path / "doc"
    doc_dir.mkdir()
    (doc_dir / "index.html").write_text(
        '<!DOCTYPE html><html><head><script src="_static/searchtools.js"></script></head><body>OK</body></html>',
        encoding="utf-8",
    )
    (doc_dir / "search.html").write_text("<!DOCTYPE html><html><body>Search</body></html>", encoding="utf-8")
    (doc_dir / "searchindex.js").write_text("Search.setIndex({})", encoding="utf-8")
    fake_asset = doc_dir / "_static" / "searchtools.js"
    fake_asset.mkdir(parents=True)

    problems = verify_documentation_integrity(doc_dir)
    assert any("references missing local asset: _static/searchtools.js" in p for p in problems)


def test_build_docs_cleans_output_dir_on_failure(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """When Sphinx build fails, the partial output directory must be deleted so it cannot be installed."""
    import tools.build_docs as bd
    source_dir = tmp_path / "source"
    source_dir.mkdir()
    (source_dir / "conf.py").write_text("", encoding="utf-8")
    output_dir = tmp_path / "output"
    output_dir.mkdir()
    (output_dir / "partial.html").write_text("incomplete", encoding="utf-8")

    class FakeCompletedProcess:
        returncode = 1

    monkeypatch.setattr(bd.subprocess, "run", lambda *a, **kw: FakeCompletedProcess())

    with pytest.raises(RuntimeError, match="Sphinx documentation build failed with exit code 1"):
        bd.build_docs(source_dir=source_dir, output_dir=output_dir)

    assert not output_dir.exists(), "Output directory must be cleaned up on failure"




