include("${CMAKE_CURRENT_LIST_DIR}/ConfigDependencyDefaults.cmake")

dlt_dependency_resolve_path(
    _dlt_sqlite_root _dlt_sqlite_origin sqlite
    VARIABLES SQLITE_ROOT SQLite3_ROOT SQLITE3_ROOT
    ENVIRONMENT_VARIABLES SQLITE_ROOT SQLite3_ROOT SQLITE3_ROOT
    PREFIX_PATHS ${CMAKE_PREFIX_PATH}
    REQUIRED_FILES include/sqlite3.h
)
if(_dlt_sqlite_root)
    dlt_dependency_cache_set(
        SQLITE_ROOT "${_dlt_sqlite_root}" PATH
        "SQLite installation root"
        DLT_DEPENDENCY_SQLITE_ROOT "${_dlt_sqlite_origin}")
endif()

if(DEFINED SQLITE_ROOT AND NOT SQLITE_ROOT STREQUAL "")
    set(SQLite3_ROOT "${SQLITE_ROOT}" CACHE PATH "SQLite3 installation root")
    set(SQLITE3_ROOT "${SQLITE_ROOT}" CACHE PATH "SQLite3 installation root")
    list(PREPEND CMAKE_PREFIX_PATH "${SQLITE_ROOT}")
endif()

find_package(SQLite3 QUIET)

if(NOT TARGET SQLite3::SQLite3)
    find_path(SQLite3_INCLUDE_DIR NAMES sqlite3.h
        HINTS ${SQLITE_ROOT}/include ${SQLITE_ROOT}
        PATH_SUFFIXES include
    )
    find_library(SQLite3_LIBRARY NAMES sqlite3 sqlite3_static
        HINTS ${SQLITE_ROOT}/lib ${SQLITE_ROOT}
        PATH_SUFFIXES lib lib64
    )
    if(SQLite3_INCLUDE_DIR AND SQLite3_LIBRARY)
        add_library(SQLite3::SQLite3 UNKNOWN IMPORTED GLOBAL)
        set_target_properties(SQLite3::SQLite3 PROPERTIES
            IMPORTED_LOCATION "${SQLite3_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${SQLite3_INCLUDE_DIR}"
        )
        set(SQLite3_FOUND TRUE)
    endif()
endif()
