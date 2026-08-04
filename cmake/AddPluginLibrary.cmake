# 添加插件库的通用函数（支持 Qt、可配置静态/动态库）
# 参数:
#   PLUGIN_NAME           - 插件名称
#   STATIC                - 构建为静态库（默认构建为动态库 SHARED）
#   NO_QML_MODULE         - 不添加 QML 模块
#   QML_URI               - QML 模块 URI（默认: ${PROJECT_NAME}.${PLUGIN_NAME}）
#   QML_VERSION           - QML 模块版本（默认: 1.0）
#   QML_PLUGIN_DIRECTORY  - QML 插件输出目录
#   PRIVATE_LIBS          - PRIVATE 链接的库列表
#   PUBLIC_LIBS           - PUBLIC 链接的库列表
#   QML_FILES             - 额外的 QML 文件（与自动收集的 *.qml 合并）
#   INCLUDE_DIRS          - 额外的 PRIVATE include 目录
function(add_plugin_library PLUGIN_NAME)
    # 解析函数参数
    set(options STATIC NO_QML_MODULE)
    set(oneValueArgs QML_URI QML_VERSION QML_PLUGIN_DIRECTORY)
    set(multiValueArgs PRIVATE_LIBS PUBLIC_LIBS QML_FILES INCLUDE_DIRS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    set(TARGET_NAME "${PROJECT_NAME}_${PLUGIN_NAME}")

    # 指定可执行文件和库文件的输出目录
    set(_OUTPUT_DIR "${CMAKE_BINARY_DIR}/${PROJECT_NAME}/${PLUGIN_NAME}")

    # 获取所有源文件的相对路径
    file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
        RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
        *.cpp *.cu
    )

    # 获取所有头文件的相对路径
    file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS
        RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
        *.h *.hpp *.cuh
    )

    # 获取所有QML文件的相对路径
    file(GLOB_RECURSE QML_SOURCES CONFIGURE_DEPENDS
        RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
        *.qml
    )
    if(QML_SOURCES)
        source_group("QML Files" FILES ${QML_SOURCES})
    endif()

    # 确定库类型
    if(ARG_STATIC)
        set(LIB_TYPE STATIC)
    else()
        set(LIB_TYPE SHARED)
    endif()

    # 使用 Qt 添加库
    qt_add_library(${TARGET_NAME} ${LIB_TYPE}
        ${SOURCES}
        ${HEADERS}
        ${QML_SOURCES}
    )

    # MSVC 去掉 Debug Release 文件夹的嵌套
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${_OUTPUT_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${_OUTPUT_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${_OUTPUT_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${_OUTPUT_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${_OUTPUT_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${_OUTPUT_DIR}"
    )

    # QML 模块
    if(NOT ARG_NO_QML_MODULE)
        if(NOT ARG_QML_URI)
            set(ARG_QML_URI "${PROJECT_NAME}.${PLUGIN_NAME}")
        endif()
        if(NOT ARG_QML_VERSION)
            set(ARG_QML_VERSION 1.0)
        endif()
        if(NOT ARG_QML_PLUGIN_DIRECTORY)
            set(ARG_QML_PLUGIN_DIRECTORY "${CMAKE_BINARY_DIR}/${PROJECT_NAME}/${PLUGIN_NAME}")
        endif()

        set(_qml_module_args
            PLUGIN_TARGET ${TARGET_NAME}
            OUTPUT_DIRECTORY ${ARG_QML_PLUGIN_DIRECTORY}
            VERSION ${ARG_QML_VERSION}
            URI ${ARG_QML_URI}
            SOURCES ${SOURCES}
        )

        # 合并自动收集和手动指定的 QML 文件
        set(ALL_QML_FILES ${QML_SOURCES} ${ARG_QML_FILES})
        if(ALL_QML_FILES)
            list(REMOVE_DUPLICATES ALL_QML_FILES)
            list(APPEND _qml_module_args QML_FILES ${ALL_QML_FILES})
        endif()

        qt_add_qml_module(${TARGET_NAME} ${_qml_module_args})

        # 如果编译模式是Debug或RelWithDebInfo，则添加预处理定义QT_QML_DEBUG
        target_compile_definitions(${TARGET_NAME}
            PRIVATE $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:QT_QML_DEBUG>
        )
    endif()

    # 链接库
    target_link_libraries(${TARGET_NAME}
        PRIVATE
            ${ARG_PRIVATE_LIBS}
        PUBLIC
            ${ARG_PUBLIC_LIBS}
    )

    # include 目录
    target_include_directories(${TARGET_NAME}
        PRIVATE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/${PLUGIN_NAME}>
            $<INSTALL_INTERFACE:include>
            ${ARG_INCLUDE_DIRS}
    )

    # 将名称转换为大写
    string(TOUPPER ${TARGET_NAME} TARGET_NAME_UPPER)
    string(TOUPPER ${PLUGIN_NAME} PLUGIN_NAME_UPPER)

    # 设置编译定义（导出宏）
    # https://stackoverflow.com/a/67923443
    if(ARG_STATIC)
        # 静态库: PUBLIC 定义 STATIC_LIBS，使消费者看到 API 为空（无需导入导出）
        target_compile_definitions(${TARGET_NAME} PUBLIC ${TARGET_NAME_UPPER}_STATIC_LIBS)
    else()
        # 动态库: PRIVATE 定义 BUILD_SHARED_LIBS，编译时导出符号
        target_compile_definitions(${TARGET_NAME} PRIVATE ${TARGET_NAME_UPPER}_BUILD_SHARED_LIBS)
    endif()

    # 使用 configure_file 生成 Export.h
    configure_file(
        ${CMAKE_SOURCE_DIR}/cmake/Export.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/include/${PROJECT_NAME}/${PLUGIN_NAME}/Export.h
        @ONLY
    )

    # 添加接口头文件，链接目标后可以 include, 无需另外包含头文件目录
    set(PLUGIN_HEADER "${TARGET_NAME}_header")
    add_library(${PLUGIN_HEADER} INTERFACE)
    target_include_directories(${PLUGIN_HEADER}
        INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_BINARY_DIR}/include
    )

    target_link_libraries(${TARGET_NAME} PUBLIC ${PLUGIN_HEADER})

    # DIRECTORY path/to/dir 会安装 dir 目录本身及其内容
    # DIRECTORY path/to/dir/ - 只安装 dir 目录的内容（不包含 dir 本身）
    install(
        DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/${PROJECT_NAME}/${PLUGIN_NAME}/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/${PLUGIN_NAME}
        COMPONENT dev
        PATTERN "detail" EXCLUDE
    )

    install(
        TARGETS ${TARGET_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

    # 安装生成的头文件 Export.h
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/${PROJECT_NAME}/${PLUGIN_NAME}/Export.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/${PLUGIN_NAME}
            COMPONENT dev)
endfunction()
