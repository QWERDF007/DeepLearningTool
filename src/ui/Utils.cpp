#include "ui/Utils.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
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

QString Utils::getCleanPath(const QString &path) const
{
#ifdef _WIN32
    return path.sliced(8);
#else
    return path.sliced(7);
#endif
}

void Utils::openInFileExplorer(const QString &path)
{
    // QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    const QString explorer = "explorer";
    QStringList   param{"/select,", QDir::toNativeSeparators(path)};
    QProcess::startDetached(explorer, param);
}

} // namespace dltool::ui
