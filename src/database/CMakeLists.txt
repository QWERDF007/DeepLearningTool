add_plugin_library(database
    NO_QML_MODULE
    PRIVATE_LIBS Qt6::Core
    PUBLIC_LIBS sqlpp11::sqlite3
)

target_compile_options(${PROJECT_NAME}_database PRIVATE
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:/bigobj>
)
