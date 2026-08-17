find_package(Qt6 REQUIRED COMPONENTS Core Test)

add_executable(dltool_model_evaluation_tests
    main.cpp
    test_ModelEvaluationProtocol.cpp
    test_EvaluationRegistries.cpp
    test_EvaluationResult.cpp
    test_ModelTaskTypes.cpp
    test_TaskCommunicationProtocol.cpp
    test_EvaluationGeometryMatching.cpp
    test_ModelStorageService.cpp
    test_ModelParamDefs.cpp
    test_EvaluationDataset.cpp
)

target_link_libraries(dltool_model_evaluation_tests
    PRIVATE
        Qt6::Test
        dltool_model
        dltool_core
)

target_include_directories(dltool_model_evaluation_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/model/include
        ${CMAKE_SOURCE_DIR}/src/core/include
)

add_test(NAME model_evaluation_tests COMMAND dltool_model_evaluation_tests)

set_tests_properties(model_evaluation_tests PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_model>\;$<TARGET_FILE_DIR:dltool_core>\;$<TARGET_FILE_DIR:dltool_parameter>\;$<TARGET_FILE_DIR:dltool_common>\;$<TARGET_FILE_DIR:dltool_ui>\;$<TARGET_FILE_DIR:dltool_settings>\;$<TARGET_FILE_DIR:dltool_data>\;$<TARGET_FILE_DIR:dltool_database>\;$<TARGET_FILE_DIR:quickui>\;$<TARGET_FILE_DIR:Qt6::Core>"
)
