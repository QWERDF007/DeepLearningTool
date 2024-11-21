#pragma once

#ifdef _WIN32
#    define UI_HIDDEN
#    define UI_EXPORT __declspec(dllexport)
#    define UI_IMPORT __declspec(dllimport)
#elif __GNUC__
#    define UI_EXPORT __attribute__((__visibility__("default")))
#    define UI_HIDDEN __attribute__((__visibility__("hidden")))
#else
#    define UI_EXPORT
#    define UI_IMPORT
#    define UI_HIDDEN
#endif

#ifdef DLTOOL_UI_BUILD_SHARED_LIBS
#    define UI_API UI_EXPORT
#else
#    define UI_API UI_IMPORT
#endif