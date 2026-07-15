add_plugin_library(parameter
    NO_QML_MODULE
    PUBLIC_LIBS Qt6::Core yaml-cpp::yaml-cpp
    PRIVATE_LIBS dltool_common spdlog::spdlog lfreist-hwinfo::hwinfo
)

if(MSVC)
    # hwinfo/platform.h defines NOMINMAX while the project also passes it
    # globally; suppress only the resulting redefinition warning here.
    target_compile_options(dltool_parameter PRIVATE /wd4005)
endif()
