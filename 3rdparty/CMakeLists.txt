# sqlpp11
set(BUILD_SQLITE3_CONNECTOR ON CACHE BOOL "Build sqlite3 connector")
add_subdirectory(sqlpp11)

# QuickUI
add_subdirectory(QuickUI)

# spdlog
add_subdirectory(spdlog)

# yaml-cpp
add_subdirectory(yaml-cpp)

# hwinfo
set(HWINFO_SHARED OFF CACHE BOOL "Build hwinfo as a shared library" FORCE)
set(HWINFO_STATIC ON CACHE BOOL "Build hwinfo as a static library" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Build hwinfo examples" FORCE)
set(HWINFO_CPU ON CACHE BOOL "Enable hwinfo CPU information" FORCE)
set(HWINFO_GPU ON CACHE BOOL "Enable hwinfo GPU information" FORCE)
set(HWINFO_GPU_OPENCL OFF CACHE BOOL "Enable hwinfo OpenCL GPU information" FORCE)
set(HWINFO_OS OFF CACHE BOOL "Enable hwinfo OS information" FORCE)
set(HWINFO_MAINBOARD OFF CACHE BOOL "Enable hwinfo mainboard information" FORCE)
set(HWINFO_DISK OFF CACHE BOOL "Enable hwinfo disk information" FORCE)
set(HWINFO_RAM OFF CACHE BOOL "Enable hwinfo RAM information" FORCE)
set(HWINFO_BATTERY OFF CACHE BOOL "Enable hwinfo battery information" FORCE)
set(HWINFO_NETWORK OFF CACHE BOOL "Enable hwinfo network information" FORCE)
add_subdirectory(hwinfo EXCLUDE_FROM_ALL)

if(MSVC)
    # hwinfo/platform.h defines NOMINMAX itself while the project also passes
    # /DNOMINMAX globally. Suppress only that harmless redefinition warning
    # for hwinfo's component targets; keep /WX for all other diagnostics.
    foreach(HWINFO_TARGET hwinfo_cpu hwinfo_gpu)
        if(TARGET ${HWINFO_TARGET})
            target_compile_options(${HWINFO_TARGET} PRIVATE /wd4005)
        endif()
    endforeach()
endif()
