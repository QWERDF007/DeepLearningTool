add_plugin_library(project
    PRIVATE_LIBS Qt6::Core Qt6::Gui Qt6::Quick
        dltool_ui dltool_data dltool_database  dltool_settings dltool_common 
    INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/3rdparty/nlohmann
)
