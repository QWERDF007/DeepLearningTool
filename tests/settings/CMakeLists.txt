find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Test)

set(_settings_test_environment
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_feature>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_model>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_data>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_database>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_settings>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_parameter>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_common>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_ui>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:quickui>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Gui>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Qml>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Quick>"
    "PATH=path_list_prepend:${INFERRT_BIN_DIR}"
    "PATH=path_list_prepend:${OpenCV_BIN_DIR}"
    "PATH=path_list_prepend:${CUDAToolkit_BIN_DIR}"
    "PATH=path_list_prepend:${TRT_ROOT}/bin"
    "PATH=path_list_prepend:${ONNXRUNTIME_ROOT}/lib"
    "PATH=path_list_prepend:F:/Github/faiss/install/bin"
    "PATH=path_list_prepend:D:/Software/dev/mkl/mkl/bin"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_common>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_parameter>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_settings>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_database>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_data>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_model>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_ui>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_feature>"
    "DLT_RUNTIME_ROOT=set:${CMAKE_BINARY_DIR}/bin"
)

dlt_add_test_executable(dltool_settings_save_behavior_tests
    QT_EXECUTABLE
    SOURCES test_SettingsSaveBehavior.cpp
    LINK_LIBRARIES
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::Test
        dltool_settings
        dltool_database
        dltool_parameter
        dltool_feature
        dltool_core
        dltool_common
        dltool_ui
        quickui
    INCLUDE_DIRECTORIES
        ${CMAKE_SOURCE_DIR}/src/settings/include
        ${CMAKE_SOURCE_DIR}/src/database/include
        ${CMAKE_SOURCE_DIR}/src/parameter/include
        ${CMAKE_SOURCE_DIR}/src/feature/include
        ${CMAKE_SOURCE_DIR}/src/core/include
        ${CMAKE_SOURCE_DIR}/src/common/include
        ${CMAKE_SOURCE_DIR}/src/ui/include
    LABELS "ordinary;settings"
    ENVIRONMENT_MODIFICATION "${_settings_test_environment}"
)

unset(_settings_test_environment)
