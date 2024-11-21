#include "ui/Utils.h"

#include <QColor>
#include <QStandardPaths>

namespace dltool::ui {

Utils::Utils(QObject *parent)
    : QObject(parent)
{
}

Utils::~Utils() {}

QColor Utils::withOpacity(const QColor &color, qreal opacity) const
{
    int alpha = qRound(opacity * 255) & 0xff;
    return QColor::fromRgba((alpha << 24) | (color.rgba() & 0xffffff));
}

QString Utils::documentsLocation() const
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

} // namespace dltool::ui
