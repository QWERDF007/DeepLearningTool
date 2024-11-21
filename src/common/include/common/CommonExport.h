#pragma once

#ifdef _WIN32
#    define COMMON_HIDDEN
#    define COMMON_EXPORT __declspec(dllexport)
#    define COMMON_IMPORT __declspec(dllimport)
#elif __GNUC__
#    define COMMON_EXPORT __attribute__((__visibility__("default")))
#    define COMMON_HIDDEN __attribute__((__visibility__("hidden")))
#else
#    define COMMON_EXPORT
#    define COMMON_IMPORT
#    define COMMON_HIDDEN
#endif

#ifdef DLTOOL_COMMON_BUILD_SHARED_LIBS
#    define COMMON_API COMMON_EXPORT
#else
#    define COMMON_API COMMON_IMPORT
#endif