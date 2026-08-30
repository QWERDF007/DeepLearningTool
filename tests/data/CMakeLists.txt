find_package(Qt6 REQUIRED COMPONENTS Core Test)

dlt_glob_sources(_data_test_sources
    RECURSE
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
    PATTERNS "test_*.cpp" "test_*.h" "test_*.hpp"
)

set(_data_test_path_updates
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_data>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_database>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_settings>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_parameter>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_ui>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_common>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:quickui>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)
dlt_add_test_executable(dltool_data_split_tests
    SOURCES ${_data_test_sources}
    LINK_LIBRARIES
        Qt6::Core
        Qt6::Test
        dltool_data
        dltool_database
        dltool_core
        dltool_common
    INCLUDE_DIRECTORIES
        ${CMAKE_SOURCE_DIR}/src/data/include
        ${CMAKE_SOURCE_DIR}/src/database/include
        ${CMAKE_SOURCE_DIR}/src/core/include
        ${CMAKE_SOURCE_DIR}/src/common/include
    LABELS "ordinary;data"
    ENVIRONMENT_MODIFICATION "${_data_test_path_updates}"
)

unset(_data_test_sources)
unset(_data_test_path_updates)
