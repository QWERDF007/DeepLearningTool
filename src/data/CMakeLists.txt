add_plugin_library(data
    PRIVATE_LIBS Qt6::Core Qt6::Gui Qt6::Quick
        dltool_common dltool_ui dltool_settings dltool_database
    INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/3rdparty/nlohmann ${CMAKE_SOURCE_DIR}/3rdparty/spdlog/include
)

setup_inferrt(${PROJECT_NAME}_data)

# target_link_libraries(${PROJECT_NAME}_data PUBLIC
#     "$<$<CONFIG:Debug>:inferrt_modeld>"
#     "$<$<NOT:$<CONFIG:Debug>>:inferrt_model>"
# )
