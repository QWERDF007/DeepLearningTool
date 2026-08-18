find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Test)

add_library(dltool_model_test_support STATIC
    ../model_support/TestFixture.cpp
    ../model_support/TestFixture.h
)

target_link_libraries(dltool_model_test_support
    PUBLIC
        Qt6::Core
        Qt6::Gui
        dltool_model
        dltool_data
        dltool_database
)

target_include_directories(dltool_model_test_support
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../model_support
        ${CMAKE_SOURCE_DIR}/src/model/include
        ${CMAKE_SOURCE_DIR}/src/data/include
        ${CMAKE_SOURCE_DIR}/src/database/include
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
    add_executable(${TARGET_NAME}
        main.cpp
        ${ARGN}
    )
    target_link_libraries(${TARGET_NAME} PRIVATE ${_model_test_common_libs})
    target_include_directories(${TARGET_NAME} PRIVATE ${_model_test_include_dirs})
    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
    set_tests_properties(${TARGET_NAME} PROPERTIES
        LABELS "ordinary;model"
        ENVIRONMENT_MODIFICATION "${_model_test_path_updates}"
    )
endfunction()

add_model_cpp_test(dltool_model_evaluation_tests
    test_ModelEvaluationProtocol.cpp
    test_EvaluationRegistries.cpp
    test_EvaluationResult.cpp
    test_EvaluationGeometryMatching.cpp
    test_EvaluationModels.cpp
    test_EvaluationCharts.cpp
    test_EvaluationAnomalyConfusion.cpp
    test_AggregateEvaluation.cpp
    test_ModelEvaluationViewModel.cpp
)

add_model_cpp_test(dltool_model_dataset_tests
    test_EvaluationDataset.cpp
    test_ModelDatasetOrganizer.cpp
    test_EvaluationEngine.cpp
    test_ModelDatasetSelection.cpp
)

add_model_cpp_test(dltool_model_tasks_tests
    test_ModelTaskTypes.cpp
    test_TaskCommunicationProtocol.cpp
    test_TaskCommunicationServer.cpp
    test_TaskManager.cpp
    test_ExternalModelTaskRunner.cpp
    test_ModelTestTaskRepository.cpp
    test_ModelTaskPreparation.cpp
    test_ModelTestTaskManager.cpp
    test_ModelTaskController.cpp
)

add_model_cpp_test(dltool_model_storage_params_tests
    test_ModelStorageService.cpp
    test_ModelStorageMigration.cpp
    test_ModelParamDefs.cpp
    test_ModelParamsSchema.cpp
    test_ModelDataBases.cpp
    test_ModelManager.cpp
)

add_model_cpp_test(dltool_model_registry_tests
    test_RegistryIsolation.cpp
)

set(_model_test_path_updates "")
set(_model_test_include_dirs "")
set(_model_test_common_libs "")
