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

set(_evaluation_refactor_cases
    test_protocolKeysAreTheQmlBoundary
    test_basePropertyAcceptsConcreteViewModelRuntimeExtensions
    test_anomalyThumbnailHeatmapLifecycle
    test_anomalyHeatmapToggleLabelFits
    test_anomalyChartThresholdLineFollowsChartResize
    test_chartTabSurvivesSameEvaluationRefresh
    test_anomalyMatrixClickSetsFnAndFpFilters
    test_anomalyMatrixFilterFirstHeatmapPaintsPolygons
    test_everyRegisteredPanelCanInstantiate
    test_baseContentInjectionAndMethodChartHook
    test_testEvaluationPanelReadyStatesAndRuntimeStateOverlay
    test_detectionMatrixDelegatePropertiesAndClickFilters
    test_modelBusyLocksTestControlsAndRestoresEditing
    test_runningModelLeavesOnlyStopActionAvailable
    test_trainingBusyStateDisablesParameterAndDatasetEditors
)
foreach(_case IN LISTS _evaluation_refactor_cases)
    add_test(NAME "${TEST_TARGET_NAME}_${_case}"
        COMMAND ${TEST_TARGET_NAME} "EvaluationRefactor::${_case}"
                -o "${CMAKE_CURRENT_BINARY_DIR}/${_case}.txt,txt")
endforeach()
add_test(NAME ${TEST_TARGET_NAME}_registry
    COMMAND ${TEST_TARGET_NAME}
        EvaluationPanelRegistry::test_detectionMapping
        EvaluationPanelRegistry::test_anomalyMapping
        EvaluationPanelRegistry::test_segmentationMapping
        -o -,txt
)
add_test(NAME ${TEST_TARGET_NAME}_smoke
    COMMAND ${TEST_TARGET_NAME}
        EvaluationPanelSmoke::test_componentsLoad
        -o -,txt
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
    ${TEST_TARGET_NAME}_registry
    ${TEST_TARGET_NAME}_smoke
    ${TEST_TARGET_NAME}_test_protocolKeysAreTheQmlBoundary
    ${TEST_TARGET_NAME}_test_basePropertyAcceptsConcreteViewModelRuntimeExtensions
    ${TEST_TARGET_NAME}_test_anomalyThumbnailHeatmapLifecycle
    ${TEST_TARGET_NAME}_test_anomalyHeatmapToggleLabelFits
    ${TEST_TARGET_NAME}_test_anomalyChartThresholdLineFollowsChartResize
    ${TEST_TARGET_NAME}_test_chartTabSurvivesSameEvaluationRefresh
    ${TEST_TARGET_NAME}_test_anomalyMatrixClickSetsFnAndFpFilters
    ${TEST_TARGET_NAME}_test_anomalyMatrixFilterFirstHeatmapPaintsPolygons
    ${TEST_TARGET_NAME}_test_everyRegisteredPanelCanInstantiate
    ${TEST_TARGET_NAME}_test_baseContentInjectionAndMethodChartHook
    ${TEST_TARGET_NAME}_test_testEvaluationPanelReadyStatesAndRuntimeStateOverlay
    ${TEST_TARGET_NAME}_test_detectionMatrixDelegatePropertiesAndClickFilters
    ${TEST_TARGET_NAME}_test_modelBusyLocksTestControlsAndRestoresEditing
    ${TEST_TARGET_NAME}_test_runningModelLeavesOnlyStopActionAvailable
    ${TEST_TARGET_NAME}_test_trainingBusyStateDisablesParameterAndDatasetEditors
    PROPERTIES
    LABELS "ordinary;qml"
    ENVIRONMENT_MODIFICATION "${_test_path_updates}"
)
unset(_test_path_updates)
