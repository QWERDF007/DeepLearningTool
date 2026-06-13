set(PLUGIN_NAME "ui")
set(TARGET_NAME "${PROJECT_NAME}_${PLUGIN_NAME}")
set(TEST_TARGET_NAME "tst_${TARGET_NAME}")

file(GLOB_RECURSE SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.cpp)
file(GLOB_RECURSE HEADERS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.h *.hpp)

qt_add_executable(${TEST_TARGET_NAME}
    ${SOURCES}
    ${HEADERS}
)

add_test(NAME ${TEST_TARGET_NAME} COMMAND ${TEST_TARGET_NAME})
set(_test_path_updates
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_common>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_ui>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:quickui>"
    "PATH=path_list_prepend:${Qt6_ROOT}/bin"
)
set_tests_properties(${TEST_TARGET_NAME} PROPERTIES
    ENVIRONMENT_MODIFICATION "${_test_path_updates}"
)
unset(_test_path_updates)

target_link_libraries(${TEST_TARGET_NAME} PRIVATE
    dltool_ui
    Qt6::Core Qt6::Test
)

target_include_directories(${TEST_TARGET_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/tests)
