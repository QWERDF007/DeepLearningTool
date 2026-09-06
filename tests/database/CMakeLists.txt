find_package(Qt6 REQUIRED COMPONENTS Core Test)

dlt_glob_sources(_database_test_sources
    RECURSE
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
    PATTERNS "test_*.cpp"
)

set(_database_test_environment
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_database>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_common>"
    "PATH=path_list_prepend:${CMAKE_BINARY_DIR}/bin"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "DLT_RUNTIME_ROOT=set:${CMAKE_BINARY_DIR}/bin"
)

foreach(_database_test_source IN LISTS _database_test_sources)
    get_filename_component(_database_test_case "${_database_test_source}" NAME_WE)
    string(REGEX REPLACE "^test_" "" _database_test_suffix "${_database_test_case}")
    string(REGEX REPLACE "([a-z0-9])([A-Z])" "\\1_\\2" _database_test_suffix "${_database_test_suffix}")
    string(TOLOWER "${_database_test_suffix}" _database_test_suffix)

    dlt_add_test_executable("dltool_database_${_database_test_suffix}_tests"
        SOURCES "${_database_test_source}"
        LINK_LIBRARIES
            Qt6::Core
            Qt6::Test
            dltool_database
            dltool_common
        INCLUDE_DIRECTORIES
            ${CMAKE_SOURCE_DIR}/src/database/include
            ${CMAKE_SOURCE_DIR}/src/common/include
        LABELS "ordinary;database"
        ENVIRONMENT_MODIFICATION "${_database_test_environment}"
    )
endforeach()

unset(_database_test_sources)
unset(_database_test_environment)
unset(_database_test_source)
unset(_database_test_case)
unset(_database_test_suffix)
