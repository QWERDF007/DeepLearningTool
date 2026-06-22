set(INFERRT_ROOT "/home/pc/workspace/InferRT/InferRT-0.0.1" CACHE PATH "InferRT installation root" FORCE)
set(INFERRT_INCLUDE_DIR "${INFERRT_ROOT}/include" CACHE PATH "InferRT include directory" FORCE)
set(INFERRT_LIB_DIR "${INFERRT_ROOT}/lib" CACHE PATH "InferRT library directory" FORCE)
set(INFERRT_BIN_DIR "${INFERRT_ROOT}/bin" CACHE PATH "InferRT runtime directory" FORCE)

# TensorRT headers (required transitively by IModelConfig.hpp)
set(TRT_ROOT "/home/pc/workspace/TensorRT-10.16.1.11" CACHE PATH "TensorRT installation directory" FORCE)

if(NOT OpenCV_FOUND)
    include(ConfigOpenCV)
endif()
set(INFERRT_OPENCV_LIBS ${OpenCV_LIBS})
if(TARGET opencv_world)
    set(INFERRT_OPENCV_LIBS opencv_world)
endif()

if(NOT TARGET CUDA::cudart)
    include(ConfigCUDA)
endif()

function(link_inferrt_module target module)
    find_library(INFERRT_RELEASE_LIB_${module}
        NAMES inferrt_${module}
        HINTS "${INFERRT_LIB_DIR}"
        NO_DEFAULT_PATH)
    find_library(INFERRT_DEBUG_LIB_${module}
        NAMES inferrt_${module}d
        HINTS "${INFERRT_LIB_DIR}"
        NO_DEFAULT_PATH)

    if(NOT INFERRT_RELEASE_LIB_${module})
        message(FATAL_ERROR
            "InferRT ${module} release library not found in ${INFERRT_LIB_DIR}. "
            "Please build and install inferrt_${module} first.")
    endif()

    if(INFERRT_DEBUG_LIB_${module})
        target_link_libraries(${target} PRIVATE
            debug "${INFERRT_DEBUG_LIB_${module}}"
            optimized "${INFERRT_RELEASE_LIB_${module}}")
    else()
        message(WARNING
            "InferRT debug library not found for module ${module}. "
            "Debug builds will use release InferRT libraries.")
        target_link_libraries(${target} PRIVATE "${INFERRT_RELEASE_LIB_${module}}")
    endif()
endfunction()

function(setup_inferrt target)
    set(TARGET_NAME "${PROJECT_NAME}_${target}")
    target_include_directories(${TARGET_NAME} PRIVATE
        "${INFERRT_INCLUDE_DIR}"
        "${TRT_ROOT}/include"
        ${OpenCV_INCLUDE_DIRS})

    target_link_libraries(${TARGET_NAME} PRIVATE CUDA::cudart ${INFERRT_OPENCV_LIBS})

    foreach(INFERRT_MODULE IN ITEMS features model core util cvcuda)
        link_inferrt_module(${TARGET_NAME} ${INFERRT_MODULE})
    endforeach()

endfunction()
