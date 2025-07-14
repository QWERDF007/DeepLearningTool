cmake_minimum_required(VERSION 3.18)


set(PROJECT_NAME "dltool")
set(PROJECT_NAME_LONG "DeepLearningTool")
project(${PROJECT_NAME}
        LANGUAGES C CXX
        VERSION 0.0.1
        DESCRIPTION "深度学习工具"
)

# 确保 cuda 主机编译器与我们使用的一致，
if(NOT CMAKE_CUDA_HOST_COMPILER)
    set(CMAKE_CUDA_HOST_COMPILER "${CMAKE_CXX_COMPILER}")
endif()

enable_language(CUDA)

# Used when creating special builds
set(PROJECT_VERSION_SUFFIX "-beta")

# 如果用户没指定安装前缀
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${PROJECT_SOURCE_DIR}/${PROJECT_NAME}-${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}" CACHE PATH "where ${PROJECT_NAME} will be installed" FORCE)
endif()

# 配置构建树的选项
option(DLT_BUILD_TESTS "Enable testsuite" ON)
option(DLT_BUILD_DOCS "Build Docs" OFF)
option(DLT_ENABLE_SANITIZER "Enabled sanitized build" OFF)

# 配置构建树
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" )

include(ConfigBuildTree)
include(ConfigCompiler)
include(ConfigQT)
# include(ConfigCUDA)
include(ConfigSQLite)

# 第三方库
add_subdirectory(3rdparty EXCLUDE_FROM_ALL)

add_subdirectory(src)

# 构建测试用例
if(DLT_BUILD_TESTS)
    add_subdirectory(tests)
endif()

message(STATUS "")
include(PrintConfig)
