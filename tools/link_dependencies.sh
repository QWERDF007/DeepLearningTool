#!/bin/bash

# 执行 link_dltool.py
python3 tools/link_dltool.py
if [[ $? -ne 0 ]]; then
    echo "link dltool dll failed"
    exit 1
else
    echo "link dltool dll success"
fi

# 执行 link_sqlite.py
python3 tools/link_sqlite.py
if [[ $? -ne 0 ]]; then
    echo "link sqlite3 dll failed"
    exit 1
else
    echo "link sqlite3 dll success"
fi

# 执行 link_test.py
python3 tools/link_test.py
if [[ $? -ne 0 ]]; then
    echo "link test failed"
    exit 1
else
    echo "link test success"
fi