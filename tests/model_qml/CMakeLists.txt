find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml QuickTest)

set(TEST_TARGET_NAME "tst_dltool_model_qml")

qt_add_executable(${TEST_TARGET_NAME}
    main.cpp
    QmlModelFixture.cpp
    QmlModelFixture.h
)

target_link_libraries(${TEST_TARGET_NAME}
    PRIVATE
        Qt6::QuickTest
        dltool_model
        dltool_core
        dltool_model_test_support
)

target_include_directories(${TEST_TARGET_NAME}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/model/include
        ${CMAKE_SOURCE_DIR}/src/core/include
        ${CMAKE_SOURCE_DIR}/tests/model_support
)

# Keep QML component groups in separate processes.  Qt Quick render objects
# created by the smoke components can outlive a test function on offscreen
# backends, so sharing the process with the fixture-heavy refactor tests is
# not reliable.
add_test(NAME ${TEST_TARGET_NAME}
    COMMAND ${TEST_TARGET_NAME}
        EvaluationRefactor::test_protocolKeysAreTheQmlBoundary
        EvaluationRefactor::test_basePropertyAcceptsConcreteViewModelRuntimeExtensions
        EvaluationRefactor::test_anomalyMatrixClickSetsFnAndFpFilters
        EvaluationRefactor::test_everyRegisteredPanelCanInstantiate
        EvaluationRefactor::test_baseContentInjectionAndMethodChartHook
        EvaluationRefactor::test_testEvaluationPanelReadyStatesAndRuntimeStateOverlay
        EvaluationRefactor::test_detectionMatrixDelegatePropertiesAndClickFilters
)
add_test(NAME ${TEST_TARGET_NAME}_registry
    COMMAND ${TEST_TARGET_NAME}
        EvaluationPanelRegistry::test_detectionMapping
        EvaluationPanelRegistry::test_anomalyMapping
        EvaluationPanelRegistry::test_segmentationMapping
)
add_test(NAME ${TEST_TARGET_NAME}_smoke
    COMMAND ${TEST_TARGET_NAME}
        EvaluationPanelSmoke::test_componentsLoad
)

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
    "DLT_RUNTIME_ROOT=set:${CMAKE_SOURCE_DIR}"
    "QML2_IMPORT_PATH=path_list_prepend:${CMAKE_BINARY_DIR}"
    "QML2_IMPORT_PATH=path_list_prepend:${CMAKE_BINARY_DIR}/qml"
    "DLT_TEST_TMP_ROOT=set:F:/tmp"
)
set_tests_properties(
    ${TEST_TARGET_NAME}
    ${TEST_TARGET_NAME}_registry
    ${TEST_TARGET_NAME}_smoke
    PROPERTIES
    LABELS "ordinary;qml"
    ENVIRONMENT_MODIFICATION "${_test_path_updates}"
)
unset(_test_path_updates)
