find_package(Qt6 REQUIRED COMPONENTS Core Gui Test)

dlt_glob_sources(_data_test_sources
    RECURSE
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
    PATTERNS "test_*.cpp"
)

set(_data_test_link_libraries
    Qt6::Core
    Qt6::Gui
    Qt6::Test
    dltool_data
    dltool_database
    dltool_settings
    dltool_parameter
    dltool_core
    dltool_common
    dltool_ui
)

set(_data_test_include_directories
    ${CMAKE_SOURCE_DIR}/src/data/include
    ${CMAKE_SOURCE_DIR}/src/database/include
    ${CMAKE_SOURCE_DIR}/src/settings/include
    ${CMAKE_SOURCE_DIR}/src/parameter/include
    ${CMAKE_SOURCE_DIR}/src/core/include
    ${CMAKE_SOURCE_DIR}/src/common/include
    ${CMAKE_SOURCE_DIR}/src/ui/include
)

set(_data_test_environment_modification
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_data>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_database>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_settings>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_parameter>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_common>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_ui>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:quickui>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Gui>"
    "DLT_RUNTIME_ROOT=set:${CMAKE_BINARY_DIR}/bin"
)

foreach(_data_test_source IN LISTS _data_test_sources)
    get_filename_component(_data_test_case "${_data_test_source}" NAME_WE)
    string(REGEX REPLACE "^test_" "" _data_test_suffix "${_data_test_case}")
    string(REGEX REPLACE "([a-z0-9])([A-Z])" "\\1_\\2" _data_test_suffix "${_data_test_suffix}")
    string(TOLOWER "${_data_test_suffix}" _data_test_suffix)

    dlt_add_test_executable("dltool_data_${_data_test_suffix}_tests"
        SOURCES "${_data_test_source}"
        LINK_LIBRARIES ${_data_test_link_libraries}
        INCLUDE_DIRECTORIES ${_data_test_include_directories}
        LABELS "ordinary;data"
        ENVIRONMENT_MODIFICATION ${_data_test_environment_modification}
    )
endforeach()

unset(_data_test_sources)
unset(_data_test_link_libraries)
unset(_data_test_include_directories)
unset(_data_test_environment_modification)
unset(_data_test_source)
unset(_data_test_case)
unset(_data_test_suffix)
