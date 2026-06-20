add_plugin_library(settings
    QML_URI "dltool.settings"
    PRIVATE_LIBS Qt6::Core Qt6::Qml Qt6::Quick quickui dltool_common dltool_database dltool_ui yaml-cpp::yaml-cpp
)

target_compile_definitions(dltool_settings
    PRIVATE SETTINGS_LIBRARY
)
