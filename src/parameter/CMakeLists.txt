add_plugin_library(parameter
    NO_QML_MODULE
    PUBLIC_LIBS Qt6::Core yaml-cpp::yaml-cpp
    PRIVATE_LIBS dltool_common spdlog::spdlog InferRT::util
)
