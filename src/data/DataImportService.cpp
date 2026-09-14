#include "DataImportService.h"

#include "common/Utils.h"
#include "data/DataFormat.h"
#include "data/DataIO.h"
#include "data/DataNameUtils.h"
#include "data/GlobalFilter.h"
#include "data/DataViewModels.h"
#include "database/DataBase.h"
#include "ui/ProgressManager.h"
#include "ui/SignalHelper.h"

#include <spdlog/spdlog.h>

#include <QColor>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QUuid>
#include <set>

namespace dltool::data {

using dltool::common::cleanPath;

namespace {

std::map<QString, QString> parseLabelClassGroupMap(const QVariantMap &groups)
{
    std::map<QString, QString> result;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it)
    {
        const QString name = sanitizeName(it.key());
        if (name.isEmpty())
            continue;
        result[name] = normalizeLabelClassGroup(it.value().toString());
    }
    return result;
}

} // namespace

DataImportService::DataImportService(DataManagerServices services) : s_(std::move(services)) {}

DataImportService::~DataImportService() = default;

void DataImportService::importData(const int64_t dataset_id, const int data_format, const QString &image_dir,
                                   const QString &data_dir, const std::map<QString, QString> &label_class_groups)
{
    if (s_.isShuttingDown())
        return;
    startImportData(dataset_id, data_format, image_dir, data_dir, label_class_groups);
}

void DataImportService::scanImportLabelClasses(const int data_format, const QString &image_dir,
                                               const QString &data_dir)
{
    if (s_.isShuttingDown())
        return;

    if (s_.is_data_operation_running())
    {
        const QString message = QString("已有数据操作正在运行");
        ui::SignalHelper::notifyWarn(QString("导入失败"), message);
        s_.emit_import_label_classes_scanned(false, {}, message);
        return;
    }

    if (!data::DataFormat::isImportDataFormatSupported(s_.method, data_format))
    {
        const QString message = QString("当前项目类型不支持该导入格式");
        spdlog::error("扫描导入类别失败, 项目类型 {} 不支持数据格式: {}", s_.method, data_format);
        ui::SignalHelper::notifyError(QString("导入失败"), message);
        s_.emit_import_label_classes_scanned(false, {}, message);
        return;
    }

    const QString clean_image_dir = cleanPath(image_dir);
    if (clean_image_dir.isEmpty())
    {
        const QString message = QString("导入图像路径为空");
        spdlog::error("扫描导入类别失败, {}", message.toUtf8().constData());
        ui::SignalHelper::notifyError(QString("导入失败"), message);
        s_.emit_import_label_classes_scanned(false, {}, message);
        return;
    }

    const QFileInfo image_dir_info(clean_image_dir);
    if (!image_dir_info.isAbsolute() || !image_dir_info.exists())
    {
        const QString message = QString("图像路径不存在或路径无效: %1").arg(image_dir);
        spdlog::error("扫描导入类别失败, {}", message.toUtf8().constData());
        ui::SignalHelper::notifyError(QString("导入失败"), message);
        s_.emit_import_label_classes_scanned(false, {}, message);
        return;
    }

    const QString clean_data_dir = cleanPath(data_dir);
    if (!clean_data_dir.isEmpty())
    {
        const QFileInfo data_dir_info(clean_data_dir);
        if (!data_dir_info.isAbsolute() || !data_dir_info.exists())
        {
            const QString message = QString("标注路径不存在或路径无效: %1").arg(data_dir);
            spdlog::error("扫描导入类别失败, {}", message.toUtf8().constData());
            ui::SignalHelper::notifyError(QString("导入失败"), message);
            s_.emit_import_label_classes_scanned(false, {}, message);
            return;
        }
    }

    DataIO *scanner = DataIO::createIO(data_format, s_.host);
    if (!scanner)
    {
        const QString message = QString("不支持的数据格式");
        spdlog::error("无法为格式 {} 创建扫描器", data_format);
        ui::SignalHelper::notifyError(QString("导入失败"), message);
        s_.emit_import_label_classes_scanned(false, {}, message);
        return;
    }

    s_.set_data_operation_running(true);
    scanner->setTargetMethod(s_.method);
    qRegisterMetaType<std::map<QString, QString>>("std::map<QString, QString>");

    const QString scan_task_id = QStringLiteral("scan_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    scanner->setTaskId(scan_task_id);
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "startTask", Qt::QueuedConnection,
                              Q_ARG(QString, "扫描导入类别"), Q_ARG(QString, scan_task_id));

    QObject::connect(
        scanner, &DataIO::labelClassesScanned, s_.host,
        [this, scanner, scan_task_id](bool success, const std::map<QString, QString> &label_class_info,
                                      const QString &message)
        {
            if (s_.isShuttingDown())
            {
                scanner->deleteLater();
                return;
            }

            QVariantList label_classes;
            if (success)
            {
                std::set<QString> used_colors;
                if (s_.label_classes)
                {
                    for (const int64_t id : s_.label_classes->getAllLabelClassIds())
                    {
                        const QString c    = s_.label_classes->getLabelClassColor(static_cast<int>(id));
                        const QString norm = QColor(c.trimmed()).name(QColor::HexRgb).toLower();
                        if (!norm.isEmpty())
                            used_colors.insert(norm);
                    }
                }

                for (const auto &[name, color] : label_class_info)
                {
                    const int label_class_id = s_.label_classes ? s_.label_classes->getLabelClassId(name) : -1;
                    QString   effective_color;
                    if (label_class_id >= 0 && s_.label_classes)
                    {
                        effective_color = s_.label_classes->getLabelClassColor(label_class_id);
                    }
                    else
                    {
                        const QString norm_cand = QColor(color.trimmed()).name(QColor::HexRgb).toLower();
                        if (!norm_cand.isEmpty() && used_colors.find(norm_cand) == used_colors.end())
                        {
                            effective_color = norm_cand;
                        }
                        else
                        {
                            effective_color = DatasetIO::allocateUniqueColor(used_colors);
                        }
                        used_colors.insert(effective_color);
                    }

                    const QString group = label_class_id >= 0 && s_.label_classes
                                              ? s_.label_classes->getLabelClassGroup(label_class_id)
                                              : defaultLabelClassGroup();

                    QVariantMap item;
                    item.insert(QStringLiteral("label_class_id"), label_class_id);
                    item.insert(QStringLiteral("name"), name);
                    item.insert(QStringLiteral("color"), effective_color);
                    item.insert(QStringLiteral("group"), normalizeLabelClassGroup(group));
                    item.insert(QStringLiteral("group_name"), labelClassGroupDisplayName(group));
                    item.insert(QStringLiteral("existing"), label_class_id >= 0);
                    label_classes.append(item);
                }
            }

            const int     level            = success ? spdlog::level::info : spdlog::level::err;
            const QString progress_message = message.isEmpty() ? QString("导入类别扫描完成") : message;
            QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                      Q_ARG(int, level), Q_ARG(QString, progress_message), Q_ARG(QString, scan_task_id));
            QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "finishTask", Qt::QueuedConnection,
                                      Q_ARG(QString, scan_task_id), Q_ARG(bool, success));

            if (!success)
            {
                const QString err_msg = message.isEmpty() ? QString("扫描导入类别失败") : message;
                spdlog::error("{}", err_msg.toUtf8().constData());
                ui::SignalHelper::notifyError(QString("导入失败"), err_msg);
            }

            scanner->deleteLater();
            s_.set_data_operation_running(false);
            // 先释放扫描状态，再通知 QML。QML 可能在收到信号后立即启动正式导入。
            s_.emit_import_label_classes_scanned(success, label_classes, message);
        },
        Qt::QueuedConnection);

    scanner->startScanLabelClasses(clean_image_dir, clean_data_dir);
}

void DataImportService::startImportData(const int64_t dataset_id, const int data_format, const QString &image_dir,
                                        const QString &data_dir, const std::map<QString, QString> &label_class_groups)
{
    if (s_.isShuttingDown())
        return;

    if (s_.is_data_operation_running())
    {
        const QString message = QString("已有数据操作正在运行");
        spdlog::warn("导入数据失败, 已有数据操作正在运行");
        ui::SignalHelper::notifyWarn(QString("导入失败"), message);
        s_.emit_data_import_finished(false, message);
        return;
    }

    // 验证数据格式是否支持当前项目类型
    if (!data::DataFormat::isImportDataFormatSupported(s_.method, data_format))
    {
        const QString message = QString("当前项目类型不支持该导入格式");
        spdlog::error("导入数据失败, 项目类型 {} 不支持数据格式: {}", s_.method, data_format);
        ui::SignalHelper::notifyError(QString("导入失败"), message);
        s_.emit_data_import_finished(false, message);
        return;
    }

    const QString clean_image_dir = cleanPath(image_dir);
    if (clean_image_dir.isEmpty())
    {
        const QString message = QString("导入图像路径为空");
        spdlog::error("导入数据失败, {}", message.toUtf8().constData());
        ui::SignalHelper::notifyError(QString("导入失败"), message);
        s_.emit_data_import_finished(false, message);
        return;
    }

    const QFileInfo image_dir_info(clean_image_dir);
    if (!image_dir_info.isAbsolute() || !image_dir_info.exists())
    {
        const QString message = QString("图像路径不存在或路径无效: %1").arg(image_dir);
        spdlog::error("导入数据失败, {}", message.toUtf8().constData());
        ui::SignalHelper::notifyError(QString("导入失败"), message);
        s_.emit_data_import_finished(false, message);
        return;
    }

    const QString clean_data_dir = cleanPath(data_dir);
    if (!clean_data_dir.isEmpty())
    {
        const QFileInfo data_dir_info(clean_data_dir);
        if (!data_dir_info.isAbsolute() || !data_dir_info.exists())
        {
            const QString message = QString("标注路径不存在或路径无效: %1").arg(data_dir);
            spdlog::error("导入数据失败, {}", message.toUtf8().constData());
            ui::SignalHelper::notifyError(QString("导入失败"), message);
            s_.emit_data_import_finished(false, message);
            return;
        }
    }

    QString db_check_err_msg;
    if (s_.database == nullptr || !s_.database->checkIntegrity(db_check_err_msg))
    {
        const QString message = QString("项目数据库检查失败，无法导入数据: %1").arg(db_check_err_msg);
        spdlog::error("{}", message.toUtf8().constData());
        ui::SignalHelper::notifyError(QString("导入失败"), message);
        s_.emit_data_import_finished(false, message);
        return;
    }

    // 使用工厂函数创建导入器，并通过 ImportDatabaseWriter 独占事务边界
    // （批次提交 + 失败回滚）；worker 只接触自己的数据上下文。
    DataIO *importer = DataIO::createIO(data_format, s_.host);
    if (!importer)
    {
        const QString message = QString("不支持的数据格式");
        spdlog::error("无法为格式 {} 创建导入器", data_format);
        ui::SignalHelper::notifyError(QString("导入失败"), message);
        s_.emit_data_import_finished(false, message);
        return;
    }
    s_.set_data_operation_running(true);
    const QString import_task_id = QStringLiteral("import_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    importer->setTaskId(import_task_id);
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "startTask", Qt::QueuedConnection,
                              Q_ARG(QString, "导入数据"), Q_ARG(QString, import_task_id));
    importer->setTargetMethod(s_.method);
    import_running_         = true;
    current_import_task_id_ = import_task_id;
    import_elapsed_timer_.restart();

    qRegisterMetaType<std::vector<QString>>("std::vector<QString>");
    qRegisterMetaType<std::vector<int64_t>>("std::vector<int64_t>");
    qRegisterMetaType<std::map<QString, QString>>("std::map<QString, QString>");
    qRegisterMetaType<std::vector<ImportedLabel>>("std::vector<ImportedLabel>");
    qRegisterMetaType<dltool::data::ImportDatabaseWriter::Stats>("dltool::data::ImportDatabaseWriter::Stats");

    auto *writer = new ImportDatabaseWriter(s_.database->path(), s_.method, data_format, dataset_id,
                                            label_class_groups, importer, s_.host);

    QObject::connect(importer, &DataIO::dataBatchReady, writer, &ImportDatabaseWriter::onDataBatchReady,
                     Qt::DirectConnection);
    QObject::connect(importer, &DataIO::importFinished, writer, &ImportDatabaseWriter::onImporterFinished,
                     Qt::DirectConnection);
    QObject::connect(writer, &ImportDatabaseWriter::finished, s_.host,
                     [this, writer](const bool success, const QString &message,
                                    const ImportDatabaseWriter::Stats &stats)
                     { handleImportSessionFinished(success, message, stats, writer); }, Qt::QueuedConnection);

    // 启动导入
    importer->startImport(dataset_id, clean_image_dir, clean_data_dir);
}

void DataImportService::handleImportSessionFinished(const bool success, const QString &message,
                                                    const ImportDatabaseWriter::Stats &stats,
                                                    ImportDatabaseWriter *writer)
{
    if (s_.isShuttingDown())
        return;

    Q_UNUSED(stats);

    const qint64 elapsed_ms = import_elapsed_timer_.isValid() ? import_elapsed_timer_.elapsed() : 0;
    QString completed_message = QString("%1，耗时 %2 ms").arg(message).arg(elapsed_ms);
    if (!success)
    {
        completed_message += QStringLiteral("，已回滚导入数据");
    }

    if (success)
    {
        spdlog::info("{}", completed_message.toUtf8().constData());
        if (s_.label_classes != nullptr)
        {
            s_.label_classes->reloadFromDatabase();
        }
        if (s_.image_source != nullptr)
        {
            s_.image_source->reloadFromDatabase();
        }
        if (s_.label_source != nullptr)
        {
            s_.label_source->reloadFromDatabase();
        }
        s_.rebuild_label_relations();
        if (s_.image_info != nullptr)
        {
            s_.image_info->updateLabelInfo();
        }
        if (s_.global_filter != nullptr && s_.global_filter->isActive())
        {
            s_.global_filter->refresh();
        }
    }
    else
    {
        spdlog::error("{}", completed_message.toUtf8().constData());
    }

    const QString task_id = current_import_task_id_;
    const int level = success ? spdlog::level::info : spdlog::level::err;
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                              Q_ARG(int, level), Q_ARG(QString, completed_message), Q_ARG(QString, task_id));
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "finishTask", Qt::QueuedConnection,
                              Q_ARG(QString, task_id), Q_ARG(bool, success));

    if (writer != nullptr)
    {
        writer->deleteLater();
    }
    const QList<DataIO *> io_children = s_.host->findChildren<DataIO *>();
    for (DataIO *io : io_children)
    {
        if (io != nullptr)
        {
            io->deleteLater();
        }
    }

    import_running_ = false;
    s_.set_data_operation_running(false);
    s_.emit_data_import_finished(success, completed_message);

    QMetaObject::invokeMethod(
        ui::SignalHelper::getInstance(),
        [success, completed_message]()
        {
            if (success)
                ui::SignalHelper::notifySuccess(QString("导入完成"), completed_message);
            else
                ui::SignalHelper::notifyError(QString("导入失败"), completed_message);
        },
        Qt::QueuedConnection);
}

} // namespace dltool::data
