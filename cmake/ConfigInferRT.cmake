set(INFERRT_ROOT "F:/Projects/InferRT/InferRT-0.0.1" CACHE PATH "InferRT release installation root")
set(INFERRT_DEBUG_ROOT "F:/Projects/InferRT/build" CACHE PATH "InferRT debug build root")
set(INFERRT_INCLUDE_DIR "${INFERRT_ROOT}/include")
set(INFERRT_RELEASE_LIB_DIR "${INFERRT_ROOT}/lib")
set(INFERRT_DEBUG_LIB_DIR "${INFERRT_DEBUG_ROOT}/lib")

# TensorRT headers (required transitively by IModelConfig.hpp)
set(TRT_ROOT "D:/Software/dev/TensorRT-10.16.1.11" CACHE PATH "TensorRT installation directory")

# InferRT model headers include OpenCV core headers.
set(OPENCV_ROOT "D:/Software/dev/opencv-4.8.0/build" CACHE PATH "OpenCV installation root")
set(OPENCV_INCLUDE_DIR "${OPENCV_ROOT}/include" CACHE PATH "OpenCV include directory")

find_package(CUDAToolkit REQUIRED)

function(setup_inferrt target)
    target_include_directories(${target} PRIVATE
        "${INFERRT_INCLUDE_DIR}"
        "${TRT_ROOT}/include"
        "${OPENCV_INCLUDE_DIR}")

    target_link_libraries(${target} PRIVATE CUDA::cudart)

    foreach(INFERRT_MODULE IN ITEMS features model core util cvcuda)
        set(INFERRT_RELEASE_LIB "${INFERRT_RELEASE_LIB_DIR}/inferrt_${INFERRT_MODULE}.lib")
        set(INFERRT_DEBUG_LIB "${INFERRT_DEBUG_LIB_DIR}/inferrt_${INFERRT_MODULE}d.lib")
        if(EXISTS "${INFERRT_DEBUG_LIB}")
            target_link_libraries(${target} PRIVATE
                "$<$<CONFIG:Debug>:${INFERRT_DEBUG_LIB}>"
                "$<$<NOT:$<CONFIG:Debug>>:${INFERRT_RELEASE_LIB}>")
        else()
            message(WARNING
                "InferRT debug library not found: ${INFERRT_DEBUG_LIB}. "
                "Debug builds will use release InferRT libraries.")
            target_link_libraries(${target} PRIVATE "${INFERRT_RELEASE_LIB}")
        endif()
    endforeach()

endfunction()
