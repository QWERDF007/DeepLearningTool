set(PLUGIN_NAME settings)

add_plugin_library(${PLUGIN_NAME}
    QML_URI "dltool.settings"
    PUBLIC_LIBS dltool_parameter
    PRIVATE_LIBS Qt6::Core Qt6::Qml Qt6::Quick quickui dltool_common dltool_database dltool_ui yaml-cpp::yaml-cpp
)

add_executable(dltool_settings_keygen
    ${CMAKE_SOURCE_DIR}/tools/settings_keygen.cpp
)
set_target_properties(dltool_settings_keygen PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_link_libraries(dltool_settings_keygen
    PRIVATE yaml-cpp::yaml-cpp
)
if(MSVC)
    target_compile_options(dltool_settings_keygen PRIVATE /wd4067)
endif()

file(GLOB SETTINGS_SCHEMA_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/config/settings/*.yaml"
)

set(SETTINGS_GENERATED_INCLUDE_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/include/${PROJECT_NAME}/${PLUGIN_NAME}"
)
set(SETTINGS_GENERATED_KEYS_HEADER
    "${SETTINGS_GENERATED_INCLUDE_DIR}/SettingsKeys.hpp"
)

add_custom_command(
    OUTPUT "${SETTINGS_GENERATED_KEYS_HEADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SETTINGS_GENERATED_INCLUDE_DIR}"
    COMMAND $<TARGET_FILE:dltool_settings_keygen>
            --input-dir "${CMAKE_SOURCE_DIR}/config/settings"
            --output "${SETTINGS_GENERATED_KEYS_HEADER}"
    DEPENDS dltool_settings_keygen ${SETTINGS_SCHEMA_FILES}
    COMMENT "Generating settings keys header"
    VERBATIM
)

add_custom_target(dltool_settings_generated_keys
    DEPENDS "${SETTINGS_GENERATED_KEYS_HEADER}"
)

add_dependencies(dltool_settings dltool_settings_generated_keys)
target_sources(dltool_settings PRIVATE "${SETTINGS_GENERATED_KEYS_HEADER}")
set_source_files_properties("${SETTINGS_GENERATED_KEYS_HEADER}" PROPERTIES GENERATED TRUE)
target_include_directories(dltool_settings PRIVATE "${SETTINGS_GENERATED_INCLUDE_DIR}")

target_compile_definitions(dltool_settings
    PRIVATE SETTINGS_LIBRARY
)

install(FILES "${SETTINGS_GENERATED_KEYS_HEADER}"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/${PLUGIN_NAME}
        COMPONENT dev)
