#include "data/DataImporter.h"

#include "data/LabelMeImporter.h"
#include "database/DataBase.h"
#include "ui/ProgressManager.h"
#include "data/DataFormat.h"

#include <spdlog/spdlog.h>

namespace dltool::data {

DataImporter::DataImporter(dltool::database::ProjectDataBase *database, QObject *parent)
    : QObject(parent)
    , database_(database)
{
}

DataImporter::~DataImporter() {}

DataImporter *DataImporter::createImporter(int data_format, dltool::database::ProjectDataBase *database,
                                           QObject *parent)
{
    spdlog::info("创建导入器: data_format={}", data_format);

    // 验证格式是否支持
    if (!DataFormat::isDataFormatSupported(data_format))
    {
        spdlog::error("不支持的数据格式: {}", data_format);
        return nullptr;
    }

    // 根据格式创建相应的导入器
    // 工厂模式：根据数据格式标识符返回对应的导入器实例
    // 添加新格式时，只需在此处添加新的 if 分支
    DataImporter *importer = nullptr;

    if (data_format == DataFormat::getDataFormat("LabelMe"))
    {
        spdlog::info("创建 LabelMeImporter 实例");
        importer = new LabelMeImporter(database, parent);
    }
    else
    {
        spdlog::error("未实现的数据格式: {}", data_format);
        return nullptr;
    }

    if (importer)
    {
        spdlog::info("成功创建导入器实例");
    }
    else
    {
        spdlog::error("创建导入器失败");
    }

    return importer;
}

void DataImporter::updateProgress(int progress, const QString &message)
{
    // 使用 QueuedConnection 确保在主线程（UI 线程）中更新进度
    // 这对于从后台线程调用此方法是安全的
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "updateProgress", Qt::QueuedConnection,
                              Q_ARG(int, progress));
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                              Q_ARG(int, spdlog::level::info), Q_ARG(QString, message));
}

} // namespace dltool::data
