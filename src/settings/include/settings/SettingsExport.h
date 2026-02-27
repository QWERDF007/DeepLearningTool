#pragma once

#include <QtCore/qglobal.h>

#if defined(SETTINGS_LIBRARY)
#    define SETTINGS_API Q_DECL_EXPORT
#else
#    define SETTINGS_API Q_DECL_IMPORT
#endif
