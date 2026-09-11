#include "DataExportService.h"

#include "common/Utils.h"
#include "data/DataFormat.h"
#include "data/DataIO.h"
#include "data/DatasetIO.h"
#include "ui/ProgressManager.h"
#include "ui/SignalHelper.h"

#include <spdlog/spdlog.h>

#include <QDir>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QUuid>

#include <algorithm>
#include <set>
#include <utility>

namespace dltool::data {

using dltool::common::ensureDirectory;

namespace {

void addProgressMessage(int level, const QString &message, const QString &taskId = QString())
{
    if (!taskId.isEmpty())
    {
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                  Q_ARG(int, level), Q_ARG(QString, message), Q_ARG(QString, taskId));
    }
    else
    {
        QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                                  Q_ARG(int, level), Q_ARG(QString, message));
    }
}

QString exportFormatName(const int data_format)
{
    switch (data_format)
    {
    case DataFormat::LabelMe:
        return QStringLiteral("LabelMe");
    case DataFormat::COCO:
        return QStringLiteral("COCO");
    case DataFormat::Mask:
        return QStringLiteral("Mask");
    case DataFormat::Folder:
        return QStringLiteral("Folder");
    default:
        return QStringLiteral("未知格式");
    }
}

QString exportDatasetSummary(const std::vector<QString> &dataset_names, const std::size_t selected_count)
{
    if (selected_count > 0 && selected_count <= 3 && dataset_names.size() == selected_count)
    {
        QStringList names;
        names.reserve(static_cast<qsizetype>(dataset_names.size()));
        for (const QString &name : dataset_names) names.append(name);
        return QStringLiteral("数据集: %1").arg(names.join(QStringLiteral(", ")));
    }
    return QStringLiteral("数据集数量: %1").arg(selected_count);
}

} // namespace

DataExportService::DataExportService(DataManagerServices services) : s_(std::move(services)) {}

DataExportService::~DataExportService() = default;

void DataExportService::requestCancel()
{
    if (active_cancel_token_)
    {
        active_cancel_token_->store(true);
    }
}

void DataExportService::exportDatasets(const std::vector<int64_t> &dataset_ids, const int data_format,
                                       const QString &output_dir, const QVariantMap &options)
{
    if (s_.isShuttingDown())
        return;

    if (s_.is_data_operation_running())
    {
        ui::SignalHelper::notifyWarn(QString("导出数据"), QString("当前已有数据操作正在进行中"));
        return;
    }

    if (!data::DataFormat::isExportDataFormatSupported(s_.method, data_format))
    {
        const QString message = QString("当前项目类型不支持该导出格式");
        spdlog::error("导出数据失败, 项目类型 {} 不支持数据格式: {}", s_.method, data_format);
        ui::SignalHelper::notifyError(QString("导出失败"), message);
        return;
    }

    QString clean_output_dir;
    QString resolve_err;
    if (!DatasetIO::resolveExportPath(output_dir, options, clean_output_dir, resolve_err))
    {
        spdlog::error("导出数据失败, {}", resolve_err.toUtf8().constData());
        ui::SignalHelper::notifyError(QString("导出失败"), resolve_err);
        return;
    }

    if (dataset_ids.empty())
    {
        const QString message = QString("未选择数据集");
        spdlog::warn("导出数据失败, {}", message.toUtf8().constData());
        ui::SignalHelper::notifyWarn(QString("导出失败"), message);
        return;
    }

    QString err_msg;
    if (!ensureDirectory(clean_output_dir, err_msg))
    {
        spdlog::error("导出数据失败, {}", err_msg.toUtf8().constData());
        ui::SignalHelper::notifyError(QString("导出失败"), err_msg);
        return;
    }

    struct ExportBatchItem
    {
        ExportDataset dataset;
        QString       output_dir;
    };

    struct ExportBatchState
    {
        QString                      task_id;
        std::vector<ExportBatchItem> items;
        QString                      dataset_summary;
        QElapsedTimer                elapsed_timer;
        QElapsedTimer                dataset_elapsed_timer;
        int                          current{0};
        int                          success_count{0};
        int                          failed_count{0};
    };

    auto state              = std::make_shared<ExportBatchState>();
    auto cancel_token       = std::make_shared<std::atomic_bool>(false);
    active_cancel_token_    = cancel_token;
    state->task_id = QStringLiteral("export_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "startTask", Qt::QueuedConnection,
                              Q_ARG(QString, "导出数据"), Q_ARG(QString, state->task_id));
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "updateProgress", Qt::QueuedConnection,
                              Q_ARG(int, 1), Q_ARG(QString, state->task_id));
    const std::vector<int64_t> selected_dataset_ids = dataset_ids;
    std::set<int64_t>          unique_selected_dataset_ids;
    std::vector<QString>       selected_dataset_names;
    for (const int64_t dataset_id : selected_dataset_ids)
    {
        if (!unique_selected_dataset_ids.insert(dataset_id).second)
            continue;
        const QString dataset_name
            = s_.datasets != nullptr ? s_.datasets->getDatasetName(static_cast<int>(dataset_id)) : QString();
        if (!dataset_name.isEmpty())
            selected_dataset_names.push_back(dataset_name);
    }
    state->dataset_summary = exportDatasetSummary(selected_dataset_names, unique_selected_dataset_ids.size());
    state->elapsed_timer.start();

    const QString format_name   = exportFormatName(data_format);
    const QString start_message = QString("开始导出数据: 格式=%1，%2").arg(format_name, state->dataset_summary);
    spdlog::info("{}", start_message.toUtf8().constData());
    addProgressMessage(spdlog::level::info, start_message);

    DatasetExportRequest export_request;
    export_request.dataset_ids = selected_dataset_ids;

    DataOperationWorkflow::Options prepare_options;
    prepare_options.manage_progress = false;
    s_.run_dataset_export_async(
        s_.host, std::move(export_request), std::move(prepare_options),
        [selected_dataset_ids, clean_output_dir, state, data_format](const DatasetExportSource     &source,
                                                                     DataOperationWorkflow::Result &result)
        {
            std::map<int64_t, size_t>            state_index_by_dataset;
            std::map<int64_t, std::set<int64_t>> class_ids_by_dataset;

            for (const int64_t dataset_id : selected_dataset_ids)
            {
                if (state_index_by_dataset.contains(dataset_id))
                {
                    continue;
                }

                const QString dataset_name = source.datasetName(dataset_id);
                if (dataset_name.isEmpty())
                {
                    spdlog::warn("跳过不存在的数据集: {}", dataset_id);
                    addProgressMessage(spdlog::level::warn, QString("跳过不存在的数据集: %1").arg(dataset_id));
                    continue;
                }

                ExportBatchItem item;
                item.dataset.dataset_id   = dataset_id;
                item.dataset.dataset_name = dataset_name;
                item.output_dir           = QDir(clean_output_dir).filePath(dataset_name);
                QString directory_error;
                if (!ensureDirectory(item.output_dir, directory_error))
                {
                    ++state->failed_count;
                    addProgressMessage(spdlog::level::err, directory_error);
                    continue;
                }

                state_index_by_dataset.emplace(dataset_id, state->items.size());
                state->items.push_back(std::move(item));
            }

            const std::vector<int64_t> image_ids = source.allImageIds();
            std::map<int64_t, size_t>  image_index_by_id;
            for (const int64_t image_id : image_ids)
            {
                const int64_t dataset_id = source.imageDatasetId(image_id);
                const auto    state_it   = state_index_by_dataset.find(dataset_id);
                if (state_it == state_index_by_dataset.end())
                {
                    continue;
                }

                const QString image_path = source.imagePath(image_id);
                if (image_path.isEmpty())
                {
                    continue;
                }

                ExportImage image;
                image.dataset_id = dataset_id;
                image.image_id   = image_id;
                image.path       = image_path;
                DatasetIO::getImageDimensions(image.path, image.width, image.height);

                auto &dataset = state->items[state_it->second].dataset;
                image_index_by_id.emplace(image_id, state_it->second);
                dataset.images.push_back(std::move(image));
            }

            for (const auto &[image_id, state_index] : image_index_by_id)
            {
                auto &dataset = state->items[state_index].dataset;
                for (const int64_t label_id : source.imageLabelIds(image_id))
                {
                    const int64_t label_class_id = source.labelClassId(label_id);
                    ExportLabel   label;
                    label.label_id       = label_id;
                    label.image_id       = image_id;
                    label.label_class_id = label_class_id;
                    label.data           = source.labelData(label_id);
                    dataset.labels.push_back(std::move(label));
                    class_ids_by_dataset[dataset.dataset_id].insert(label_class_id);
                }
            }

            for (auto &item : state->items)
            {
                for (const int64_t class_id : class_ids_by_dataset[item.dataset.dataset_id])
                {
                    item.dataset.label_classes.push_back(
                        ExportLabelClass{class_id, source.labelClassName(class_id), source.labelClassColor(class_id)});
                }
                if (item.dataset.images.empty())
                {
                    addProgressMessage(spdlog::level::warn, QString("跳过空数据集: %1").arg(item.dataset.dataset_name));
                }
            }

            state->items.erase(std::remove_if(state->items.begin(), state->items.end(),
                                              [](const ExportBatchItem &item) { return item.dataset.images.empty(); }),
                               state->items.end());

            for (const auto &item : state->items)
            {
                QString collision_err;
                if (!DataIO::checkExportSourceCollision(item.dataset, item.output_dir, data_format, collision_err))
                {
                    result.success = false;
                    result.error   = collision_err;
                    return;
                }
            }

            result.success = !state->items.empty();
            if (!result.success)
            {
                result.error = QString("没有可导出的数据集");
            }
        },
        [this, state, data_format, format_name, cancel_token, options](const DataOperationWorkflow::Result &result) mutable
        {
            const bool was_cancelled = (cancel_token && cancel_token->load()) || result.cancelled;
            if (s_.isShuttingDown() || was_cancelled || !result.success || state->items.empty())
            {
                s_.set_data_operation_running(false);
                const QString message = was_cancelled
                                            ? QStringLiteral("导出已取消")
                                            : (result.error.isEmpty() ? QStringLiteral("没有可导出的数据集") : result.error);
                const QString completed_message = QStringLiteral("%1，%2，耗时 %3 ms")
                                                      .arg(message)
                                                      .arg(state->dataset_summary)
                                                      .arg(state->elapsed_timer.elapsed());
                if (was_cancelled)
                {
                    spdlog::warn("导出已取消: 格式={}, {}", format_name.toUtf8().constData(),
                                 completed_message.toUtf8().constData());
                    addProgressMessage(spdlog::level::warn, completed_message, state->task_id);
                    ui::SignalHelper::notifyWarn(QStringLiteral("导出已取消"), completed_message);
                }
                else
                {
                    spdlog::error("导出失败: 格式={}, {}", format_name.toUtf8().constData(),
                                  completed_message.toUtf8().constData());
                    addProgressMessage(spdlog::level::err, completed_message, state->task_id);
                    ui::SignalHelper::notifyError(QStringLiteral("导出失败"), completed_message);
                }
                QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "finishTask", Qt::QueuedConnection,
                                          Q_ARG(QString, state->task_id), Q_ARG(bool, false));
                return;
            }

            // runDatasetExportAsync 只负责准备阶段；导出器完成前继续保持数据写入阻断。
            s_.set_data_operation_running(true);
            auto                                 start_next      = std::make_shared<std::function<void()>>();
            std::weak_ptr<std::function<void()>> weak_start_next = start_next;
            *start_next = [this, data_format, format_name, options, state, cancel_token, weak_start_next]()
            {
                if (s_.isShuttingDown() || (cancel_token && cancel_token->load()))
                {
                    s_.set_data_operation_running(false);
                    const int     remaining_items = static_cast<int>(state->items.size()) - state->current;
                    const QString message         = QStringLiteral("导出已取消: 成功 %1 个, 失败 %2 个, 未执行 %3 个，%4，耗时 %5 ms")
                                                .arg(state->success_count)
                                                .arg(state->failed_count)
                                                .arg(std::max(0, remaining_items))
                                                .arg(state->dataset_summary)
                                                .arg(state->elapsed_timer.elapsed());
                    spdlog::warn("导出已取消: 格式={}, {}", format_name.toUtf8().constData(),
                                 message.toUtf8().constData());
                    addProgressMessage(spdlog::level::warn, message, state->task_id);
                    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "finishTask", Qt::QueuedConnection,
                                              Q_ARG(QString, state->task_id), Q_ARG(bool, false));
                    ui::SignalHelper::notifyWarn(QStringLiteral("导出已取消"), message);
                    return;
                }

                if (state->current >= static_cast<int>(state->items.size()))
                {
                    const bool    success = state->success_count > 0 && state->failed_count == 0;
                    const QString message = QStringLiteral("导出完成: 成功 %1 个, 失败 %2 个，%3，耗时 %4 ms")
                                                .arg(state->success_count)
                                                .arg(state->failed_count)
                                                .arg(state->dataset_summary)
                                                .arg(state->elapsed_timer.elapsed());
                    s_.set_data_operation_running(false);
                    const int level = state->failed_count == 0 ? spdlog::level::info : spdlog::level::warn;
                    spdlog::log(static_cast<spdlog::level::level_enum>(level), "导出结束: 格式={}, {}",
                                format_name.toUtf8().constData(), message.toUtf8().constData());
                    addProgressMessage(level, message, state->task_id);
                    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "finishTask", Qt::QueuedConnection,
                                              Q_ARG(QString, state->task_id), Q_ARG(bool, success));
                    if (success)
                        ui::SignalHelper::notifySuccess(QStringLiteral("导出完成"), message);
                    else if (state->success_count > 0)
                        ui::SignalHelper::notifyWarn(QStringLiteral("导出完成"), message);
                    else
                        ui::SignalHelper::notifyError(QStringLiteral("导出失败"), message);
                    return;
                }

                const int       item_index    = state->current;
                const int       total_items   = static_cast<int>(state->items.size());
                ExportBatchItem item          = std::move(state->items[static_cast<size_t>(state->current++)]);
                const int       dataset_index = state->current;
                state->dataset_elapsed_timer.restart();
                if (total_items <= 3)
                {
                    const QString message
                        = QStringLiteral("开始导出数据集: %1 -> %2").arg(item.dataset.dataset_name, item.output_dir);
                    spdlog::info("{}", message.toUtf8().constData());
                    addProgressMessage(spdlog::level::info, message, state->task_id);
                }
                else
                {
                    addProgressMessage(spdlog::level::info,
                                       QStringLiteral("正在导出数据集 %1/%2: %3")
                                           .arg(dataset_index)
                                           .arg(total_items)
                                           .arg(item.dataset.dataset_name),
                                       state->task_id);
                }

                DataIO *exporter = DataIO::createIO(data_format, s_.host);
                if (!exporter)
                {
                    addProgressMessage(spdlog::level::err, QStringLiteral("不支持的数据格式"), state->task_id);
                    ++state->failed_count;
                    if (auto next = weak_start_next.lock())
                    {
                        (*next)();
                    }
                    return;
                }
                exporter->setTargetMethod(s_.method);
                exporter->setTaskId(state->task_id);

                const int range_min = item_index * 100 / total_items;
                const int range_max = (item_index + 1) * 100 / total_items;
                exporter->setProgressRange(range_min, range_max);

                QObject::connect(
                    exporter, &DataIO::exportFinished, s_.host,
                    [this, exporter, state, cancel_token, dataset_name = item.dataset.dataset_name,
                     start_next = weak_start_next.lock()](
                        bool success, const QString &message)
                    {
                        if (s_.isShuttingDown())
                        {
                            exporter->deleteLater();
                            return;
                        }

                        const bool was_cancelled = (cancel_token && cancel_token->load())
                                                   || message.contains(QStringLiteral("取消"));
                        if (was_cancelled && cancel_token)
                        {
                            cancel_token->store(true);
                        }

                        if (success)
                            ++state->success_count;
                        else if (!was_cancelled)
                            ++state->failed_count;

                        const qint64 dataset_elapsed_ms
                            = state->dataset_elapsed_timer.isValid() ? state->dataset_elapsed_timer.elapsed() : 0;
                        const QString completed_message = QStringLiteral("数据集 %1：%2，耗时 %3 ms")
                                                              .arg(dataset_name)
                                                              .arg(message)
                                                              .arg(dataset_elapsed_ms);
                        if (state->items.size() <= 3)
                        {
                            if (success)
                                spdlog::info("导出数据集结束: {}", completed_message.toUtf8().constData());
                            else if (was_cancelled)
                                spdlog::warn("导出数据集已取消: {}", completed_message.toUtf8().constData());
                            else
                                spdlog::error("导出数据集失败: {}", completed_message.toUtf8().constData());
                        }
                        addProgressMessage(success ? spdlog::level::info
                                                   : (was_cancelled ? spdlog::level::warn : spdlog::level::err),
                                           completed_message, state->task_id);
                        exporter->deleteLater();
                        if (start_next)
                            (*start_next)();
                    },
                    Qt::QueuedConnection);

                exporter->startExport(std::move(item.dataset), item.output_dir, options);
            };

            (*start_next)();
        });
}

} // namespace dltool::data
