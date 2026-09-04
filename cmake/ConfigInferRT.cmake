set(INFERRT_ROOT "F:/Projects/InferRT/InferRT-0.0.3" CACHE PATH "InferRT installation root" FORCE)
set(INFERRT_INCLUDE_DIR "${INFERRT_ROOT}/include" CACHE PATH "InferRT include directory" FORCE)
set(INFERRT_LIB_DIR "${INFERRT_ROOT}/lib" CACHE PATH "InferRT library directory" FORCE)
set(INFERRT_BIN_DIR "${INFERRT_ROOT}/bin" CACHE PATH "InferRT runtime directory" FORCE)
set(InferRT_DIR "${INFERRT_ROOT}/lib/cmake/InferRT" CACHE PATH "InferRT CMake package directory" FORCE)

find_package(InferRT CONFIG REQUIRED COMPONENTS features)

function(setup_inferrt target)
    set(TARGET_NAME "${PROJECT_NAME}_${target}")

    target_link_libraries(${TARGET_NAME}
        PUBLIC
            InferRT::features
    )
endfunction()
