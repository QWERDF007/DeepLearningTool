find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Qml Quick Test)

set(_project_common_libs
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::Test
    dltool_project
    dltool_feature
    dltool_model
    dltool_data
    dltool_database
    dltool_settings
    dltool_parameter
    dltool_core
    dltool_common
    dltool_ui
)

set(_project_include_dirs
    ${CMAKE_SOURCE_DIR}/src/project/include
    ${CMAKE_SOURCE_DIR}/src/feature/include
    ${CMAKE_SOURCE_DIR}/src/model/include
    ${CMAKE_SOURCE_DIR}/src/data/include
    ${CMAKE_SOURCE_DIR}/src/database/include
    ${CMAKE_SOURCE_DIR}/src/settings/include
    ${CMAKE_SOURCE_DIR}/src/parameter/include
    ${CMAKE_SOURCE_DIR}/src/core/include
    ${CMAKE_SOURCE_DIR}/src/common/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set(_project_path_updates
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:dltool_project>"
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
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Network>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Qml>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Quick>"
    "DLT_RUNTIME_ROOT=set:${CMAKE_BINARY_DIR}/bin"
    "DLT_TEST_ASSET_ROOT=set:${CMAKE_SOURCE_DIR}/tests/assets/model"
)

add_library(dltool_project_test_support STATIC
    PersistentProjectFixture.cpp
    PersistentProjectFixture.h
)
target_link_libraries(dltool_project_test_support PUBLIC ${_project_common_libs})
target_include_directories(dltool_project_test_support PUBLIC ${_project_include_dirs})
target_compile_definitions(dltool_project_test_support PUBLIC DLT_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")

function(add_project_test TARGET_NAME SOURCE_FILE CATEGORY LAYER)
    add_executable(${TARGET_NAME} ${SOURCE_FILE})
    target_link_libraries(${TARGET_NAME} PRIVATE dltool_project_test_support)
    target_include_directories(${TARGET_NAME} PRIVATE ${_project_include_dirs})
    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
    set_tests_properties(${TARGET_NAME} PROPERTIES
        LABELS "project;${CATEGORY};${LAYER}"
        ENVIRONMENT_MODIFICATION "${_project_path_updates}"
    )
endfunction()

add_project_test(dltool_model_project_creation_test test_ProjectCreation.cpp project-setup project-creation)
add_project_test(dltool_model_python_environment_test test_PythonEnvironment.cpp project-setup project-python)
add_project_test(dltool_model_data_creation_test test_DataCreation.cpp project-data project-data-creation)
add_project_test(dltool_model_data_import_test test_DataImport.cpp project-data project-data-import)
add_project_test(dltool_model_data_export_test test_DataExport.cpp project-data project-data-export)
add_project_test(dltool_model_data_roundtrip_test test_DataFormatRoundtrip.cpp project-data project-data-roundtrip)
add_project_test(dltool_model_patchcore_model_test test_PatchcoreModel.cpp project-model project-model-creation)
add_project_test(dltool_model_patchcore_copy_test test_PatchcoreCopy.cpp project-model project-model-copy)
add_project_test(dltool_model_patchcore_rename_test test_PatchcoreRename.cpp project-model project-model-rename)
add_project_test(dltool_model_patchcore_delete_test test_PatchcoreDelete.cpp project-model project-model-delete)
add_project_test(dltool_model_patchcore_train_test test_PatchcoreTrain.cpp project-model project-model-train)
add_project_test(dltool_model_patchcore_predict_test test_PatchcorePredict.cpp project-model project-model-predict)
add_project_test(dltool_model_patchcore_evaluation_test test_PatchcoreEvaluation.cpp project-model project-model-evaluation)

set_tests_properties(dltool_model_project_creation_test PROPERTIES
    FIXTURES_SETUP dltool_model_project
)
set_tests_properties(dltool_model_data_import_test PROPERTIES
    FIXTURES_REQUIRED dltool_model_data_creation
    FIXTURES_SETUP dltool_model_data_import
)
set_tests_properties(dltool_model_data_creation_test PROPERTIES
    FIXTURES_REQUIRED dltool_model_project
    FIXTURES_SETUP dltool_model_data_creation
)
set_tests_properties(dltool_model_data_export_test PROPERTIES
    FIXTURES_REQUIRED dltool_model_data_import
    FIXTURES_SETUP dltool_model_data_export
)
set_tests_properties(dltool_model_data_roundtrip_test PROPERTIES
    FIXTURES_REQUIRED dltool_model_data_export
)
set_tests_properties(dltool_model_patchcore_model_test PROPERTIES
    FIXTURES_REQUIRED dltool_model_data_import
    FIXTURES_SETUP dltool_model_patchcore_model
)
set_tests_properties(dltool_model_patchcore_copy_test PROPERTIES
    FIXTURES_REQUIRED dltool_model_patchcore_model
)
set_tests_properties(dltool_model_patchcore_rename_test PROPERTIES
    FIXTURES_REQUIRED dltool_model_patchcore_model
)
set_tests_properties(dltool_model_patchcore_delete_test PROPERTIES
    FIXTURES_REQUIRED dltool_model_patchcore_model
)
set_tests_properties(dltool_model_patchcore_train_test PROPERTIES
    FIXTURES_REQUIRED dltool_model_patchcore_model
    FIXTURES_SETUP dltool_model_patchcore_train
)
set_tests_properties(dltool_model_patchcore_predict_test PROPERTIES
    FIXTURES_REQUIRED dltool_model_patchcore_train
    FIXTURES_SETUP dltool_model_patchcore_predict
)
set_tests_properties(dltool_model_patchcore_evaluation_test PROPERTIES
    FIXTURES_REQUIRED dltool_model_patchcore_predict
)

set(_project_common_libs "")
set(_project_include_dirs "")
set(_project_path_updates "")
