from pathlib import Path
import re
from utils import symlink, find_pattern_in_cmake


if __name__ == '__main__':
    # 编写正则表达式来匹配PYTHON_EXECUTABLE的路径
    pattern = r'set\(CMAKE_PREFIX_PATH "([^"]+)"\)'
    sqlite_home = find_pattern_in_cmake(Path('./cmake/ConfigSQLite.cmake'), pattern)
    print('find sqlite dir:', sqlite_home)
    if sqlite_home:
        sqlite_dll = sqlite_home / 'lib' / 'sqlite3.dll'
        target_dir = Path('./build/bin')
        symlink(sqlite_dll, target_dir / sqlite_dll.name)
