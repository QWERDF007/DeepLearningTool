#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
#    define DATABASE_HIDDEN
#    define DATABASE_EXPORT __declspec(dllexport)
#    define DATABASE_IMPORT __declspec(dllimport)
#elif defined(__GNUC__) || defined(__clang__)
#    define DATABASE_EXPORT __attribute__((visibility("default")))
#    define DATABASE_IMPORT __attribute__((visibility("default")))
#    define DATABASE_HIDDEN __attribute__((visibility("hidden")))
#else
#    define DATABASE_EXPORT
#    define DATABASE_IMPORT
#    define DATABASE_HIDDEN
#endif

#ifdef DLTOOL_DATABASE_BUILD_SHARED_LIBS
#    define DATABASE_API DATABASE_EXPORT
#else
#    define DATABASE_API DATABASE_IMPORT
#endif
