#pragma once

#include "dltool/data/Export.h"

#include <QChar>
#include <QString>

namespace dltool::data {

DATA_API bool isValidNameCharacter(const QChar &ch);
DATA_API QString invalidNameError(const QString &name);
DATA_API QString sanitizeName(const QString &name);

} // namespace dltool::data
