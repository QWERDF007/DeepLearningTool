#pragma once

#include <QObject>
#include <QString>
#include <string>

#ifdef _WIN32
#    include <Windows.h>
#endif

namespace dltool::common {

QString uuid();
QString toQString(const QStringList &, const QString &sep = ", ", Qt::SplitBehavior behavior = Qt::KeepEmptyParts);

#ifdef _WIN32
std::string  wcharToString(const wchar_t *wstr);
std::wstring stringToWchar(const std::string &str);
#endif

QString              getDirectory(const QString &path);
std::vector<QString> getDirectories(const QString &path, bool recursive = false);
std::vector<QString> getFiles(const QString &path, const QStringList &name_filters, bool recursive = false);

class FileReader : QObject
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
