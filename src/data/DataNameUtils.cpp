#include "data/DataNameUtils.h"

namespace dltool::data {

bool isValidNameCharacter(const QChar &ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('-') || ch == QLatin1Char('_');
}

QString invalidNameError(const QString &name)
{
    if (name.isEmpty())
    {
        return QString("error:名称不能为空");
    }

    for (const QChar &ch : name)
    {
        if (!isValidNameCharacter(ch))
        {
            return QString("error:名称只能使用字母、数字、中文、-、_");
        }
    }
    return QString();
}

QString sanitizeName(const QString &name)
{
    QString sanitized;
    sanitized.reserve(name.size());
    for (const QChar &ch : name)
    {
        sanitized.append(isValidNameCharacter(ch) ? ch : QLatin1Char('_'));
    }
    return sanitized;
}

} // namespace dltool::data
