add_plugin_library(model
    PRIVATE_LIBS 
        Qt6::Core Qt6::Gui Qt6::Quick Qt6::Network Qt6::WebEngineQuick
        quickui dltool_common dltool_core dltool_ui dltool_settings dltool_database dltool_data
        yaml-cpp::yaml-cpp
    INCLUDE_DIRS 
        ${CMAKE_SOURCE_DIR}/3rdparty/nlohmann 
        ${CMAKE_SOURCE_DIR}/3rdparty/spdlog/include
)
