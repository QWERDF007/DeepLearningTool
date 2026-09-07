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

dlt_glob_sources(_project_fixture_sources
    RECURSE
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
    PATTERNS "*Fixture.cpp" "*Fixture.h" "*Fixture.hpp"
)
add_library(dltool_project_test_support STATIC ${_project_fixture_sources})
target_link_libraries(dltool_project_test_support PUBLIC ${_project_common_libs})
target_include_directories(dltool_project_test_support PUBLIC ${_project_include_dirs})
target_compile_definitions(dltool_project_test_support PUBLIC DLT_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")

function(project_test_metadata TEST_SOURCE OUTPUT_TARGET OUTPUT_CATEGORY OUTPUT_LAYER OUTPUT_REQUIRED OUTPUT_SETUP)
    set(_category)
    set(_layer)
    set(_required)
    set(_setup)

    get_filename_component(_test_key "${TEST_SOURCE}" NAME_WE)
    string(REGEX REPLACE "^test_" "" _test_key "${_test_key}")
    string(REGEX REPLACE "([a-z0-9])([A-Z])" "\\1_\\2" _target_suffix "${_test_key}")
    string(TOLOWER "${_target_suffix}" _target_suffix)
    string(REGEX REPLACE "^data_format_" "data_" _target_suffix "${_target_suffix}")

    if(_test_key STREQUAL "ProjectCreation")
        set(_category project-setup)
        set(_layer project-creation)
        set(_setup dltool_model_project)
    elseif(_test_key STREQUAL "PythonEnvironment")
        set(_category project-setup)
        set(_layer project-python)
    elseif(_test_key MATCHES "^Data")
        set(_category project-data)
        string(REGEX REPLACE "^data_" "" _data_operation "${_target_suffix}")
        string(REPLACE "_" "-" _data_operation "${_data_operation}")
        set(_layer "project-data-${_data_operation}")
        if(_test_key STREQUAL "DataCreation")
            set(_required dltool_model_project)
            set(_setup dltool_model_data_creation)
        elseif(_test_key STREQUAL "DataImport")
            set(_required dltool_model_data_creation)
            set(_setup dltool_model_data_import)
        elseif(_test_key STREQUAL "DataExport")
            set(_required dltool_model_data_import)
            set(_setup dltool_model_data_export)
        elseif(_test_key STREQUAL "DataSplit")
            set(_required dltool_model_data_import)
            set(_setup dltool_model_data_split)
        elseif(_test_key STREQUAL "DataFormatRoundtrip")
            set(_required dltool_model_data_export)
        endif()
    elseif(_test_key MATCHES "^Patchcore")
        set(_category project-model)
        string(REGEX REPLACE "^patchcore_" "" _operation "${_target_suffix}")
        if(_operation STREQUAL "model")
            set(_layer project-model-creation)
            set(_required dltool_model_data_import)
            set(_setup dltool_model_patchcore_model)
        else()
            set(_layer "project-model-${_operation}")
            set(_required dltool_model_patchcore_model)
            if(_operation STREQUAL "train")
                set(_setup dltool_model_patchcore_train)
            elseif(_operation STREQUAL "predict")
                set(_required dltool_model_patchcore_train)
                set(_setup dltool_model_patchcore_predict)
            elseif(_operation STREQUAL "evaluation")
                set(_required dltool_model_patchcore_predict)
            endif()
        endif()
    else()
        message(FATAL_ERROR "Cannot infer project test metadata from ${TEST_SOURCE}")
    endif()

    set(${OUTPUT_TARGET} "dltool_model_${_target_suffix}_test" PARENT_SCOPE)
    set(${OUTPUT_CATEGORY} "${_category}" PARENT_SCOPE)
    set(${OUTPUT_LAYER} "${_layer}" PARENT_SCOPE)
    set(${OUTPUT_REQUIRED} "${_required}" PARENT_SCOPE)
    set(${OUTPUT_SETUP} "${_setup}" PARENT_SCOPE)
endfunction()

dlt_glob_sources(_project_test_sources
    RECURSE
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
    PATTERNS "test_*.cpp"
)
foreach(_project_test_source IN LISTS _project_test_sources)
    project_test_metadata("${_project_test_source}" _target _category _layer _required _setup)
    dlt_add_test_executable(${_target}
        SOURCES "${_project_test_source}"
        LINK_LIBRARIES dltool_project_test_support
        INCLUDE_DIRECTORIES ${_project_include_dirs}
        LABELS "project;${_category};${_layer}"
        ENVIRONMENT_MODIFICATION "${_project_path_updates}"
        FIXTURES_REQUIRED "${_required}"
        FIXTURES_SETUP "${_setup}"
    )
endforeach()

unset(_project_common_libs)
unset(_project_include_dirs)
unset(_project_path_updates)
unset(_project_fixture_sources)
unset(_project_test_sources)
unset(_project_test_source)
unset(_target)
unset(_category)
unset(_layer)
unset(_required)
unset(_setup)
