#include "data/DataExporter.h"

#include "data/COCOExporter.h"
#include "data/DataFormat.h"
#include "data/LabelMeExporter.h"
#include "ui/ProgressManager.h"

#include <spdlog/spdlog.h>

namespace dltool::data {

DataExporter::DataExporter(QObject *parent)
    : QObject(parent)
{
}

DataExporter::~DataExporter() {}

DataExporter *DataExporter::createExporter(int data_format, QObject *parent)
{
    if (!DataFormat::isDataFormatSupported(data_format))
    {
        spdlog::error("不支持的数据导出格式: {}", data_format);
        return nullptr;
    }

    if (data_format == DataFormat::LabelMe)
    {
        return new LabelMeExporter(parent);
    }

    if (data_format == DataFormat::COCO)
    {
        return new COCOExporter(parent);
    }

    spdlog::error("未实现的数据导出格式: {}", data_format);
    return nullptr;
}

void DataExporter::updateProgress(int progress, const QString &message)
{
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "updateProgress", Qt::QueuedConnection,
                              Q_ARG(int, progress));
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                              Q_ARG(int, spdlog::level::info), Q_ARG(QString, message));
}

} // namespace dltool::data
