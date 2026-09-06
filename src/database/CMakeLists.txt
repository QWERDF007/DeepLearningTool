add_plugin_library(database
    NO_QML_MODULE
    PRIVATE_LIBS Qt6::Core
    PUBLIC_LIBS sqlpp11::sqlite3
)

file(GLOB _database_schema_files CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/database/ddl/create_*.sql"
)
qt_add_resources(dltool_database database_schema_resources
    PREFIX "/dltool/database/schema"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/include/database/ddl"
    FILES ${_database_schema_files}
)
unset(_database_schema_files)

target_compile_options(${PROJECT_NAME}_database PRIVATE
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:/bigobj>
)
