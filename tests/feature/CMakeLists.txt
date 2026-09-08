find_package(Qt6 REQUIRED COMPONENTS Core Gui Test)

set(_feature_test_environment
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
    # CTest applies ENVIRONMENT_MODIFICATION entries in order.  The module
    # directories are repeated after the shared runtime root so every current
    # build-tree DLL wins over a stale copy in build/bin.
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

dlt_add_test_executable(dltool_feature_lifecycle_tests
    QT_EXECUTABLE
    SOURCES test_FeatureLifecycle.cpp
    LINK_LIBRARIES
        Qt6::Core
        Qt6::Gui
        Qt6::Test
        dltool_feature
        dltool_model
        dltool_data
        dltool_database
        dltool_settings
        dltool_parameter
        dltool_core
        dltool_common
        dltool_ui
    INCLUDE_DIRECTORIES
        ${CMAKE_SOURCE_DIR}/src/feature/include
        ${CMAKE_SOURCE_DIR}/src/model/include
        ${CMAKE_SOURCE_DIR}/src/data/include
        ${CMAKE_SOURCE_DIR}/src/database/include
        ${CMAKE_SOURCE_DIR}/src/settings/include
        ${CMAKE_SOURCE_DIR}/src/parameter/include
        ${CMAKE_SOURCE_DIR}/src/core/include
        ${CMAKE_SOURCE_DIR}/src/common/include
        ${CMAKE_SOURCE_DIR}/src/ui/include
    LABELS "ordinary;feature"
    ENVIRONMENT_MODIFICATION "${_feature_test_environment}"
)

unset(_feature_test_environment)
