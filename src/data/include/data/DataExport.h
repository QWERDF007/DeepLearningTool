#pragma once

#ifdef _WIN32
#    define DATA_HIDDEN
#    define DATA_EXPORT __declspec(dllexport)
#    define DATA_IMPORT __declspec(dllimport)
#elif __GNUC__
#    define DATA_EXPORT __attribute__((__visibility__("default")))
#    define DATA_HIDDEN __attribute__((__visibility__("hidden")))
#else
#    define DATA_EXPORT
#    define DATA_IMPORT
#    define DATA_HIDDEN
#endif

#ifdef DLTOOL_DATA_BUILD_SHARED_LIBS
#    define DATA_API DATA_EXPORT
#else
#    define DATA_API DATA_IMPORT
#endif