set(SQLITE_ROOT "D:/Software/dev/sqlite-3.46.0" CACHE PATH "SQLite installation root" FORCE)
set(CMAKE_PREFIX_PATH "${SQLITE_ROOT}")
# Windows 编译 sqlite3.lib
# https://gist.github.com/zeljic/d8b542788b225b1bcb5fce169ee28c55
