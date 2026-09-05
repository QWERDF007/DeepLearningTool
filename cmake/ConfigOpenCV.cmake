include("${CMAKE_CURRENT_LIST_DIR}/ConfigDependencyDefaults.cmake")

set(_dlt_opencv_home_from_default OFF)
set(_dlt_opencv_default_home)
set(_dlt_opencv_search_dir)

if(DEFINED OpenCV_DIR AND "${OpenCV_DIR}" MATCHES "-NOTFOUND$")
    unset(OpenCV_DIR CACHE)
    unset(OpenCV_DIR)
endif()

dlt_dependency_variable_is_explicit(
    OpenCV_DIR DLT_DEPENDENCY_OPENCV_DIR _dlt_opencv_dir_explicit)
if(NOT _dlt_opencv_dir_explicit)
    unset(OpenCV_DIR CACHE)
    unset(OpenCV_DIR)
endif()

if(_dlt_opencv_dir_explicit)
    set(_dlt_opencv_search_dir "${OpenCV_DIR}")
elseif(DEFINED ENV{OpenCV_DIR} AND NOT "$ENV{OpenCV_DIR}" STREQUAL "")
    set(_dlt_opencv_search_dir "$ENV{OpenCV_DIR}")
    dlt_dependency_cache_set(
        OpenCV_DIR "${_dlt_opencv_search_dir}" PATH
        "OpenCV CMake package directory"
        DLT_DEPENDENCY_OPENCV_DIR environment)
else()
    dlt_dependency_resolve_path(
        _dlt_opencv_home _dlt_opencv_home_origin opencv
        VARIABLES OpenCV_HOME OpenCV_ROOT
        ENVIRONMENT_VARIABLES OpenCV_HOME OpenCV_ROOT
    )
    if(_dlt_opencv_home)
        dlt_dependency_cache_set(
            OpenCV_HOME "${_dlt_opencv_home}" PATH
            "OpenCV installation directory"
            DLT_DEPENDENCY_OPENCV_HOME "${_dlt_opencv_home_origin}")
        set(_dlt_opencv_search_dir "${OpenCV_HOME}/lib")
        if(_dlt_opencv_home_origin STREQUAL "project-default")
            set(_dlt_opencv_home_from_default ON)
        endif()
    endif()
endif()

if(_dlt_opencv_search_dir)
    if(_dlt_opencv_dir_explicit)
        set(OpenCV_DIR "${_dlt_opencv_search_dir}")
    elseif(NOT DEFINED OpenCV_DIR OR NOT OpenCV_DIR STREQUAL "${_dlt_opencv_search_dir}")
        dlt_dependency_cache_set(
            OpenCV_DIR "${_dlt_opencv_search_dir}" PATH
            "OpenCV CMake package directory"
            DLT_DEPENDENCY_OPENCV_DIR
            "${_dlt_opencv_home_origin}")
    endif()
endif()

if(DEFINED OpenCV_DIR AND NOT OpenCV_DIR STREQUAL "")
    set(OpenCV_LIBRARY_DIR "${OpenCV_DIR}")
endif()

find_package(OpenCV REQUIRED)

if(OpenCV_FOUND)
    if(_dlt_opencv_home_from_default
       AND DEFINED OpenCV_LIB_PATH AND NOT OpenCV_LIB_PATH STREQUAL "")
        foreach(_dlt_opencv_candidate IN ITEMS
                "${OpenCV_LIB_PATH}/.."
                "${OpenCV_LIB_PATH}/../.."
                "${OpenCV_LIB_PATH}/../../.."
                "${OpenCV_LIB_PATH}/../../../..")
            if(IS_DIRECTORY "${_dlt_opencv_candidate}/include")
                get_filename_component(_dlt_opencv_home "${_dlt_opencv_candidate}" ABSOLUTE)
                unset(OpenCV_HOME CACHE)
                set(OpenCV_HOME "${_dlt_opencv_home}" CACHE PATH
                    "OpenCV installation root")
                set(_dlt_opencv_home_from_default OFF)
                break()
            endif()
        endforeach()
    endif()

    if(_dlt_opencv_home_from_default AND OpenCV_DIR)
        foreach(_dlt_opencv_candidate IN ITEMS
                "${OpenCV_DIR}"
                "${OpenCV_DIR}/.."
                "${OpenCV_DIR}/../.."
                "${OpenCV_DIR}/../../.."
                "${OpenCV_DIR}/../../../..")
            if(IS_DIRECTORY "${_dlt_opencv_candidate}/bin")
                get_filename_component(_dlt_opencv_home "${_dlt_opencv_candidate}" ABSOLUTE)
                unset(OpenCV_HOME CACHE)
                set(OpenCV_HOME "${_dlt_opencv_home}" CACHE PATH
                    "OpenCV installation root")
                break()
            endif()
        endforeach()
    endif()

    if((NOT DEFINED OpenCV_HOME OR OpenCV_HOME STREQUAL "") AND OpenCV_DIR)
        foreach(_dlt_opencv_candidate IN ITEMS
                "${OpenCV_DIR}"
                "${OpenCV_DIR}/.."
                "${OpenCV_DIR}/../.."
                "${OpenCV_DIR}/../../.."
                "${OpenCV_DIR}/../../../..")
            if(IS_DIRECTORY "${_dlt_opencv_candidate}/bin")
                get_filename_component(_dlt_opencv_home "${_dlt_opencv_candidate}" ABSOLUTE)
                set(OpenCV_HOME "${_dlt_opencv_home}" CACHE PATH
                    "OpenCV installation root")
                break()
            endif()
        endforeach()
    endif()

    if((NOT DEFINED OpenCV_HOME OR OpenCV_HOME STREQUAL "")
       AND DEFINED OpenCV_LIB_PATH AND NOT OpenCV_LIB_PATH STREQUAL "")
        foreach(_dlt_opencv_candidate IN ITEMS
                "${OpenCV_LIB_PATH}/.."
                "${OpenCV_LIB_PATH}/../.."
                "${OpenCV_LIB_PATH}/../../.."
                "${OpenCV_LIB_PATH}/../../../..")
            if(IS_DIRECTORY "${_dlt_opencv_candidate}/include")
                get_filename_component(_dlt_opencv_home "${_dlt_opencv_candidate}" ABSOLUTE)
                set(OpenCV_HOME "${_dlt_opencv_home}" CACHE PATH
                    "OpenCV installation root")
                break()
            endif()
        endforeach()
    endif()

    set(OpenCV_LIBRARY_DIR ${OpenCV_DIR})

    set(_dlt_opencv_bin_dir)
    if(DEFINED OpenCV_LIB_PATH AND NOT OpenCV_LIB_PATH STREQUAL "")
        get_filename_component(_dlt_opencv_bin_dir "${OpenCV_LIB_PATH}/../bin" ABSOLUTE)
        if(NOT IS_DIRECTORY "${_dlt_opencv_bin_dir}")
            unset(_dlt_opencv_bin_dir)
        endif()
    endif()
    if(NOT _dlt_opencv_bin_dir AND DEFINED OpenCV_HOME
       AND IS_DIRECTORY "${OpenCV_HOME}/bin")
        get_filename_component(_dlt_opencv_bin_dir "${OpenCV_HOME}/bin" ABSOLUTE)
    endif()
    if(NOT _dlt_opencv_bin_dir AND OpenCV_DIR)
        foreach(_dlt_opencv_candidate IN ITEMS
                "${OpenCV_DIR}/bin"
                "${OpenCV_DIR}/../bin"
                "${OpenCV_DIR}/../../bin"
                "${OpenCV_DIR}/../../../bin"
                "${OpenCV_DIR}/../../../../bin")
            if(IS_DIRECTORY "${_dlt_opencv_candidate}")
                get_filename_component(_dlt_opencv_bin_dir
                    "${_dlt_opencv_candidate}" ABSOLUTE)
                break()
            endif()
        endforeach()
    endif()
    if(_dlt_opencv_bin_dir
       AND (NOT DEFINED OpenCV_BIN_DIR OR OpenCV_BIN_DIR STREQUAL ""))
        set(OpenCV_BIN_DIR "${_dlt_opencv_bin_dir}" CACHE PATH
            "OpenCV runtime binary directory")
    endif()

    unset(_dlt_opencv_candidate)
    unset(_dlt_opencv_home)
    unset(_dlt_opencv_bin_dir)
endif()

unset(_dlt_opencv_home_from_default)
unset(_dlt_opencv_default_home)
unset(_dlt_opencv_search_dir)
unset(_dlt_opencv_home)
unset(_dlt_opencv_home_origin)
unset(_dlt_opencv_dir_explicit)
