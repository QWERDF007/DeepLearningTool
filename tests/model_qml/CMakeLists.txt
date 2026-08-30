find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml QuickTest)

set(TEST_TARGET_NAME "tst_dltool_model_qml")

dlt_glob_sources(_qml_test_sources
    RECURSE
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
    PATTERNS "*.cpp" "*.h" "*.hpp"
)
dlt_glob_sources(_qml_test_files
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}/qml"
    PATTERNS "tst_*.qml"
)
source_group("QML Test Files" FILES ${_qml_test_files})

dlt_add_test_executable(${TEST_TARGET_NAME}
    NO_TEST
    QT_EXECUTABLE
    SOURCES ${_qml_test_sources}
    LINK_LIBRARIES
        Qt6::QuickTest
        dltool_model
        dltool_core
        dltool_model_test_support
    INCLUDE_DIRECTORIES
        ${CMAKE_SOURCE_DIR}/src/model/include
        ${CMAKE_SOURCE_DIR}/src/core/include
        ${CMAKE_SOURCE_DIR}/tests/model_support
)

set(_evaluation_refactor_cases
    test_protocolKeysAreTheQmlBoundary
    test_basePropertyAcceptsConcreteViewModelRuntimeExtensions
    test_anomalyThumbnailHeatmapLifecycle
    test_anomalyHeatmapToggleLabelFits
    test_instanceSortSelectorProvidesMethodSpecificOptions
    test_anomalyChartThresholdLineFollowsChartResize
    test_chartTabsUseCompactStableHeightAndPrTooltipValues
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

foreach(_case IN LISTS _evaluation_refactor_cases)
    dlt_register_test("${TEST_TARGET_NAME}_${_case}"
        COMMAND ${TEST_TARGET_NAME} "EvaluationRefactor::${_case}"
                -o "${CMAKE_CURRENT_BINARY_DIR}/${_case}.txt,txt"
        LABELS "ordinary;qml"
        ENVIRONMENT_MODIFICATION "${_test_path_updates}"
    )
endforeach()

dlt_register_test(${TEST_TARGET_NAME}_registry
    COMMAND ${TEST_TARGET_NAME}
        EvaluationPanelRegistry::test_detectionMapping
        EvaluationPanelRegistry::test_anomalyMapping
        EvaluationPanelRegistry::test_segmentationMapping
        -o -,txt
    LABELS "ordinary;qml"
    ENVIRONMENT_MODIFICATION "${_test_path_updates}"
)
dlt_register_test(${TEST_TARGET_NAME}_smoke
    COMMAND ${TEST_TARGET_NAME}
        EvaluationPanelSmoke::test_componentsLoad
        -o -,txt
    LABELS "ordinary;qml"
    ENVIRONMENT_MODIFICATION "${_test_path_updates}"
)

unset(_qml_test_sources)
unset(_qml_test_files)
unset(_evaluation_refactor_cases)
unset(_test_path_updates)
