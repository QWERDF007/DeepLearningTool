add_plugin_library(common
    NO_QML_MODULE
    PUBLIC_LIBS Qt6::Core spdlog::spdlog yaml-cpp::yaml-cpp
)

# 平台特定库
if(WIN32)
    target_link_libraries(dltool_common PRIVATE Dbghelp)
else()
    target_link_libraries(dltool_common PRIVATE dl)
endif()
