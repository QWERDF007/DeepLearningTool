set(INFERRT_ROOT "F:/Projects/InferRT/InferRT-0.0.1" CACHE PATH "InferRT release installation root")
set(INFERRT_INCLUDE_DIR "${INFERRT_ROOT}/include")
set(INFERRT_LIB_DIR "${INFERRT_ROOT}/lib")

# TensorRT headers (required transitively by IModelConfig.hpp)
set(TRT_ROOT "D:/Software/dev/TensorRT-10.16.1.11" CACHE PATH "TensorRT installation directory")

function(setup_inferrt target)
    target_include_directories(${target} PRIVATE "${INFERRT_INCLUDE_DIR}" "${TRT_ROOT}/include")
    target_link_directories(${target} PRIVATE "${INFERRT_LIB_DIR}")
    target_link_libraries(${target} PRIVATE
    "$<$<CONFIG:Debug>:inferrt_featuresd>"
    "$<$<NOT:$<CONFIG:Debug>>:inferrt_features>"
    "$<$<CONFIG:Debug>:inferrt_modeld>"
    "$<$<NOT:$<CONFIG:Debug>>:inferrt_model>")
endfunction()


# set(INFERRT_ROOT "F:/Projects/InferRT/InferRT-0.0.1" CACHE PATH "InferRT release installation root")
# set(INFERRT_DEBUG_ROOT "F:/Projects/InferRT/build" CACHE PATH "InferRT debug build root")

# if(EXISTS "${INFERRT_ROOT}/include/inferrt/features/ImageSearch.hpp")
#     target_include_directories(${PROJECT_NAME}_data PRIVATE "${INFERRT_ROOT}/include")

#     set(INFERRT_RELEASE_LIB_DIR "${INFERRT_ROOT}/lib")
#     set(INFERRT_DEBUG_LIB_DIR "${INFERRT_DEBUG_ROOT}/lib")

#     foreach(INFERRT_MODULE IN ITEMS features model core util cvcuda)
#         set(INFERRT_RELEASE_LIB "${INFERRT_RELEASE_LIB_DIR}/inferrt_${INFERRT_MODULE}.lib")
#         set(INFERRT_DEBUG_LIB "${INFERRT_DEBUG_LIB_DIR}/inferrt_${INFERRT_MODULE}d.lib")
#         if(EXISTS "${INFERRT_DEBUG_LIB}")
#             target_link_libraries(${PROJECT_NAME}_data PRIVATE
#                 "$<$<CONFIG:Debug>:${INFERRT_DEBUG_LIB}>"
#                 "$<$<NOT:$<CONFIG:Debug>>:${INFERRT_RELEASE_LIB}>"
#             )
#         else()
#             message(WARNING
#                 "InferRT debug library not found: ${INFERRT_DEBUG_LIB}. "
#                 "Debug builds will use release InferRT libraries, which is unsafe for InferRT APIs that pass STL types.")
#             target_link_libraries(${PROJECT_NAME}_data PRIVATE "${INFERRT_RELEASE_LIB}")
#         endif()
#     endforeach()

#     file(GLOB INFERRT_COMMON_RUNTIME_DLLS
#         "${INFERRT_ROOT}/bin/libiomp5md.dll"
#         "${INFERRT_ROOT}/bin/mkl_*.dll"
#         "${INFERRT_ROOT}/bin/nvinfer_*.dll"
#         "${INFERRT_ROOT}/bin/nvonnxparser_*.dll"
#     )
#     set(INFERRT_RELEASE_RUNTIME_DLLS
#         "${INFERRT_ROOT}/bin/faiss.dll"
#         "${INFERRT_ROOT}/bin/inferrt_core.dll"
#         "${INFERRT_ROOT}/bin/inferrt_cvcuda.dll"
#         "${INFERRT_ROOT}/bin/inferrt_features.dll"
#         "${INFERRT_ROOT}/bin/inferrt_model.dll"
#         "${INFERRT_ROOT}/bin/inferrt_util.dll"
#         "${INFERRT_ROOT}/bin/opencv_world480.dll"
#     )
#     set(INFERRT_DEBUG_RUNTIME_DLLS)
#     if(EXISTS "${INFERRT_DEBUG_ROOT}/bin")
#         set(INFERRT_DEBUG_RUNTIME_DLLS
#             "${INFERRT_DEBUG_ROOT}/bin/faissd.dll"
#             "${INFERRT_DEBUG_ROOT}/bin/inferrt_cored.dll"
#             "${INFERRT_DEBUG_ROOT}/bin/inferrt_cvcudad.dll"
#             "${INFERRT_DEBUG_ROOT}/bin/inferrt_featuresd.dll"
#             "${INFERRT_DEBUG_ROOT}/bin/inferrt_modeld.dll"
#             "${INFERRT_DEBUG_ROOT}/bin/inferrt_utild.dll"
#             "${INFERRT_DEBUG_ROOT}/bin/opencv_world480d.dll"
#         )
#     endif()
#     if(INFERRT_COMMON_RUNTIME_DLLS)
#         add_custom_command(TARGET ${PROJECT_NAME}_data POST_BUILD
#             COMMAND ${CMAKE_COMMAND} -E copy_if_different
#                 ${INFERRT_COMMON_RUNTIME_DLLS}
#                 "$<$<CONFIG:Debug>:${INFERRT_DEBUG_RUNTIME_DLLS}>"
#                 "$<$<NOT:$<CONFIG:Debug>>:${INFERRT_RELEASE_RUNTIME_DLLS}>"
#                 $<TARGET_FILE_DIR:${PROJECT_NAME}_data>
#             COMMAND ${CMAKE_COMMAND} -E copy_if_different
#                 ${INFERRT_COMMON_RUNTIME_DLLS}
#                 "$<$<CONFIG:Debug>:${INFERRT_DEBUG_RUNTIME_DLLS}>"
#                 "$<$<NOT:$<CONFIG:Debug>>:${INFERRT_RELEASE_RUNTIME_DLLS}>"
#                 "${CMAKE_BINARY_DIR}/bin"
#             COMMAND_EXPAND_LISTS
#         )
#     endif()

#     add_custom_command(TARGET ${PROJECT_NAME}_data POST_BUILD
#         COMMAND ${CMAKE_COMMAND}
#             -D "BUILD_CONFIG=$<CONFIG>"
#             -D "DATA_DIR=$<TARGET_FILE_DIR:${PROJECT_NAME}_data>"
#             -D "BIN_DIR=${CMAKE_BINARY_DIR}/bin"
#             -P "${CMAKE_CURRENT_LIST_DIR}/cmake/CleanupInferRtReleaseDlls.cmake"
#     )
# else()
#     message(FATAL_ERROR "InferRT ImageSearch headers not found at ${INFERRT_ROOT}")
# endif()
