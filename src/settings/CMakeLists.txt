add_plugin_library(settings
    QML_URI "dltool.settings"
    PRIVATE_LIBS Qt6::Core Qt6::Qml dltool_common dltool_database
)

target_compile_definitions(dltool_settings
    PRIVATE SETTINGS_LIBRARY
)
