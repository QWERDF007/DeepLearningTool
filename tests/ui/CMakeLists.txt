set(PLUGIN_NAME "ui")
set(TARGET_NAME "${PROJECT_NAME}_${PLUGIN_NAME}")
set(TEST_TARGET_NAME "tst_${TARGET_NAME}")

dlt_glob_sources(_ui_test_sources
    RECURSE
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
    PATTERNS "*.cpp" "*.h" "*.hpp"
)

set(_test_path_updates
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_common>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_ui>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:quickui>"
    "PATH=path_list_prepend:${Qt6_ROOT}/bin"
)
dlt_add_test_executable(${TEST_TARGET_NAME}
    QT_EXECUTABLE
    SOURCES ${_ui_test_sources}
    LINK_LIBRARIES
        dltool_ui
        Qt6::Core
        Qt6::Test
    INCLUDE_DIRECTORIES
        ${CMAKE_SOURCE_DIR}/tests
    LABELS "ordinary;ui"
    ENVIRONMENT_MODIFICATION "${_test_path_updates}"
)

unset(_ui_test_sources)
unset(_test_path_updates)
