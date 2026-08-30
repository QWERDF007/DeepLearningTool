find_package(Qt6 REQUIRED COMPONENTS Core Test)

include(AddTest)

enable_testing()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

dlt_glob_sources(_test_module_cmake_files
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
    PATTERNS "*/CMakeLists.txt"
)
foreach(_test_module_cmake_file IN LISTS _test_module_cmake_files)
    get_filename_component(_test_module "${_test_module_cmake_file}" DIRECTORY)
    add_subdirectory("${_test_module}")
endforeach()

unset(_test_module_cmake_files)
unset(_test_module_cmake_file)
unset(_test_module)
