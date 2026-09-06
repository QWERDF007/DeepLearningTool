find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(_tools_pytest_base_temp "${CMAKE_BINARY_DIR}/pytest-tools-tmp")

dlt_register_test(dltool_tools_tests
    COMMAND "${Python3_EXECUTABLE}" -m pytest tests/tools -q --basetemp "${_tools_pytest_base_temp}"
    LABELS "ordinary;tools"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

unset(_tools_pytest_base_temp)
