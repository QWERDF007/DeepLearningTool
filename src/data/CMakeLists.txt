add_plugin_library(data
    PRIVATE_LIBS Qt6::Core Qt6::Gui Qt6::Quick
        quickui dltool_common dltool_core dltool_ui dltool_settings dltool_database
    PUBLIC_LIBS dltool_feature
    INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/3rdparty/nlohmann ${CMAKE_SOURCE_DIR}/3rdparty/spdlog/include
)
