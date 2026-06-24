#pragma once

#include "dltool/data/Export.h"
#include "common/Singleton.h"

#include <unordered_map>

namespace dltool::data {

class DATA_API DataFormat : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DataFormat)
    QT_QML_SINGLETON(DataFormat)
public:
    enum SupportedDataFormat
    {
        LabelMe = 0,
        COCO    = 1,
        Mask    = 2,
    };
    Q_ENUM(SupportedDataFormat)

    static Q_INVOKABLE QList<QString> getSupportedDataFormat()
    {
        return ImportDataFormatList;
    }

    static Q_INVOKABLE QList<QString> getSupportedExportDataFormat()
    {
        return ExportDataFormatList;
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

    static bool isDataFormatSupported(const int data_format)
    {
        return IdToName.find(data_format) != IdToName.end();
    }

private:
    explicit DataFormat(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~DataFormat() {}

    inline static const QList<QString> ImportDataFormatList = {
        "LabelMe",
        "COCO",
        "Mask",
    };

    inline static const QList<QString> ExportDataFormatList = {
        "LabelMe",
        "COCO",
    };

    inline static const std::unordered_map<int, QString> IdToName = {
        {LabelMe, "LabelMe"},
        {  COCO,    "COCO"},
        {  Mask,    "Mask"},
    };

    inline static const std::unordered_map<QString, int> NameToId = {
        {"LabelMe", LabelMe},
        {   "COCO",    COCO},
        {   "Mask",    Mask},
    };

    inline static const QList<QString> ImageFormatList = {
        "*.jpg", "*.jpeg", "*.png", "*.bmp", "*.webp",
    };
};

} // namespace dltool::data
