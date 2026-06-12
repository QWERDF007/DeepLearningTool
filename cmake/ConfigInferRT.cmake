set(INFERRT_ROOT "D:/Project/InferRT/InferRT-0.0.1" CACHE PATH "InferRT installation root" FORCE)
set(INFERRT_INCLUDE_DIR "${INFERRT_ROOT}/include")
set(INFERRT_LIB_DIR "${INFERRT_ROOT}/lib")

# TensorRT headers (required transitively by IModelConfig.hpp)
set(TRT_ROOT "D:/Software/dev/TensorRT-10.16.1.11" CACHE PATH "TensorRT installation directory")

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
    set(INFERRT_RELEASE_LIB "${INFERRT_LIB_DIR}/inferrt_${module}.lib")
    set(INFERRT_DEBUG_LIB "${INFERRT_LIB_DIR}/inferrt_${module}d.lib")

    if(EXISTS "${INFERRT_DEBUG_LIB}")
        target_link_libraries(${target} PRIVATE
            debug "${INFERRT_DEBUG_LIB}"
            optimized "${INFERRT_RELEASE_LIB}")
    else()
        message(WARNING
            "InferRT debug library not found: ${INFERRT_DEBUG_LIB}. "
            "Debug builds will use release InferRT libraries.")
        target_link_libraries(${target} PRIVATE "${INFERRT_RELEASE_LIB}")
    endif()
endfunction()

function(setup_inferrt target)
    target_include_directories(${target} PRIVATE
        "${INFERRT_INCLUDE_DIR}"
        "${TRT_ROOT}/include"
        ${OpenCV_INCLUDE_DIRS})

    target_link_libraries(${target} PRIVATE CUDA::cudart ${INFERRT_OPENCV_LIBS})

    foreach(INFERRT_MODULE IN ITEMS features model core util cvcuda)
        link_inferrt_module(${target} ${INFERRT_MODULE})
    endforeach()

endfunction()
