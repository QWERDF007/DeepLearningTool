"""T6 验收：可移动发布包在含中文与空格路径下的完整性与真实运行验证。

本脚本执行完整的发布包验收：
1. 将打包产物移动至包含中文与空格的路径；
2. 验证发布包静态目录布局与 marker 元数据；
3. 验证文档脱机完整性门禁；
4. 真实启动被移动的可执行文件，检查工作目录隔离，并通过 Windows API 逐一核对加载的 DLL 全部源自移动后的发布包目录；
5. 真实执行帮助文档打开调用，验证其脱离构建目录后依然准确定位并打开文档；
6. 使用系统真实浏览器内核（Chrome/Edge）脱机加载移动后发布包的搜索页面，实测中文分词与离线检索结果。
"""

from __future__ import annotations

import ctypes
import json
import os
import re

import shutil
import subprocess
import sys
import tempfile
import time
import urllib.parse
from ctypes import wintypes
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT))

from tools.package_app import (
    DOC_DIR_NAME,
    DOC_ENTRY_FILE,
    MARKER_FILE,
    PROJECT_NAME,
    verify_documentation_integrity,
    verify_package,
)

PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010


def get_loaded_modules(pid: int) -> list[str]:
    """通过 Windows API 查询指定进程所有已加载模块的绝对路径。"""
    kernel32 = ctypes.windll.kernel32
    psapi = ctypes.windll.psapi

    kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel32.OpenProcess.restype = wintypes.HANDLE

    psapi.EnumProcessModules.argtypes = [
        wintypes.HANDLE,
        ctypes.POINTER(wintypes.HMODULE),
        wintypes.DWORD,
        ctypes.POINTER(wintypes.DWORD),
    ]
    psapi.EnumProcessModules.restype = wintypes.BOOL

    psapi.GetModuleFileNameExW.argtypes = [
        wintypes.HANDLE,
        wintypes.HMODULE,
        wintypes.LPWSTR,
        wintypes.DWORD,
    ]
    psapi.GetModuleFileNameExW.restype = wintypes.DWORD

    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel32.CloseHandle.restype = wintypes.BOOL

    h_process = kernel32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)
    if not h_process:
        return []

    try:
        modules = (wintypes.HMODULE * 1024)()
        cb_needed = wintypes.DWORD()
        if psapi.EnumProcessModules(h_process, modules, ctypes.sizeof(modules), ctypes.byref(cb_needed)):
            count = cb_needed.value // ctypes.sizeof(wintypes.HMODULE)
            loaded = []
            for i in range(count):
                buf = ctypes.create_unicode_buffer(1024)
                if psapi.GetModuleFileNameExW(h_process, modules[i], buf, 1024):
                    loaded.append(buf.value)
            return loaded
        return []
    finally:
        kernel32.CloseHandle(h_process)


def find_available_browsers() -> list[tuple[str, Path]]:
    """定位系统中可用的 Chrome 与 Edge 浏览器。"""
    candidates = [
        ("Chrome", Path(r"C:\Program Files\Google\Chrome\Application\chrome.exe")),
        ("Chrome (x86)", Path(r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe")),
        ("Edge", Path(r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe")),
        ("Edge (x64)", Path(r"C:\Program Files\Microsoft\Edge\Application\msedge.exe")),
    ]
    found: list[tuple[str, Path]] = []
    seen_types: set[str] = set()
    for name, candidate in candidates:
        browser_type = "Chrome" if "Chrome" in name else "Edge"
        if candidate.is_file() and browser_type not in seen_types:
            found.append((browser_type, candidate))
            seen_types.add(browser_type)
    if not found:
        raise RuntimeError("No suitable Chrome or Edge browser executable found for browser acceptance test.")
    return found


def verify_movable_package() -> None:
    source_dir = REPO_ROOT / "build" / "Release Package 深度学习工具" / "dltool"
    test_parent = REPO_ROOT / "build" / "测试 目录 with space"
    target_dir = test_parent / "深度学习工具 发布包"

    if not source_dir.is_dir():
        raise RuntimeError(f"source package directory not found: {source_dir}")

    print(f"[1/6] Preparing target directory with Chinese & spaces: {target_dir}")
    if target_dir.exists():
        shutil.rmtree(target_dir)
    test_parent.mkdir(parents=True, exist_ok=True)

    print(f"[2/6] Copying release package to moved location...")
    shutil.copytree(source_dir, target_dir)

    print(f"[3/6] Verifying static package layout, marker and offline docs...")
    verify_package(
        target_dir,
        REPO_ROOT / "build",
        require_qt_runtime=True,
        expected_version="0.0.2",
        require_docs=True,
    )

    marker_content = (target_dir / MARKER_FILE).read_text(encoding="utf-8")
    assert "state=complete" in marker_content, f"Marker missing state=complete:\n{marker_content}"
    assert "architecture=x64" in marker_content

    exe_path = target_dir / f"{PROJECT_NAME}.exe"
    assert exe_path.is_file(), f"missing executable: {exe_path}"

    doc_dir = target_dir / DOC_DIR_NAME
    doc_entry = doc_dir / DOC_ENTRY_FILE
    assert doc_entry.is_file(), f"missing entry: {doc_entry}"

    problems = verify_documentation_integrity(doc_dir)
    if problems:
        raise RuntimeError("Documentation integrity failed in moved package:\n" + "\n".join(problems))

    print(f"[4/6] Verifying runtime execution and DLL module origins from moved directory...")
    # 构建隔离的干净运行环境，排除源码与构建目录
    clean_env = os.environ.copy()
    clean_env.pop("DLT_SOURCE_DIR", None)
    clean_env.pop("DLT_SMOKE_TEST", None)
    # 保证 PATH 不包含 build/bin 或 build/dltool
    system_paths = [p for p in clean_env.get("PATH", "").split(os.pathsep) if "DeepLearningTool" not in p]
    clean_env["PATH"] = str(target_dir) + os.pathsep + os.pathsep.join(system_paths)

    temp_working_dir = Path(tempfile.gettempdir()) / "dltool_movable_cwd"
    temp_working_dir.mkdir(parents=True, exist_ok=True)
    clean_env["DLT_SMOKE_TEST_MS"] = "6000"

    # 启动进程，工作目录设为无关临时目录
    proc = subprocess.Popen(
        [str(exe_path), "--smoke-test"],
        cwd=str(temp_working_dir),
        env=clean_env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    # 在进程运行期间枚举加载的模块（最多等待 15 秒）
    loaded_modules = []
    target_dir_canonical = os.path.normcase(os.path.abspath(str(target_dir)))
    start_time = time.time()
    while time.time() - start_time < 15:
        mods = get_loaded_modules(proc.pid)
        if mods:
            loaded_modules = mods
            if any(os.path.normcase(m).endswith("dltool_core.dll") for m in loaded_modules):
                break
        if proc.poll() is not None:
            break
        time.sleep(0.05)

    stdout_data, stderr_data = proc.communicate(timeout=30)
    assert proc.returncode == 0, f"Executable failed with returncode {proc.returncode}"

    # 校验关键 DLL 的加载来源全部为 target_dir
    checked_dlls = ["qt6core.dll", "qt6gui.dll", "qt6qml.dll", "qt6quick.dll", "dltool_core.dll", "quickui.dll"]
    found_dlls = {}
    for mod in loaded_modules:
        mod_norm = os.path.normcase(os.path.abspath(mod))
        for name in checked_dlls:
            if mod_norm.endswith(name):
                found_dlls[name] = mod
                assert mod_norm.startswith(target_dir_canonical), (
                    f"DLL {name} loaded from incorrect location: {mod} (expected within {target_dir})"
                )

    assert len(found_dlls) >= 4, f"Expected key DLLs to be verified, but found only {len(found_dlls)}: {found_dlls}"

    print(f"  Verified {len(found_dlls)} key DLLs loaded strictly from moved package directory:")
    for name, path in found_dlls.items():
        print(f"    - {name} -> {path}")

    print(f"[5/6] Verifying help documentation open trigger from moved executable...")
    # 5.1 触发真实 QML 菜单项验证 (Header.qml: helpButton -> helpMenu -> helpDocMenuItem)
    url_output_file = temp_working_dir / "help_menu_opened_url.txt"
    if url_output_file.exists():
        url_output_file.unlink()

    menu_proc = subprocess.run(
        [str(exe_path), "--verify-help-menu", str(url_output_file)],
        cwd=str(temp_working_dir),
        env=clean_env,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert menu_proc.returncode == 0, (
        f"--verify-help-menu failed with code {menu_proc.returncode}:\nStdout: {menu_proc.stdout}\nStderr: {menu_proc.stderr}"
    )
    assert url_output_file.is_file(), f"--verify-help-menu did not produce output file: {url_output_file}"
    opened_url_str = url_output_file.read_text(encoding="utf-8").strip()
    print(f"  QML Help menu triggered successfully; recorded opened URL: {opened_url_str}")

    # 核对打开的 URL 严格指向移动后发布包内的 doc/index.html
    expected_doc_uri = doc_entry.as_uri()
    assert urllib.parse.unquote(opened_url_str).lower() == urllib.parse.unquote(expected_doc_uri).lower(), (
        f"Help menu opened URL mismatch: got '{opened_url_str}', expected '{expected_doc_uri}'"
    )

    # 5.2 触发常规 --test-open-doc 命令行选项
    doc_proc = subprocess.run(
        [str(exe_path), "--test-open-doc"],
        cwd=str(temp_working_dir),
        env=clean_env,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert doc_proc.returncode == 0, (
        f"--test-open-doc failed with code {doc_proc.returncode}:\nStdout: {doc_proc.stdout}\nStderr: {doc_proc.stderr}"
    )

    # 5.3 严格负例测试：验证当 doc/index.html 缺失时，HelpController 绝不假成功
    print("  Running negative tests to eliminate false positives when documentation is missing...")
    backup_doc_entry = doc_dir / "index.html.bak"
    try:
        doc_entry.rename(backup_doc_entry)

        # 负例 A: --test-open-doc 必须失败 (明确返回 exit code 1)
        neg_doc_proc = subprocess.run(
            [str(exe_path), "--test-open-doc"],
            cwd=str(temp_working_dir),
            env=clean_env,
            capture_output=True,
            text=True,
            timeout=30,
        )
        assert neg_doc_proc.returncode == 1, (
            f"Negative test failed: --test-open-doc returned code {neg_doc_proc.returncode} instead of 1 when doc/index.html was missing!"
        )
        neg_doc_out = neg_doc_proc.stdout + neg_doc_proc.stderr
        assert "Help document unavailable or not readable" in neg_doc_out, (
            f"Expected error message not found in output:\n{neg_doc_out}"
        )
        assert "帮助文档不可用，请重新安装完整程序。" in neg_doc_out, (
            f"Expected notification message not found in output:\n{neg_doc_out}"
        )

        # 负例 B: --verify-help-menu 必须失败 (明确返回 exit code 2)
        neg_url_file = temp_working_dir / "neg_url.txt"
        neg_menu_proc = subprocess.run(
            [str(exe_path), "--verify-help-menu", str(neg_url_file)],
            cwd=str(temp_working_dir),
            env=clean_env,
            capture_output=True,
            text=True,
            timeout=30,
        )
        assert neg_menu_proc.returncode == 2, (
            f"Negative test failed: --verify-help-menu returned code {neg_menu_proc.returncode} instead of 2 when doc/index.html was missing!"
        )
        neg_menu_out = neg_menu_proc.stdout + neg_menu_proc.stderr
        assert "Help document unavailable or not readable" in neg_menu_out, (
            f"Expected error message not found in output:\n{neg_menu_out}"
        )
        assert "帮助文档不可用，请重新安装完整程序。" in neg_menu_out, (
            f"Expected notification message not found in output:\n{neg_menu_out}"
        )
        print("  Negative tests verified: application correctly rejects opening (codes 1 & 2) and logs warnings when documentation is absent.")
    finally:
        if backup_doc_entry.exists():
            backup_doc_entry.rename(doc_entry)

    # 5.4 负例测试：验证当操作系统/桌面环境拒绝打开 URL 时，程序正确报错拦截 (系统拒绝场景)
    print("  Running negative tests to verify handling when OS rejects URL opening...")
    neg_reject_env = clean_env.copy()
    neg_reject_env["DLTOOL_SIMULATE_SYSTEM_REJECT_OPEN"] = "1"

    neg_reject_doc_proc = subprocess.run(
        [str(exe_path), "--test-open-doc"],
        cwd=str(temp_working_dir),
        env=neg_reject_env,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert neg_reject_doc_proc.returncode == 1, (
        f"Negative test failed: --test-open-doc returned code {neg_reject_doc_proc.returncode} instead of 1 when system rejected opening!"
    )
    reject_doc_out = neg_reject_doc_proc.stdout + neg_reject_doc_proc.stderr
    assert "System rejected opening help document URL" in reject_doc_out, (
        f"Expected rejection reason not found in output:\n{reject_doc_out}"
    )
    assert "无法打开帮助文档，请检查 HTML 文件的默认打开方式。" in reject_doc_out, (
        f"Expected notification message not found in output:\n{reject_doc_out}"
    )

    neg_reject_url_file = temp_working_dir / "neg_reject_url.txt"
    neg_reject_menu_proc = subprocess.run(
        [str(exe_path), "--verify-help-menu", str(neg_reject_url_file)],
        cwd=str(temp_working_dir),
        env=neg_reject_env,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert neg_reject_menu_proc.returncode == 2, (
        f"Negative test failed: --verify-help-menu returned code {neg_reject_menu_proc.returncode} instead of 2 when system rejected opening!"
    )
    reject_menu_out = neg_reject_menu_proc.stdout + neg_reject_menu_proc.stderr
    assert "System rejected opening help document URL" in reject_menu_out, (
        f"Expected rejection reason not found in output:\n{reject_menu_out}"
    )
    assert "无法打开帮助文档，请检查 HTML 文件的默认打开方式。" in reject_menu_out, (
        f"Expected notification message not found in output:\n{reject_menu_out}"
    )
    assert "Verify help menu FAILED" in reject_menu_out, (
        f"Expected menu verification failure not found in output:\n{reject_menu_out}"
    )
    print("  System rejection negative tests verified: application correctly reports failure (codes 1 & 2) and logs detailed reasons when system rejects opening.")

    # 确认恢复后正例再次通过
    doc_proc_recovered = subprocess.run(
        [str(exe_path), "--test-open-doc"],
        cwd=str(temp_working_dir),
        env=clean_env,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert doc_proc_recovered.returncode == 0, "Post-negative-test doc open recovery failed."
    print("  HelpController successfully located and triggered documentation from moved package (positive & negative verified).")

    print(f"[6/6] Verifying offline Chinese search, navigation, theme & highlighting in Chrome & Edge with network isolation...")
    browsers = find_available_browsers()
    assert len(browsers) >= 2, f"Expected both Chrome and Edge to be available, but found: {[b[0] for b in browsers]}"

    # T4 规格全覆盖测试用词：典型用词、空查询及无结果场景
    positive_queries = ["标注", "异常检测", "训练", "评估", "阈值", "导出"]
    empty_query = ""
    non_existent_query = "NonExistentTermXYZ12345"

    for browser_name, browser_exe in browsers:
        print(f"\n  Testing browser engine: {browser_name} ({browser_exe})")

        # 6.1 验证搜索用词、空查询及无结果查询 (各查询独立干净 Profile，杜绝 localStorage 污染)
        for query in positive_queries:
            query_profile = Path(tempfile.mkdtemp(prefix=f"dltool_search_{query}_"))
            try:
                encoded_query = urllib.parse.quote(query)
                search_url = (doc_dir / "search.html").as_uri() + f"?q={encoded_query}"

                cmd = [
                    str(browser_exe),
                    "--headless=new",
                    "--disable-gpu",
                    "--virtual-time-budget=4000",
                    f"--user-data-dir={query_profile}",
                    "--host-resolver-rules=MAP * ~NOTFOUND",
                    "--disable-background-networking",
                    "--disable-default-apps",
                    "--no-first-run",
                    "--dump-dom",
                    search_url,
                ]
                res = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", timeout=15)
                assert res.returncode == 0, f"[{browser_name}] dump-dom failed for query '{query}': {res.stderr}"

                dom = res.stdout
                match = re.search(r'class="search-summary">搜索完成，匹配到\s*(\d+)\s*页。', dom)
                assert match is not None, f"[{browser_name}] Query '{query}' failed to produce search-summary in DOM:\n{dom[:1000]}"
                count = int(match.group(1))
                assert count > 0, f"[{browser_name}] Query '{query}' returned 0 matches in offline search."
                assert '<ul class="search">' in dom
                print(f"    - Query '{query}': matched {count} pages offline successfully.")

                # 针对代表性查询（标注、训练）提取真实搜索结果链接并执行跳转与动态高亮验证
                if query in ["标注", "训练"]:
                    m_ul = re.search(r'<ul class="search">(.*?)</ul>', dom, re.DOTALL)
                    assert m_ul is not None, f"[{browser_name}] <ul class='search'> not found in search results for '{query}'"
                    links = re.findall(r'<a href="([^"]+)"[^>]*>([^<]+)</a>', m_ul.group(1))
                    assert len(links) > 0, f"[{browser_name}] No result links found for query '{query}'"

                    # 提取真实搜索结果页面链接（如 guide_annotation.html / guide_training.html 或首选链接）
                    target_href = next((h for h, t in links if ("annotation" in h or "training" in h)), links[0][0])
                    # 在保留该次搜索 localStorage 的 profile 中直接加载真实跳转目标
                    target_url = (doc_dir / target_href).as_uri()
                    nav_cmd = [
                        str(browser_exe),
                        "--headless=new",
                        "--disable-gpu",
                        "--virtual-time-budget=4000",
                        f"--user-data-dir={query_profile}",
                        "--host-resolver-rules=MAP * ~NOTFOUND",
                        "--disable-background-networking",
                        "--disable-default-apps",
                        "--no-first-run",
                        "--dump-dom",
                        target_url,
                    ]
                    nav_res = subprocess.run(nav_cmd, capture_output=True, text=True, encoding="utf-8", timeout=15)
                    assert nav_res.returncode == 0, f"[{browser_name}] Navigated result dump failed: {nav_res.stderr}"
                    nav_dom = nav_res.stdout

                    assert "styles/furo.css" in nav_dom, f"[{browser_name}] Furo theme stylesheet missing"
                    assert 'class="page"' in nav_dom, f"[{browser_name}] Furo page wrapper missing"

                    # 严格检验高亮元素内容：必须包含且准确等于当前检索词，而非残留词或空标签
                    highlights = re.findall(r'<span class="highlighted">([^<]+)</span>', nav_dom)
                    assert len(highlights) > 0, (
                        f"[{browser_name}] Expected keyword highlights on navigated page '{target_href}' for '{query}', but none found."
                    )
                    assert all(h == query for h in highlights), (
                        f"[{browser_name}] Stale/mismatched highlights detected for query '{query}': got {highlights[:5]}"
                    )
                    print(f"    - [{browser_name}] Navigated from search results to '{target_href}' with {len(highlights)} exact '{query}' keywords highlighted.")
            finally:
                shutil.rmtree(query_profile, ignore_errors=True)

        # 6.2 验证空查询场景
        empty_profile = Path(tempfile.mkdtemp(prefix=f"dltool_search_empty_"))
        try:
            search_url_empty = (doc_dir / "search.html").as_uri() + "?q="
            cmd_empty = [
                str(browser_exe),
                "--headless=new",
                "--disable-gpu",
                "--virtual-time-budget=4000",
                f"--user-data-dir={empty_profile}",
                "--host-resolver-rules=MAP * ~NOTFOUND",
                "--disable-background-networking",
                "--disable-default-apps",
                "--no-first-run",
                "--dump-dom",
                search_url_empty,
            ]
            res_empty = subprocess.run(cmd_empty, capture_output=True, text=True, encoding="utf-8", timeout=15)
            assert res_empty.returncode == 0
            assert 'class="search-summary"' not in res_empty.stdout
            assert '<ul class="search">' not in res_empty.stdout
            print(f"    - Empty query: verified no erroneous results or crash.")
        finally:
            shutil.rmtree(empty_profile, ignore_errors=True)

        # 6.3 验证无结果查询场景
        no_match_profile = Path(tempfile.mkdtemp(prefix=f"dltool_search_nomatch_"))
        try:
            search_url_nomatch = (doc_dir / "search.html").as_uri() + f"?q={non_existent_query}"
            cmd_nomatch = [
                str(browser_exe),
                "--headless=new",
                "--disable-gpu",
                "--virtual-time-budget=4000",
                f"--user-data-dir={no_match_profile}",
                "--host-resolver-rules=MAP * ~NOTFOUND",
                "--disable-background-networking",
                "--disable-default-apps",
                "--no-first-run",
                "--dump-dom",
                search_url_nomatch,
            ]
            res_nomatch = subprocess.run(cmd_nomatch, capture_output=True, text=True, encoding="utf-8", timeout=15)
            assert res_nomatch.returncode == 0
            nomatch_dom = res_nomatch.stdout
            assert ("没有匹配到文档" in nomatch_dom or "匹配到 0 页" in nomatch_dom), (
                f"[{browser_name}] Expected 0-match notice for non-existent query in DOM:\n{nomatch_dom[:1000]}"
            )
            assert '<ul class="search">' not in nomatch_dom or '<li>' not in nomatch_dom
            print(f"    - Non-existent query: verified proper 0-match message offline.")
        finally:
            shutil.rmtree(no_match_profile, ignore_errors=True)

        # 6.4 验证真实 Furo 主题切换（跟随系统/Auto -> 深色/Dark -> 浅色/Light -> Auto 完整循环）
        theme_test_html = temp_working_dir / "theme_test_runner.html"
        index_uri = (doc_dir / "index.html").as_uri()
        theme_test_html.write_text(f"""<!DOCTYPE html>
<html>
<body>
<iframe id="docFrame" src="{index_uri}" style="width:800px;height:600px;"></iframe>
<script>
const frame = document.getElementById('docFrame');
frame.onload = () => {{
    try {{
        const doc = frame.contentDocument;
        const btn = doc.querySelector('.theme-toggle');
        const initialTheme = doc.body.dataset.theme || "auto";
        btn.click();
        const themeAfterClick1 = doc.body.dataset.theme;
        btn.click();
        const themeAfterClick2 = doc.body.dataset.theme;
        btn.click();
        const themeAfterClick3 = doc.body.dataset.theme;

        const resultDiv = document.createElement('div');
        resultDiv.id = 'theme-test-results';
        resultDiv.textContent = JSON.stringify({{
            initial: initialTheme,
            click1: themeAfterClick1,
            click2: themeAfterClick2,
            click3: themeAfterClick3
        }});
        document.body.appendChild(resultDiv);
    }} catch (e) {{
        const errDiv = document.createElement('div');
        errDiv.id = 'theme-test-error';
        errDiv.textContent = e.toString();
        document.body.appendChild(errDiv);
    }}
}};
</script>
</body>
</html>""", encoding="utf-8")

        theme_profile = Path(tempfile.mkdtemp(prefix=f"dltool_theme_"))
        try:
            cmd_theme = [
                str(browser_exe),
                "--headless=new",
                "--disable-gpu",
                "--allow-file-access-from-files",
                "--disable-web-security",
                f"--user-data-dir={theme_profile}",
                "--virtual-time-budget=4000",
                "--dump-dom",
                theme_test_html.as_uri(),
            ]
            theme_res = subprocess.run(cmd_theme, capture_output=True, text=True, encoding="utf-8", timeout=15)
            assert theme_res.returncode == 0, f"[{browser_name}] Theme test runner failed: {theme_res.stderr}"

            m_theme = re.search(r'<div id="theme-test-results">([^<]+)</div>', theme_res.stdout)
            assert m_theme is not None, (
                f"[{browser_name}] Theme cycle results not found in DOM:\n{theme_res.stdout[:1000]}"
            )
            theme_data = json.loads(m_theme.group(1))
            assert theme_data["initial"] == "auto", f"Expected initial auto theme, got {theme_data['initial']}"
            assert theme_data["click1"] == "dark", f"Expected click 1 dark theme, got {theme_data['click1']}"
            assert theme_data["click2"] == "light", f"Expected click 2 light theme, got {theme_data['click2']}"
            assert theme_data["click3"] == "auto", f"Expected click 3 return to auto theme, got {theme_data['click3']}"

            # 验证 index.html meta color-scheme 支持
            index_text = (doc_dir / "index.html").read_text(encoding="utf-8")
            assert '<meta name="color-scheme" content="light dark">' in index_text

            print(f"    - Theme toggle cycle verified: auto -> dark -> light -> auto on real button interaction.")
        finally:
            shutil.rmtree(theme_profile, ignore_errors=True)
            if theme_test_html.exists():
                theme_test_html.unlink()

    shutil.rmtree(temp_working_dir, ignore_errors=True)

    print(f"\n=======================================================")
    print(f"T6 Movable release package runtime acceptance SUCCESSFUL!")
    print(f"Tested location: {target_dir}")
    print(f"=======================================================\n")


if __name__ == "__main__":
    verify_movable_package()

