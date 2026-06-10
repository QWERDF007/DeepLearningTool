#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
#    define MODEL_HIDDEN
#    define MODEL_EXPORT __declspec(dllexport)
#    define MODEL_IMPORT __declspec(dllimport)
#elif defined(__GNUC__) || defined(__clang__)
#    define MODEL_EXPORT __attribute__((visibility("default")))
#    define MODEL_IMPORT __attribute__((visibility("default")))
#    define MODEL_HIDDEN __attribute__((visibility("hidden")))
#else
#    define MODEL_EXPORT
#    define MODEL_IMPORT
#    define MODEL_HIDDEN
#endif

#ifdef DLTOOL_MODEL_BUILD_SHARED_LIBS
#    define MODEL_API MODEL_EXPORT
#else
#    define MODEL_API MODEL_IMPORT
#endif
