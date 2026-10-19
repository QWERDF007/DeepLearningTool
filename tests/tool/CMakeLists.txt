set(PLUGIN_NAME "tool")
set(TARGET_NAME "${PROJECT_NAME}_${PLUGIN_NAME}")
set(TEST_TARGET_NAME "tst_${TARGET_NAME}")

dlt_glob_sources(_tool_test_sources
    RECURSE
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
    PATTERNS "*.cpp" "*.h" "*.hpp"
)

list(APPEND _tool_test_sources
    "${CMAKE_SOURCE_DIR}/src/tool/HelpController.cpp"
    "${CMAKE_BINARY_DIR}/src/tool/.rcc/qrc_dltool_raw_qml_0.cpp"
    "${CMAKE_BINARY_DIR}/src/tool/.rcc/qrc_qmake_dltool_tool.cpp"
)

set_source_files_properties(
    "${CMAKE_BINARY_DIR}/src/tool/.rcc/qrc_dltool_raw_qml_0.cpp"
    "${CMAKE_BINARY_DIR}/src/tool/.rcc/qrc_qmake_dltool_tool.cpp"
    PROPERTIES GENERATED TRUE
)

set(_test_path_updates
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_common>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_ui>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:quickui>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_project>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_data>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_feature>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_settings>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_model>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_database>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_parameter>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_core>"
    "PATH=path_list_prepend:${OpenCV_BIN_DIR}"
    "PATH=path_list_prepend:${INFERRT_BIN_DIR}"
    "PATH=path_list_prepend:${INFERRT_CUDA_ROOT}/bin"
    "PATH=path_list_prepend:${Qt6_ROOT}/bin"
    "QML2_IMPORT_PATH=path_list_prepend:${CMAKE_BINARY_DIR}"
    "QML2_IMPORT_PATH=path_list_prepend:${CMAKE_BINARY_DIR}/qml"
)

dlt_add_test_executable(${TEST_TARGET_NAME}
    QT_EXECUTABLE
    SOURCES ${_tool_test_sources}
    LINK_LIBRARIES
        dltool_common
        dltool_ui
        quickui
        dltool_project
        dltool_data
        dltool_feature
        dltool_settings
        dltool_model
        dltool_database
        dltool_parameter
        dltool_core
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::Test
    INCLUDE_DIRECTORIES
        ${CMAKE_SOURCE_DIR}/src/tool
        ${CMAKE_SOURCE_DIR}/src/common/include
        ${CMAKE_SOURCE_DIR}/src/ui/include
        ${CMAKE_SOURCE_DIR}/tests
    COMPILE_DEFINITIONS
        DLTOOL_QML_BUILD_DIR="${CMAKE_BINARY_DIR}"
        DLTOOL_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    LABELS "ordinary;tool"
    ENVIRONMENT_MODIFICATION "${_test_path_updates}"
)

add_dependencies(${TEST_TARGET_NAME} dltool)

unset(_tool_test_sources)
unset(_test_path_updates)
