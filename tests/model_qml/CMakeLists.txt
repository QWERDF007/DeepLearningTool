find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml QuickTest)

set(TEST_TARGET_NAME "tst_dltool_model_qml")

qt_add_executable(${TEST_TARGET_NAME}
    main.cpp
)

target_link_libraries(${TEST_TARGET_NAME}
    PRIVATE
        Qt6::QuickTest
        dltool_model
        dltool_core
)

target_include_directories(${TEST_TARGET_NAME}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/model/include
        ${CMAKE_SOURCE_DIR}/src/core/include
)

add_test(NAME ${TEST_TARGET_NAME} COMMAND ${TEST_TARGET_NAME})

set(_test_path_updates
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_model>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_parameter>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_common>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_ui>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_settings>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_data>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_database>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:quickui>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "QML2_IMPORT_PATH=path_list_prepend:${CMAKE_BINARY_DIR}"
    "QML2_IMPORT_PATH=path_list_prepend:${CMAKE_BINARY_DIR}/qml"
)
set_tests_properties(${TEST_TARGET_NAME} PROPERTIES
    ENVIRONMENT_MODIFICATION "${_test_path_updates}"
)
unset(_test_path_updates)
