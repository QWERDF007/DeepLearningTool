cmake_minimum_required(VERSION 3.22)


set(PROJECT_NAME "dltool")
set(PROJECT_NAME_LONG "DeepLearningTool")
project(${PROJECT_NAME}
        LANGUAGES C CXX
        VERSION 0.0.2
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
option(ENABLE_SANITIZER "Enabled sanitized build" OFF)
if(ENABLE_SANITIZER)
    set(DLT_ENABLE_SANITIZER ON CACHE BOOL "Enabled sanitized build" FORCE)
elseif(DLT_ENABLE_SANITIZER)
    set(ENABLE_SANITIZER ON CACHE BOOL "Enabled sanitized build" FORCE)
endif()

# 配置构建树
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" )

include(ConfigBuildTree)
include(ConfigCompiler)
include(ConfigQT)
include(ConfigCUDA)
include(ConfigSQLite)
include(ConfigOpenCV)
include(ConfigInferRT)
include(AddPluginLibrary)

# 从 config/doc_contract.json 读取离线帮助手册路径单一真相源配置（必须位于 add_subdirectory(src) 之前以作用于生产目标）
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/config/doc_contract.json")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/config/doc_contract.json" _dltool_doc_contract_json)
string(JSON DLTOOL_DOC_DIR_NAME GET "${_dltool_doc_contract_json}" "doc_dir_name")
string(JSON DLTOOL_DOC_ENTRY_FILE GET "${_dltool_doc_contract_json}" "doc_entry_file")
set(DLTOOL_DOC_REL_PATH "${DLTOOL_DOC_DIR_NAME}/${DLTOOL_DOC_ENTRY_FILE}")
add_compile_definitions(DLTOOL_DOC_REL_PATH="${DLTOOL_DOC_REL_PATH}")

# 第三方库
add_subdirectory(3rdparty EXCLUDE_FROM_ALL)

add_subdirectory(src)

# 安装配置与 Python 运行时
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/config"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    FILES_MATCHING
        PATTERN "*.yaml"
        PATTERN "*.json"
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/EasyTrain/src/python"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    PATTERN "__pycache__" EXCLUDE
    PATTERN "*.pyc" EXCLUDE
    PATTERN "*.pyo" EXCLUDE
)

# 构建离线帮助手册
if(DLT_BUILD_DOCS)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_custom_target(dltool_docs ALL
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_docs.py"
                --source "${CMAKE_CURRENT_SOURCE_DIR}/docs/manual"
                --output "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${DLTOOL_DOC_DIR_NAME}"
                --python "${Python3_EXECUTABLE}"
        COMMENT "Building and verifying user manual HTML documentation with Sphinx"
    )
    if(IS_ABSOLUTE "${CMAKE_INSTALL_BINDIR}")
        set(_dltool_install_doc_subpath "${CMAKE_INSTALL_BINDIR}/${DLTOOL_DOC_DIR_NAME}")
    else()
        set(_dltool_install_doc_subpath "\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/${DLTOOL_DOC_DIR_NAME}")
    endif()
    install(CODE "
        if(DEFINED ENV{DESTDIR} AND NOT \"\$ENV{DESTDIR}\" STREQUAL \"\")
            set(_dltool_destdir \"\$ENV{DESTDIR}\")
        else()
            set(_dltool_destdir \"\")
        endif()
        set(_dltool_target_doc_dir \"\${_dltool_destdir}${_dltool_install_doc_subpath}\")
        if(EXISTS \"\${_dltool_target_doc_dir}\")
            file(REMOVE_RECURSE \"\${_dltool_target_doc_dir}\")
        endif()
    ")
    install(DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${DLTOOL_DOC_DIR_NAME}"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        PATTERN ".doctrees" EXCLUDE
        PATTERN ".buildinfo" EXCLUDE
    )
    install(CODE "
        if(DEFINED ENV{DESTDIR} AND NOT \"\$ENV{DESTDIR}\" STREQUAL \"\")
            set(_dltool_destdir \"\$ENV{DESTDIR}\")
        else()
            set(_dltool_destdir \"\")
        endif()
        set(_dltool_target_doc_dir \"\${_dltool_destdir}${_dltool_install_doc_subpath}\")
        execute_process(
            COMMAND \"${Python3_EXECUTABLE}\" \"${CMAKE_CURRENT_SOURCE_DIR}/tools/build_docs.py\"
                    --check-only \"\${_dltool_target_doc_dir}\"
            RESULT_VARIABLE _dltool_install_doc_check_result
        )
        if(NOT _dltool_install_doc_check_result EQUAL 0)
            message(FATAL_ERROR \"Installation aborted: documentation integrity verification failed on installed files!\")
        endif()
    ")
endif()

# 构建测试用例
if(DLT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()


message(STATUS "")
include(PrintConfig)
