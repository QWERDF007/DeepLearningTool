add_plugin_library(feature
    PUBLIC_LIBS dltool_parameter
    PRIVATE_LIBS Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick
        quickui dltool_common dltool_core dltool_ui dltool_settings dltool_data dltool_model yaml-cpp::yaml-cpp
    INCLUDE_DIRS
        ${CMAKE_SOURCE_DIR}/3rdparty/spdlog/include
)

setup_inferrt(feature)
