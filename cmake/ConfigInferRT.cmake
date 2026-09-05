include("${CMAKE_CURRENT_LIST_DIR}/ConfigDependencyDefaults.cmake")

if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

dlt_dependency_resolve_path(
    _dlt_inferrt_root _dlt_inferrt_origin inferrt
    VARIABLES INFERRT_ROOT InferRT_ROOT INFERRT_DIR InferRT_DIR
    ENVIRONMENT_VARIABLES INFERRT_ROOT InferRT_ROOT INFERRT_DIR InferRT_DIR
    PREFIX_PATHS ${CMAKE_PREFIX_PATH}
    REQUIRED_FILES include/inferrt/features/features.h
)
if(_dlt_inferrt_root)
    dlt_dependency_cache_set(
        INFERRT_ROOT "${_dlt_inferrt_root}" PATH
        "InferRT installation root"
        DLT_DEPENDENCY_INFERRT_ROOT "${_dlt_inferrt_origin}")
endif()

if(DEFINED INFERRT_ROOT AND NOT INFERRT_ROOT STREQUAL "")
    if(NOT DEFINED InferRT_DIR OR InferRT_DIR MATCHES "-NOTFOUND$")
        foreach(_candidate IN ITEMS
                "${INFERRT_ROOT}/lib/cmake/InferRT"
                "${INFERRT_ROOT}/lib/cmake"
                "${INFERRT_ROOT}/cmake"
                "${INFERRT_ROOT}/build"
                "${INFERRT_ROOT}")
            if(EXISTS "${_candidate}/InferRTConfig.cmake")
                set(InferRT_DIR "${_candidate}" CACHE PATH "InferRT CMake package directory")
                break()
            endif()
        endforeach()
    endif()
    list(PREPEND CMAKE_PREFIX_PATH "${INFERRT_ROOT}")
endif()

find_package(InferRT CONFIG REQUIRED COMPONENTS features)

if(InferRT_FOUND)
    if(NOT DEFINED INFERRT_ROOT OR INFERRT_ROOT STREQUAL "")
        if(DEFINED InferRT_DIR)
            get_filename_component(_inferred_root "${InferRT_DIR}/../../.." ABSOLUTE)
            set(INFERRT_ROOT "${_inferred_root}" CACHE PATH "InferRT installation root" FORCE)
        endif()
    endif()
    if(DEFINED INFERRT_ROOT)
        set(INFERRT_INCLUDE_DIR "${INFERRT_ROOT}/include" CACHE PATH "InferRT include directory" FORCE)
        set(INFERRT_LIB_DIR "${INFERRT_ROOT}/lib" CACHE PATH "InferRT library directory" FORCE)
        set(INFERRT_BIN_DIR "${INFERRT_ROOT}/bin" CACHE PATH "InferRT runtime directory" FORCE)
        set(INFERRT_INCLUDE_DIR "${INFERRT_ROOT}/include")
        set(INFERRT_LIB_DIR "${INFERRT_ROOT}/lib")
        set(INFERRT_BIN_DIR "${INFERRT_ROOT}/bin")
    endif()
endif()

function(setup_inferrt target)
    set(TARGET_NAME "${PROJECT_NAME}_${target}")

    target_link_libraries(${TARGET_NAME}
        PUBLIC
            InferRT::features
    )
endfunction()
