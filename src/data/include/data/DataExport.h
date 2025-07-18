#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
#    define DATA_HIDDEN
#    define DATA_EXPORT __declspec(dllexport)
#    define DATA_IMPORT __declspec(dllimport)
#elif defined(__GNUC__) || defined(__clang__)
#    define DATA_EXPORT __attribute__((visibility("default")))
#    define DATA_IMPORT __attribute__((visibility("default")))
#    define DATA_HIDDEN __attribute__((visibility("hidden")))
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
