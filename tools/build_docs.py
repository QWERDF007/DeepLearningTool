"""DeepLearningTool 离线用户手册统一构建与校验脚本。

作为文档构建与离线完整性门禁的单一真相源（SSOT），供 CMake 目标 (dltool_docs) 与发布脚本 (package_app.py) 共同复用。
确保每次构建前清空输出目录以避免陈旧文件残留，启用 -W --keep-going 确保所有警告转化为错误，并在构建完成后
即刻执行严苛的离线完整性校验（校验必需入口、超链接、内部锚点、图片/样式等资源无缺失且无远程依赖）。
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import json
import sys
import urllib.parse
from html.parser import HTMLParser
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
CONTRACT_FILE = REPO_ROOT / "config" / "doc_contract.json"
if not CONTRACT_FILE.is_file():
    raise RuntimeError(f"documentation contract file not found: {CONTRACT_FILE}")

with open(CONTRACT_FILE, "r", encoding="utf-8") as _contract_fp:
    _doc_contract = json.load(_contract_fp)

# 从 config/doc_contract.json 派生的单一真相源常量
DOC_DIR_NAME = _doc_contract["doc_dir_name"]
DOC_ENTRY_FILE = _doc_contract["doc_entry_file"]
DOC_SEARCH_FILE = "search.html"
DOC_INDEX_JS = "searchindex.js"
DOC_REL_PATH = f"{DOC_DIR_NAME}/{DOC_ENTRY_FILE}"

DEFAULT_SOURCE_DIR = REPO_ROOT / "docs" / "manual"
DEFAULT_OUTPUT_DIR = REPO_ROOT / "build" / "bin" / DOC_DIR_NAME


def extract_css_references(css: str) -> list[tuple[str, str]]:
    """按 CSS 语法扫描 @import 与 url(...) 引用，跳过注释与字符串字面量。"""
    refs: list[tuple[str, str]] = []
    i = 0
    n = len(css)

    while i < n:
        ch = css[i]
        # 跳过 /* ... */ 注释
        if ch == "/" and i + 1 < n and css[i + 1] == "*":
            end = css.find("*/", i + 2)
            if end == -1:
                break
            i = end + 2
            continue

        # 跳过空白字符
        if ch in " \t\r\n\f":
            i += 1
            continue

        # 匹配 @import
        if ch == "@" and css[i : i + 7].lower() == "@import":
            if i + 7 < n and (css[i + 7].isalnum() or css[i + 7] in "_-"):
                i += 7
                continue
            i += 7
            while i < n:
                if css[i] in " \t\r\n\f":
                    i += 1
                elif css[i] == "/" and i + 1 < n and css[i + 1] == "*":
                    end = css.find("*/", i + 2)
                    if end == -1:
                        break
                    i = end + 2
                else:
                    break
            if i >= n:
                break

            if css[i] in ('"', "'"):
                quote = css[i]
                start = i + 1
                i += 1
                while i < n and css[i] != quote:
                    if css[i] == "\\" and i + 1 < n:
                        i += 2
                    else:
                        i += 1
                target = css[start:i]
                if i < n:
                    i += 1
                refs.append(("import", target))
            elif css[i : i + 4].lower() == "url(":
                i += 4
                while i < n:
                    if css[i] in " \t\r\n\f":
                        i += 1
                    elif css[i] == "/" and i + 1 < n and css[i + 1] == "*":
                        end = css.find("*/", i + 2)
                        if end == -1:
                            break
                        i = end + 2
                    else:
                        break
                if i < n and css[i] in ('"', "'"):
                    quote = css[i]
                    start = i + 1
                    i += 1
                    while i < n and css[i] != quote:
                        if css[i] == "\\" and i + 1 < n:
                            i += 2
                        else:
                            i += 1
                    target = css[start:i]
                    if i < n:
                        i += 1
                else:
                    start = i
                    while i < n and css[i] not in (")", " ", "\t", "\r", "\n", "\f"):
                        i += 1
                    target = css[start:i]
                while i < n and css[i] != ")":
                    i += 1
                if i < n:
                    i += 1
                refs.append(("import", target))
            continue

        # 匹配普通 url(...)，确保不是 identifier 后缀（如 myurl(...)）
        if css[i : i + 4].lower() == "url(" and (i == 0 or not (css[i - 1].isalnum() or css[i - 1] in "_-")):
            i += 4
            while i < n:
                if css[i] in " \t\r\n\f":
                    i += 1
                elif css[i] == "/" and i + 1 < n and css[i + 1] == "*":
                    end = css.find("*/", i + 2)
                    if end == -1:
                        break
                    i = end + 2
                else:
                    break
            if i < n and css[i] in ('"', "'"):
                quote = css[i]
                start = i + 1
                i += 1
                while i < n and css[i] != quote:
                    if css[i] == "\\" and i + 1 < n:
                        i += 2
                    else:
                        i += 1
                target = css[start:i]
                if i < n:
                    i += 1
            else:
                start = i
                while i < n and css[i] not in (")", " ", "\t", "\r", "\n", "\f"):
                    i += 1
                target = css[start:i]
            while i < n and css[i] != ")":
                i += 1
            if i < n:
                i += 1
            refs.append(("url", target))
            continue

        # 跳过普通字符串字面量，避免 content: "url(...)" 误报
        if ch in ('"', "'"):
            quote = ch
            i += 1
            while i < n and css[i] != quote:
                if css[i] == "\\" and i + 1 < n:
                    i += 2
                else:
                    i += 1
            if i < n:
                i += 1
            continue

        i += 1

    return refs


def parse_srcset_urls(srcset_value: str) -> list[str]:
    """从 srcset 属性中解析图片候选 URL，正确处理包含逗号的 data: URL。"""
    urls: list[str] = []
    pattern = re.compile(
        r'\s*(data:[^,\s]*,[^\s,]*|[^\s,]+)(?:\s+[0-9.]+[wx])?\s*(?:,|$)',
        re.IGNORECASE,
    )
    for match in pattern.finditer(srcset_value):
        url = match.group(1).strip()
        if url:
            urls.append(url)
    return urls


def is_inside_directory(child: Path, parent: Path) -> bool:
    """判断路径是否位于指定父目录内部。"""
    try:
        child.resolve().relative_to(parent.resolve())
        return True
    except ValueError:
        return False


def scan_css_content(css_content: str, source_name: str, base_dir: Path, doc_dir: Path) -> list[str]:
    """扫描 CSS 内容中的远程引用、目录越界及缺失本地资源。"""
    problems: list[str] = []
    refs = extract_css_references(css_content)

    for kind, ref in refs:
        ref = ref.strip()
        if not ref or ref.startswith(("data:", "javascript:")):
            continue
        if kind == "import":
            if ref.startswith(("http:", "https:", "//")):
                problems.append(f"{source_name} imports remote stylesheet: {ref}")
            else:
                path_part = urllib.parse.unquote(urllib.parse.urlsplit(ref).path)
                if path_part:
                    target = (base_dir / path_part).resolve()
                    if not is_inside_directory(target, doc_dir):
                        problems.append(f"{source_name} imports stylesheet outside documentation directory: {ref}")
                    elif not target.is_file():
                        problems.append(f"{source_name} imports missing stylesheet: {ref}")
        elif kind == "url":
            if ref.startswith(("http:", "https:", "//")):
                problems.append(f"{source_name} references remote resource: {ref}")
            else:
                path_part = urllib.parse.unquote(urllib.parse.urlsplit(ref).path)
                if path_part:
                    target = (base_dir / path_part).resolve()
                    if not is_inside_directory(target, doc_dir):
                        problems.append(f"{source_name} references asset outside documentation directory: {ref}")
                    elif not target.is_file():
                        problems.append(f"{source_name} references missing asset: {ref}")

    return problems


class _DocumentationHTMLParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.ids: set[str] = set()
        self.links: list[str] = []
        self.assets: list[tuple[str, str, str]] = []
        self.srcsets: list[str] = []
        self.inline_styles: list[str] = []
        self.style_blocks: list[str] = []
        self._current_tag: str | None = None
        self._style_buffer: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        attr_dict = {k.lower(): (v or "") for k, v in attrs}
        tag_lower = tag.lower()
        self._current_tag = tag_lower

        element_id = attr_dict.get("id")
        if element_id:
            self.ids.add(element_id)

        style = attr_dict.get("style")
        if style:
            self.inline_styles.append(style)

        if tag_lower == "a":
            name = attr_dict.get("name")
            if name:
                self.ids.add(name)
            href = attr_dict.get("href")
            if href:
                self.links.append(href)
        elif tag_lower in {"img", "script", "video", "audio", "source", "iframe"}:
            src = attr_dict.get("src")
            if src:
                self.assets.append((tag_lower, src, ""))
            srcset = attr_dict.get("srcset")
            if srcset:
                self.srcsets.append(srcset)
        elif tag_lower == "link":
            href = attr_dict.get("href")
            rel = attr_dict.get("rel", "").lower()
            if href:
                self.assets.append(("link", href, rel))
        elif tag_lower == "style":
            self._style_buffer = []

    def handle_data(self, data: str) -> None:
        if self._current_tag == "style":
            self._style_buffer.append(data)

    def handle_endtag(self, tag: str) -> None:
        if tag.lower() == "style":
            if self._style_buffer:
                self.style_blocks.append("".join(self._style_buffer))
                self._style_buffer = []
        self._current_tag = None


def verify_documentation_integrity(doc_dir: Path) -> list[str]:
    """校验指定目录中的离线手册完整性、无缺失资源且无远程脚本/样式依赖。"""
    problems: list[str] = []
    if not doc_dir.is_dir():
        return [f"documentation directory not found: {doc_dir}"]

    required_entries = [DOC_ENTRY_FILE, DOC_SEARCH_FILE, DOC_INDEX_JS]
    for entry in required_entries:
        path = doc_dir / entry
        if not path.is_file():
            problems.append(f"missing documentation entry: {entry}")
        elif path.stat().st_size == 0:
            problems.append(f"empty documentation entry: {entry}")

    if (doc_dir / "_sources").exists():
        problems.append("documentation package must not contain source files (_sources)")

    html_files = {p.resolve(): p for p in doc_dir.rglob("*.html")}
    if not html_files:
        problems.append(f"no html files found in {doc_dir}")

    parsers: dict[Path, _DocumentationHTMLParser] = {}
    for abs_path, html_file in html_files.items():
        try:
            content = html_file.read_text(encoding="utf-8")
        except Exception as e:
            problems.append(f"cannot read html file {html_file.name}: {e}")
            continue
        parser = _DocumentationHTMLParser()
        try:
            parser.feed(content)
        except Exception as e:
            problems.append(f"malformed html file {html_file.name}: {e}")
            continue
        parsers[abs_path] = parser

    for abs_path, parser in parsers.items():
        file_name = abs_path.name
        # 1. 校验页面超链接
        for href in parser.links:
            href_clean = href.strip()
            if not href_clean or href_clean.startswith(("http:", "https:", "mailto:", "javascript:", "data:", "ftp:")):
                continue
            parsed = urllib.parse.urlsplit(href_clean)
            path_part = urllib.parse.unquote(parsed.path)
            fragment = urllib.parse.unquote(parsed.fragment)
            if not path_part:
                if fragment and fragment not in parser.ids:
                    problems.append(f"{file_name} links to missing anchor #{fragment}")
            else:
                target = (abs_path.parent / path_part).resolve()
                if not is_inside_directory(target, doc_dir):
                    problems.append(f"{file_name} links to page outside documentation directory: {href_clean}")
                elif not target.is_file():
                    problems.append(f"{file_name} links to missing target: {href_clean}")
                elif fragment and target in parsers:
                    if fragment not in parsers[target].ids:
                        problems.append(f"{file_name} links to missing anchor #{fragment} in {target.name}")

        # 2. 校验 src 与 link 资源
        for tag, src_or_href, rel in parser.assets:
            val_clean = src_or_href.strip()
            if not val_clean or val_clean.startswith(("mailto:", "javascript:", "data:")):
                continue
            if val_clean.startswith(("http:", "https:", "//")):
                problems.append(f"{file_name} loads remote asset: {val_clean}")
                continue
            parsed = urllib.parse.urlsplit(val_clean)
            path_part = urllib.parse.unquote(parsed.path)
            if not path_part:
                continue
            target = (abs_path.parent / path_part).resolve()
            if not is_inside_directory(target, doc_dir):
                problems.append(f"{file_name} references asset outside documentation directory: {val_clean}")
            elif not target.is_file():
                problems.append(f"{file_name} references missing local asset: {val_clean}")

        # 3. 校验 srcset 响应式图片
        for srcset_val in parser.srcsets:
            candidates = parse_srcset_urls(srcset_val)
            for cand in candidates:
                if not cand or cand.startswith(("data:", "javascript:")):
                    continue
                if cand.startswith(("http:", "https:", "//")):
                    problems.append(f"{file_name} loads remote asset in srcset: {cand}")
                else:
                    path_part = urllib.parse.unquote(urllib.parse.urlsplit(cand).path)
                    if path_part:
                        target = (abs_path.parent / path_part).resolve()
                        if not is_inside_directory(target, doc_dir):
                            problems.append(f"{file_name} references asset outside documentation directory: {cand}")
                        elif not target.is_file():
                            problems.append(f"{file_name} references missing local asset in srcset: {cand}")

        # 4. 校验行内 style 属性
        for style_str in parser.inline_styles:
            problems.extend(scan_css_content(style_str, f"{file_name} inline style", abs_path.parent, doc_dir))

        # 5. 校验 <style> 块
        for style_block in parser.style_blocks:
            problems.extend(scan_css_content(style_block, f"{file_name} <style>", abs_path.parent, doc_dir))

    # 6. 校验外链 CSS 文件
    for css_file in doc_dir.rglob("*.css"):
        try:
            content = css_file.read_text(encoding="utf-8", errors="ignore")
        except Exception as e:
            problems.append(f"cannot read stylesheet {css_file.name}: {e}")
            continue
        problems.extend(scan_css_content(content, f"stylesheet {css_file.name}", css_file.parent, doc_dir))

    return problems


def build_docs(
    source_dir: Path = DEFAULT_SOURCE_DIR,
    output_dir: Path = DEFAULT_OUTPUT_DIR,
    python_exe: Path | str | None = None,
) -> Path:
    """构建离线用户手册 HTML 站点，并自动执行完整性门禁校验。

    Args:
        source_dir: Sphinx 文档源目录（包含 conf.py）。
        output_dir: 编译生成的目标 HTML 目录。
        python_exe: 运行 Sphinx 的 Python 解释器路径。若未指定则使用 sys.executable。

    Returns:
        生成完成并通过完整性校验的输出目录路径。

    Raises:
        RuntimeError: 当源目录不存在、构建过程产生告警/错误，或完整性校验未通过时。
    """
    source_dir = Path(source_dir).resolve()
    output_dir = Path(output_dir).resolve()

    if not source_dir.is_dir() or not (source_dir / "conf.py").is_file():
        raise RuntimeError(f"documentation source not found or missing conf.py: {source_dir}")

    # 1. 彻底清空旧输出目录，保证无陈旧产物残留
    if output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    # 2. 确定 Python 解释器
    py = str(python_exe) if python_exe else sys.executable

    # 3. 执行 Sphinx HTML 构建，-W 将告警视作错误，--keep-going 打印全部问题后再退出
    cmd = [
        py,
        "-m",
        "sphinx",
        "-b",
        "html",
        "-W",
        "--keep-going",
        str(source_dir),
        str(output_dir),
    ]
    print(f"building documentation: {' '.join(cmd)}")
    result = subprocess.run(cmd)
    if result.returncode != 0:
        if output_dir.exists():
            shutil.rmtree(output_dir, ignore_errors=True)
        raise RuntimeError(f"Sphinx documentation build failed with exit code {result.returncode}")

    # 4. 构建完成后统一执行离线完整性门禁校验（无论从 CMake 还是 Python 调用均受约束）
    print(f"verifying documentation integrity: {output_dir}")
    problems = verify_documentation_integrity(output_dir)
    if problems:
        if output_dir.exists():
            shutil.rmtree(output_dir, ignore_errors=True)
        raise RuntimeError(
            f"documentation integrity verification failed ({len(problems)} problems):\n"
            + "\n".join(f"  - {p}" for p in problems)
        )

    return output_dir


def main() -> int:
    parser = argparse.ArgumentParser(description="Build and verify DeepLearningTool offline manual with Sphinx.")
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE_DIR, help="Path to Sphinx documentation source.")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT_DIR, help="Path to output HTML directory.")
    parser.add_argument("--python", type=Path, default=None, help="Path to Python interpreter to execute Sphinx.")
    parser.add_argument("--check-only", type=Path, default=None, help="Only run integrity verification on existing doc dir.")

    args = parser.parse_args()
    try:
        if args.check_only:
            problems = verify_documentation_integrity(args.check_only)
            if problems:
                print(f"verification failed with {len(problems)} problems:\n" + "\n".join(f"  - {p}" for p in problems), file=sys.stderr)
                return 1
            print(f"documentation integrity verified: {args.check_only}")
            return 0

        build_docs(source_dir=args.source, output_dir=args.output, python_exe=args.python)
        print(f"documentation build and verification succeeded: {args.output}")
        return 0
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
