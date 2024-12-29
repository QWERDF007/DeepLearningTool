#pragma once

#include "DataExport.h"
#include "common/Singleton.h"

#include <unordered_map>

namespace dltool::data {

class DATA_API DataFormat : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DataFormat)
    QT_QML_SINGLETON(DataFormat)
public:
    static Q_INVOKABLE QList<QString> getSupportedDataFormat()
    {
        return DataFormatList;
    }

    static Q_INVOKABLE QList<QString> getSupportedImageFormat()
    {
        return ImageFormatList;
    }

    static Q_INVOKABLE int getDataFormat(const QString &name)
    {
        auto found = NameToId.find(name);
        if (found == NameToId.end())
            return -1;
        return found->second;
    }

private:
    explicit DataFormat(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~DataFormat() {}

    enum SupportedDataFormat
    {
        LabelMe = 0,
    };

    inline static const QList<QString> DataFormatList = {
        "LabelMe",
    };

    inline static const std::unordered_map<QString, int> NameToId = {
        {"LabelMe", LabelMe},
    };

    inline static const QList<QString> ImageFormatList = {
        "*.jpg",
        "*.png",
        "*.bmp",
    };
};

} // namespace dltool::data
