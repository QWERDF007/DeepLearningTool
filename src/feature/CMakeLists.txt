add_plugin_library(feature
    PRIVATE_LIBS Qt6::Core Qt6::Gui Qt6::Qml
        dltool_common dltool_ui dltool_settings
    INCLUDE_DIRS
        ${CMAKE_SOURCE_DIR}/3rdparty/spdlog/include
)

setup_inferrt(${PROJECT_NAME}_feature)
