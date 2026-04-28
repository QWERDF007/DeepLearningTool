add_plugin_library(database
    NO_QML_MODULE
    PRIVATE_LIBS Qt6::Core
    PUBLIC_LIBS sqlpp11::sqlite3
)
