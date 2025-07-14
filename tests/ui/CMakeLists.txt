set(PLUGIN_NAME "ui")
set(TARGET_NAME "${PROJECT_NAME}_${PLUGIN_NAME}")
set(TEST_TARGET_NAME "tst_${TARGET_NAME}")

# 获取所有源文件的相对路径
file(GLOB_RECURSE SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.cpp)

# 获取所有头文件的相对路径
file(GLOB_RECURSE HEADERS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.h *.hpp)

# 获取所有QML文件的相对路径
file(GLOB_RECURSE QML_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.qml)
# 将 qml 文件组织到一起
source_group("QML Files" FILES ${QML_SOURCES})

qt_add_executable(${TEST_TARGET_NAME}
    ${SOURCES}
    ${HEADERS}
    ${QML_SOURCES}
)

add_test(NAME ${TEST_TARGET_NAME} COMMAND ${TEST_TARGET_NAME})

target_link_libraries(${TEST_TARGET_NAME} PRIVATE 
    dltool_ui dltool_ui_header
    Qt6::Quick Qt6::QuickTest
)

target_include_directories(${TEST_TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../)

target_compile_definitions(${TEST_TARGET_NAME} PRIVATE TEST_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

