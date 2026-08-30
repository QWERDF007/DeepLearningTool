find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Test)

dlt_glob_sources(_model_test_support_sources
    RECURSE
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../model_support"
    PATTERNS "*.cpp" "*.h" "*.hpp"
)
add_library(dltool_model_test_support STATIC ${_model_test_support_sources})
target_link_libraries(dltool_model_test_support
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        ${OpenCV_LIBS}
        dltool_model
        dltool_core
        dltool_common
        dltool_data
        dltool_database
)
target_include_directories(dltool_model_test_support
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../model_support
        ${CMAKE_SOURCE_DIR}/src/model/include
        ${CMAKE_SOURCE_DIR}/src/core/include
        ${CMAKE_SOURCE_DIR}/src/common/include
        ${CMAKE_SOURCE_DIR}/src/data/include
        ${CMAKE_SOURCE_DIR}/src/database/include
        ${OpenCV_INCLUDE_DIRS}
)

set(_model_test_common_libs
    Qt6::Test
    Qt6::Network
    Qt6::Gui
    dltool_model
    dltool_data
    dltool_database
    dltool_settings
    dltool_parameter
    dltool_core
    dltool_common
    dltool_ui
    dltool_model_test_support
)

set(_model_test_include_dirs
    ${CMAKE_SOURCE_DIR}/src/model/include
    ${CMAKE_SOURCE_DIR}/src/core/include
    ${CMAKE_SOURCE_DIR}/src/data/include
    ${CMAKE_SOURCE_DIR}/src/database/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../model_support
)

set(_model_test_path_updates
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
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Gui>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Network>"
    "DLT_RUNTIME_ROOT=set:${CMAKE_SOURCE_DIR}"
    "DLT_TEST_TMP_ROOT=set:F:/tmp"
)

function(add_model_cpp_test TARGET_NAME)
    set(options)
    set(oneValueArgs)
    set(multiValueArgs SOURCES TEST_ARGUMENTS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(ARG_UNPARSED_ARGUMENTS OR NOT ARG_SOURCES)
        message(FATAL_ERROR "add_model_cpp_test requires SOURCES")
    endif()

    set(_test_arguments)
    if(ARG_TEST_ARGUMENTS)
        set(_test_arguments TEST_ARGUMENTS ${ARG_TEST_ARGUMENTS})
    endif()

    dlt_add_test_executable(${TARGET_NAME}
        SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp" ${ARG_SOURCES}
        LINK_LIBRARIES ${_model_test_common_libs}
        INCLUDE_DIRECTORIES ${_model_test_include_dirs}
        LABELS "ordinary;model"
        ENVIRONMENT_MODIFICATION "${_model_test_path_updates}"
        ${_test_arguments}
    )
endfunction()

dlt_glob_sources(_model_all_test_sources
    RECURSE
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
    PATTERNS "test_*.cpp"
)
dlt_glob_sources(_model_dataset_test_sources
    RECURSE
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
    PATTERNS "test_EvaluationDataset*.cpp" "test_EvaluationEngine*.cpp" "test_ModelDataset*.cpp"
)
dlt_glob_sources(_model_behavior_test_sources
    RECURSE
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
    PATTERNS "test_ModelEvaluationParameterBehavior*.cpp"
)
dlt_glob_sources(_model_evaluation_test_sources
    RECURSE
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
    PATTERNS "test_Evaluation*.cpp" "test_AggregateEvaluation*.cpp" "test_ModelEvaluation*.cpp"
)
list(REMOVE_ITEM _model_evaluation_test_sources ${_model_dataset_test_sources} ${_model_behavior_test_sources})
dlt_glob_sources(_model_task_test_sources
    RECURSE
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
    PATTERNS "test_ModelTask*.cpp" "test_ModelTestTask*.cpp" "test_Task*.cpp" "test_ExternalModelTask*.cpp"
)
dlt_glob_sources(_model_storage_test_sources
    RECURSE
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
    PATTERNS "test_ModelStorage*.cpp" "test_ModelParam*.cpp" "test_ModelDataBases*.cpp" "test_ModelManager*.cpp"
)
dlt_glob_sources(_model_config_test_sources
    RECURSE
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
    PATTERNS "test_ModelConfig*.cpp"
)
dlt_glob_sources(_model_registry_test_sources
    RECURSE
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
    PATTERNS "test_Registry*.cpp"
)

set(_model_assigned_test_sources
    ${_model_evaluation_test_sources}
    ${_model_dataset_test_sources}
    ${_model_task_test_sources}
    ${_model_storage_test_sources}
    ${_model_config_test_sources}
    ${_model_registry_test_sources}
    ${_model_behavior_test_sources}
)
list(REMOVE_DUPLICATES _model_assigned_test_sources)
set(_model_unassigned_test_sources ${_model_all_test_sources})
list(REMOVE_ITEM _model_unassigned_test_sources ${_model_assigned_test_sources})
if(_model_unassigned_test_sources)
    string(REPLACE ";" "\n  " _model_unassigned_text "${_model_unassigned_test_sources}")
    message(FATAL_ERROR "Model test sources are not assigned to a CTest target:\n  ${_model_unassigned_text}")
endif()

add_model_cpp_test(dltool_model_evaluation_tests SOURCES ${_model_evaluation_test_sources})
add_model_cpp_test(dltool_model_dataset_tests SOURCES ${_model_dataset_test_sources})
add_model_cpp_test(dltool_model_tasks_tests SOURCES ${_model_task_test_sources})
add_model_cpp_test(dltool_model_evaluation_behavior_tests SOURCES ${_model_behavior_test_sources})

foreach(_behavior_case
        parameterGroupsControlEvaluationTrigger
        anomalyClassificationThresholdIsEvaluationOnly
        anomalyViewModelRecognizesTiffPredictionsWithoutTaskRows
        evaluationParameterWithoutPredictionsDoesNotStartEvaluation
        finishedTestAutomaticallyEvaluatesAndBusyStateRestores)
    dlt_register_test("dltool_model_evaluation_behavior_${_behavior_case}"
        COMMAND dltool_model_evaluation_behavior_tests "${_behavior_case}"
        LABELS "ordinary;model"
        ENVIRONMENT_MODIFICATION "${_model_test_path_updates}"
    )
endforeach()

add_model_cpp_test(dltool_model_storage_params_tests SOURCES ${_model_storage_test_sources})
add_model_cpp_test(dltool_model_config_tests SOURCES ${_model_config_test_sources})
add_model_cpp_test(dltool_model_registry_tests SOURCES ${_model_registry_test_sources})

unset(_model_test_support_sources)
unset(_model_test_common_libs)
unset(_model_test_include_dirs)
unset(_model_test_path_updates)
unset(_model_all_test_sources)
unset(_model_dataset_test_sources)
unset(_model_evaluation_test_sources)
unset(_model_task_test_sources)
unset(_model_storage_test_sources)
unset(_model_config_test_sources)
unset(_model_registry_test_sources)
unset(_model_behavior_test_sources)
unset(_model_assigned_test_sources)
unset(_model_unassigned_test_sources)
unset(_model_unassigned_text)
unset(_behavior_case)
