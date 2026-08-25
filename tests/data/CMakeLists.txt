find_package(Qt6 REQUIRED COMPONENTS Core Test)

add_executable(dltool_data_split_tests
    test_DatasetSplitter.cpp
)

target_link_libraries(dltool_data_split_tests PRIVATE
    Qt6::Core
    Qt6::Test
    dltool_data
    dltool_database
    dltool_core
    dltool_common
)

target_include_directories(dltool_data_split_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/data/include
    ${CMAKE_SOURCE_DIR}/src/database/include
    ${CMAKE_SOURCE_DIR}/src/core/include
    ${CMAKE_SOURCE_DIR}/src/common/include
)

add_test(NAME dltool_data_split_tests COMMAND dltool_data_split_tests)
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
set_tests_properties(dltool_data_split_tests PROPERTIES
    LABELS "ordinary;data"
    ENVIRONMENT_MODIFICATION "${_data_test_path_updates}"
)
unset(_data_test_path_updates)
