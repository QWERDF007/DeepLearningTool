find_package(Qt6 REQUIRED COMPONENTS Core Gui Test)

dlt_add_test_executable(dltool_common_geometry_tests
    SOURCES test_GeometryKernel.cpp
    LINK_LIBRARIES
        Qt6::Core
        Qt6::Gui
        Qt6::Test
        dltool_common
    INCLUDE_DIRECTORIES
        ${CMAKE_SOURCE_DIR}/src/common/include
    LABELS "ordinary;common"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_common>"
        "PATH=path_list_prepend:${OpenCV_BIN_DIR}"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Gui>"
)
