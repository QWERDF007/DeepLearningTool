#pragma once

#ifdef _WIN32
#    define PROJECT_HIDDEN
#    define PROJECT_EXPORT __declspec(dllexport)
#    define PROJECT_IMPORT __declspec(dllimport)
#elif defined(__GNUC__) || defined(__clang__)
#    define PROJECT_EXPORT __attribute__((__visibility__("default")))
#    define PROJECT_IMPORT __attribute__((visibility("default")))
#    define PROJECT_HIDDEN __attribute__((__visibility__("hidden")))
#else
#    define PROJECT_EXPORT
#    define PROJECT_IMPORT
#    define PROJECT_HIDDEN
#endif

#ifdef DLTOOL_PROJECT_BUILD_SHARED_LIBS
#    define PROJECT_API PROJECT_EXPORT
#else
#    define PROJECT_API PROJECT_IMPORT
#endif
