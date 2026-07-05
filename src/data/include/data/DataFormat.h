#pragma once

#include "common/Singleton.h"
#include "core/CoreDef.h"
#include "dltool/data/Export.h"

#include <QList>
#include <QString>
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
        Folder  = 3,
    };
    Q_ENUM(SupportedDataFormat)

    static Q_INVOKABLE QList<QString> getSupportedImportDataFormat(const int method)
    {
        return formatNamesForMethod(method, ImportFormatsByMethod);
    }

    static Q_INVOKABLE QList<QString> getSupportedExportDataFormat(const int method)
    {
        return formatNamesForMethod(method, ExportFormatsByMethod);
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

    static bool isImportDataFormatSupported(const int method, const int data_format)
    {
        return isFormatSupportedForMethod(method, data_format, ImportFormatsByMethod);
    }

    static bool isExportDataFormatSupported(const int method, const int data_format)
    {
        return isFormatSupportedForMethod(method, data_format, ExportFormatsByMethod);
    }

private:
    explicit DataFormat(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~DataFormat() {}

    static QList<QString> formatNamesForIds(const QList<int> &format_ids)
    {
        QList<QString> result;
        result.reserve(format_ids.size());
        for (const int format_id : format_ids)
        {
            auto found = IdToName.find(format_id);
            if (found != IdToName.end())
                result.push_back(found->second);
        }
        return result;
    }

    static QList<QString> formatNamesForMethod(const int method,
                                               const std::unordered_map<int, QList<int>> &formats_by_method)
    {
        auto found = formats_by_method.find(method);
        if (found == formats_by_method.end())
            return {};
        return formatNamesForIds(found->second);
    }

    static bool isFormatSupportedForMethod(const int method, const int data_format,
                                           const std::unordered_map<int, QList<int>> &formats_by_method)
    {
        if (!isDataFormatSupported(data_format))
            return false;

        auto found = formats_by_method.find(method);
        if (found == formats_by_method.end())
            return false;
        return found->second.contains(data_format);
    }

    inline static const std::unordered_map<int, QString> IdToName = {
        {LabelMe, "LabelMe"},
        {   COCO,    "COCO"},
        {   Mask,    "Mask"},
        { Folder,  "Folder"},
    };

    inline static const std::unordered_map<QString, int> NameToId = {
        {"LabelMe", LabelMe},
        {   "COCO",    COCO},
        {   "Mask",    Mask},
        {"Folder",  Folder},
    };

    inline static const std::unordered_map<int, QList<int>> ImportFormatsByMethod = {
        {    dltool::core::DeepLearningMethod::Classification, QList<int>{Folder}},
        {         dltool::core::DeepLearningMethod::Detection, QList<int>{COCO, LabelMe, Mask}},
        {      dltool::core::DeepLearningMethod::Segmentation, QList<int>{COCO, LabelMe, Mask}},
        {  dltool::core::DeepLearningMethod::AnomalyDetection, QList<int>{Folder, COCO, LabelMe, Mask}},
    };

    inline static const std::unordered_map<int, QList<int>> ExportFormatsByMethod = {
        {    dltool::core::DeepLearningMethod::Classification, QList<int>{Folder}},
        {         dltool::core::DeepLearningMethod::Detection, QList<int>{COCO, LabelMe, Mask}},
        {      dltool::core::DeepLearningMethod::Segmentation, QList<int>{COCO, LabelMe, Mask}},
        {  dltool::core::DeepLearningMethod::AnomalyDetection, QList<int>{COCO, LabelMe, Mask}},
    };

    inline static const QList<QString> ImageFormatList = {
        "*.jpg", "*.jpeg", "*.png", "*.bmp", "*.webp",
    };
};

} // namespace dltool::data
