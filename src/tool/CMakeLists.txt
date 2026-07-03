set(PLUGIN_NAME "tool")

# 获取所有QML文件的相对路径
file(GLOB_RECURSE QML_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.qml)
# 将 qml 文件组织到一起
source_group("QML Files" FILES ${QML_SOURCES})

# 获取所有CPP文件的相对路径
file(GLOB_RECURSE SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.cpp)

# 获取所有头文件的相对路径
file(GLOB_RECURSE HEADERS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.h *.hpp)

# 获取所有资源文件的相对路径
file(GLOB_RECURSE RES_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} */qmldir)
# 将 qtquickcontrols2.conf 添加到资源文件列表
list(APPEND RES_FILES qtquickcontrols2.conf)
# 将 qmldir 组织到一起
source_group("Resource Files" FILES ${RES_FILES})

qt_add_big_resources(QRC_SOURCES ${PROJECT_SOURCE_DIR}/assets/assets.qrc)
# Have big_resource.qrc treated as a source file by Qt Creator
set_property(SOURCE ${PROJECT_SOURCE_DIR}/assets/assets.qrc PROPERTY SKIP_AUTORCC ON)


# 指定可执行文件和库文件的输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# MSVC 去掉 Debug Release 文件夹的嵌套  
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})


# 添加可执行文件
qt_add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
    ${QRC_SOURCES}
)

set(QML_PLUGIN_DIRECTORY "${CMAKE_BINARY_DIR}/${PROJECT_NAME}/${PLUGIN_NAME}")

# 添加QML模块
qt_add_qml_module(${PROJECT_NAME}
    VERSION 1.0
    OUTPUT_DIRECTORY ${QML_PLUGIN_DIRECTORY}
    URI "${PROJECT_NAME}.${PLUGIN_NAME}"
    QML_FILES ${QML_SOURCES}
    RESOURCES ${RES_FILES}
)

# 添加资源文件
qt_add_resources(${PROJECT_NAME} "controls2_conf"
    PREFIX "/"
    FILES "qtquickcontrols2.conf"
)


# 如果编译模式是Debug或RelWithDebInfo，则添加预处理定义QT_QML_DEBUG。用于开启Qt Quick调试功能
target_compile_definitions(${PROJECT_NAME}
PRIVATE 
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:QT_QML_DEBUG>
    DLTOOL_QML_BUILD_DIR="${CMAKE_BINARY_DIR}"
)

# 链接库
target_link_libraries(${PROJECT_NAME}
PRIVATE 
    Qt6::Quick Qt6::Gui Qt6::Widgets
    quickui dltool_common dltool_core dltool_ui dltool_project dltool_data dltool_feature dltool_model dltool_settings
)
