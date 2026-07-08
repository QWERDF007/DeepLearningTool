#pragma once

#include "dltool/common/Export.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <string>
#include <vector>

#ifdef _WIN32
#    include <Windows.h>
#endif

namespace dltool::common {

COMMON_API QString uuid();
COMMON_API QString toQString(const QStringList &, const QString &sep = ", ",
                             Qt::SplitBehavior behavior = Qt::KeepEmptyParts);
COMMON_API QString cleanPath(const QString &path);
COMMON_API QString runtimePath(const QString &path);
COMMON_API QString resolvePath(const QString &base_dir, const QString &path);
COMMON_API QString pythonExecutableFromEnvPath(const QString &env_path);

#ifdef _WIN32
COMMON_API std::string wcharToString(const wchar_t *wstr);
COMMON_API std::wstring stringToWchar(const std::string &str);
#endif

COMMON_API QString getDirectory(const QString &path);
COMMON_API std::vector<QString> getDirectories(const QString &path, bool recursive = false);
COMMON_API std::vector<QString> getFiles(const QString &path, const QStringList &name_filters, bool recursive = false);

class COMMON_API FileReader : QObject
{
public:
    FileReader(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    FileReader(const QStringList &name_filters, QObject *parent = nullptr)
        : QObject(parent)
        , name_filters_(name_filters)
    {
    }

    void setNameFilters(const QStringList &name_filters)
    {
        name_filters_ = name_filters;
    }

    const QString read(const QString &root, bool recursive = false, bool circular = false);

    int size() const
    {
        return static_cast<int>(paths_.size());
    }

    inline static const QStringList ImageFilters{"*.jpg", "*.jpeg", "*.png", "*.bmp", "*.webp"};

private:
    QStringList          name_filters_;
    QString              root_;
    std::vector<QString> paths_;
    int                  cur_{0};
};

} // namespace dltool::common
