#include "feature/ImageClusterController.h"

#include "SearchControllerUtils.h"
#include "data/DataManager.h"
#include "feature/Utils.h"
#include "settings/GlobalSettings.h"
#include "ui/ProgressManager.h"
#include "ui/SignalHelper.h"

#include <spdlog/spdlog.h>

#include <QFileInfo>
#include <QMetaObject>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <chrono>
#include <functional>
#include <limits>
#include <memory>

namespace dltool::feature {
namespace {

using AccessorKey = dltool::settings::generated::AccessorKey;

constexpr AccessorKey kSettingsAccessor = AccessorKey::ImageCluster;

int imageClusterProgressPercent(const irt::features::ImageClusterProgress &progress, size_t total_count)
{
    using Stage = irt::features::ImageClusterStage;
    switch (progress.stage)
    {
    case Stage::LoadingModel:
        return progress.processed_count > 0 ? 5 : 1;
    case Stage::ExtractingFeatures:
    {
        const size_t total = progress.total_count > 0 ? progress.total_count : total_count;
        if (total == 0)
            return 5;
        const size_t processed = std::min(total, progress.processed_count);
        return 5 + static_cast<int>(processed * 85 / total);
    }
    case Stage::Clustering:
        return progress.processed_count > 0 ? 98 : 92;
    case Stage::Unknown:
    default:
        return -1;
    }
}

QString imageClusterProgressMessage(const irt::features::ImageClusterProgress &progress, size_t total_count)
{
    if (progress.stage == irt::features::ImageClusterStage::Unknown)
        return {};

    const QString stage_name = QString::fromUtf8(irt::features::imageClusterStageName(progress.stage));
    const size_t  total      = progress.total_count > 0 ? progress.total_count : total_count;
    if (total > 0 && progress.stage == irt::features::ImageClusterStage::ExtractingFeatures)
        return QString("聚类进度 [%1]: %2 / %3").arg(stage_name).arg(progress.processed_count).arg(total);

    return QString("聚类阶段 [%1]").arg(stage_name);
}

QString clusterTargetDatasetName(const QString &source_dataset_name, const int64_t cluster_id)
{
    if (cluster_id < 0)
        return QString("%1-noise").arg(source_dataset_name);
    return QString("%1-%2").arg(source_dataset_name).arg(cluster_id);
}

size_t plannedImageCount(const std::map<int64_t, std::vector<int64_t>> &image_ids_by_target_dataset)
{
    size_t count = 0;
    for (const auto &[_, image_ids] : image_ids_by_target_dataset)
    {
        count += image_ids.size();
    }
    return count;
}

QString clusterSummary(const QString &base_summary, size_t moved_image_count, size_t copied_image_count,
                       size_t target_dataset_count, size_t skipped_noise_count, bool copy_mode)
{
    const size_t  applied_count = moved_image_count + copied_image_count;
    const QString action        = copy_mode ? QString("复制") : QString("移动");
    QString       summary       = QString("%1，已%2 %3 张图像到 %4 个数据集")
                          .arg(base_summary)
                          .arg(action)
                          .arg(static_cast<qlonglong>(applied_count))
                          .arg(static_cast<qlonglong>(target_dataset_count));
    if (skipped_noise_count > 0)
    {
        summary += QString("，跳过噪声 %1 张").arg(static_cast<qlonglong>(skipped_noise_count));
    }
    return summary;
}

} // namespace

ImageClusterController::ImageClusterController(ImageClusterDataProvider  *data_provider,
                                               dltool::data::DataManager *data_manager, QObject *parent)
    : ImageClusterController(data_provider, data_manager, nullptr, parent)
{
}

ImageClusterController::ImageClusterController(ImageClusterDataProvider  *data_provider,
                                               dltool::data::DataManager *data_manager,
                                               ClusterExecutor            executor,
                                               QObject                   *parent)
    : QObject(parent)
    , data_provider_(data_provider)
    , data_manager_(data_manager)
    , custom_executor_(std::move(executor))
{
    auto *gs = dltool::settings::GlobalSettings::getInstance();
    enabled_ = searchSettingsEnabled(gs, kSettingsAccessor);

    const QString watched_group = dltool::settings::toQString(dltool::settings::generated::groupKey(kSettingsAccessor));
    connect(gs->catalog(), &dltool::settings::SettingsCatalog::fieldValueChanged, this,
            [this, watched_group](const QString &group_key, const QString &name, const QVariant &value)
            {
                if (group_key == watched_group && name == QStringLiteral("enabled"))
                {
                    const bool v = value.toBool();
                    if (v != enabled_)
                    {
                        enabled_ = v;
                        emit enabledChanged();
                    }
                }
            });
}

ImageClusterController::~ImageClusterController()
{
    shutdown();
}

void ImageClusterController::shutdown()
{
    if (shutting_down_.exchange(true, std::memory_order_acq_rel))
        return;

    ++current_request_id_;
    if (cancellation_token_ != nullptr)
        cancellation_token_->store(true, std::memory_order_release);

    const bool was_running = running_;

    QThread *thread = worker_thread_.data();
    if (thread != nullptr)
    {
        thread->wait();
        delete thread;
    }
    worker_thread_ = nullptr;

    // A completed cluster may already have queued copy/move operations in the
    // DataManager.  Wait for those operations after the worker has stopped so
    // the controller cannot be released while their callbacks still capture it.
    if (data_manager_ != nullptr)
        data_manager_->waitForOperations();
    setRunning(false);
    if (was_running && !current_cluster_task_id_.isEmpty())
    {
        ui::ProgressManager::getInstance()->finishTask(current_cluster_task_id_, false);
        current_cluster_task_id_.clear();
    }
}

bool ImageClusterController::enabled() const
{
    return enabled_;
}

bool ImageClusterController::isRunning() const
{
    return running_;
}

bool ImageClusterController::hasResults() const
{
    return result_count_ > 0;
}

int ImageClusterController::resultCount() const
{
    return result_count_;
}

QString ImageClusterController::lastError() const
{
    return last_error_;
}

QString ImageClusterController::lastSummary() const
{
    return last_summary_;
}

QString ImageClusterController::validationError() const
{
    if (shutting_down_.load(std::memory_order_acquire))
        return QStringLiteral("图像聚类控制器正在关闭");
    if (running_)
    {
        return QString("图像聚类正在运行");
    }
    if (data_provider_ == nullptr)
    {
        return QString("图像聚类模型未初始化");
    }
    if (data_manager_ == nullptr)
    {
        return QString("数据管理器未初始化");
    }

    const auto *settings = dltool::settings::GlobalSettings::getInstance();
    if (settings == nullptr || settings->settingsGroup(kSettingsAccessor) == nullptr)
    {
        return QString("图像聚类设置未加载");
    }
    if (!searchSettingsEnabled(settings, kSettingsAccessor))
    {
        return QString("图像聚类未启用");
    }

    ClusterRequest request;
    buildClusterRequest(request);
    return clusterRequestValidationError(request);
}

bool ImageClusterController::cluster(const QVariantList &dataset_ids)
{
    if (shutting_down_.load(std::memory_order_acquire))
    {
        setLastError(QStringLiteral("图像聚类控制器正在关闭"));
        return false;
    }
    if (running_)
    {
        setLastError(QString("图像聚类正在运行"));
        return false;
    }
    if (data_provider_ == nullptr)
    {
        setLastError(QString("图像聚类模型未初始化"));
        return false;
    }
    if (data_manager_ == nullptr)
    {
        setLastError(QString("数据管理器未初始化"));
        return false;
    }

    const auto scope = parseDatasetClassScope(dataset_ids);
    if (scope.empty())
    {
        setLastError(QString("请选择要聚类的数据集"));
        return false;
    }

    const auto *settings = dltool::settings::GlobalSettings::getInstance();
    if (settings == nullptr || settings->settingsGroup(kSettingsAccessor) == nullptr)
    {
        setLastError(QString("图像聚类设置未加载"));
        return false;
    }
    if (!searchSettingsEnabled(settings, kSettingsAccessor))
    {
        setLastError(QString("图像聚类未启用"));
        return false;
    }

    ClusterRequest request;
    buildClusterRequest(request);
    if (!validateClusterRequest(request))
        return false;

    collectClusterItems(request, scope);
    if (request.items.empty())
    {
        setLastError(QString("选定范围内没有可聚类图像"));
        return false;
    }
    if (request.items.size() <= 1)
    {
        setLastError(QString("图像聚类至少需要 2 张图像"));
        return false;
    }

    const uint64_t request_id = ++current_request_id_;
    request.request_id         = request_id;
    request.weights_file       = QFileInfo(request.weights_file).absoluteFilePath();
    request.started_at         = std::chrono::steady_clock::now();
    if (cancellation_token_ != nullptr)
        cancellation_token_->store(true, std::memory_order_release);
    request.cancellation_token = std::make_shared<std::atomic_bool>(false);
    cancellation_token_        = request.cancellation_token;

    resetForNewCluster();
    startProgress(request);

    const auto controller = QPointer<ImageClusterController>(this);
    const auto run_token  = request.cancellation_token;
    const auto progress   = createProgressReporter(controller, request.items.size(), run_token);
    const auto complete   = [controller, run_token, request_id](const ClusterResponse &response)
    {
        if (!controller || !run_token || run_token->load(std::memory_order_acquire))
            return;
        QMetaObject::invokeMethod(
            controller.data(),
            [controller, run_token, request_id, response]()
            {
                if (controller && run_token && !run_token->load(std::memory_order_acquire)
                    && !controller->shutting_down_.load(std::memory_order_acquire)
                    && request_id == controller->current_request_id_.load(std::memory_order_acquire))
                    controller->finishCluster(response);
            },
            Qt::QueuedConnection);
    };
    const auto executor = custom_executor_ ? custom_executor_ : &ImageClusterController::executeCluster;

    QThread *work_thread = QThread::create(
        [request = std::move(request), executor, progress, complete]() mutable
        {
            ClusterResponse response;
            response.request_id                  = request.request_id;
            response.include_noise               = request.include_noise;
            response.apply_mode                  = request.apply_mode;
            response.frozen_items                = request.frozen_items;
            response.frozen_source_dataset_names = request.frozen_source_dataset_names;
            response.frozen_image_source_dataset = request.frozen_image_source_dataset;
            executor(request, response, progress);

            response.elapsed_ms = static_cast<qint64>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                          std::chrono::steady_clock::now() - request.started_at)
                                                          .count());
            complete(response);
        });

    connect(work_thread, &QThread::finished, work_thread, &QObject::deleteLater);
    worker_thread_ = work_thread;
    work_thread->start();
    return true;
}

void ImageClusterController::buildClusterRequest(ClusterRequest &request) const
{
    const auto settings = readImageClusterSettings(dltool::settings::GlobalSettings::getInstance());
    request.weights_file
        = settings.base.weights_file.isEmpty() ? QString() : QFileInfo(settings.base.weights_file).absoluteFilePath();
    request.include_noise = settings.include_noise;
    request.apply_mode    = settings.apply_mode == static_cast<int>(ImageClusterApplyMode::Copy)
                              ? ImageClusterApplyMode::Copy
                              : ImageClusterApplyMode::Move;
    applyImageClusterConfig(request.config, settings);
}

bool ImageClusterController::validateClusterRequest(const ClusterRequest &request)
{
    const QString error = clusterRequestValidationError(request);
    if (!error.isEmpty())
    {
        setLastError(error);
        return false;
    }
    return true;
}

QString ImageClusterController::clusterRequestValidationError(const ClusterRequest &request) const
{
    if (QString::fromStdString(request.config.model_name).trimmed().isEmpty())
    {
        return QString("请先配置图像聚类模型");
    }
    if (QString::fromStdString(request.config.feature_name).trimmed().isEmpty())
    {
        return QString("请先配置图像聚类特征层");
    }

    QFileInfo weights_info(request.weights_file);
    if (request.weights_file.trimmed().isEmpty() || !weights_info.isFile())
    {
        return QString("模型权重文件不存在: %1").arg(request.weights_file);
    }
    return {};
}

void ImageClusterController::collectClusterItems(ClusterRequest                             &request,
                                                 const std::map<int64_t, std::set<int64_t>> &scope)
{
    for (const auto &[dataset_id, _] : scope)
    {
        QString dataset_name = data_provider_->datasetName(dataset_id);
        if (dataset_name.isEmpty() && data_manager_ != nullptr)
        {
            dataset_name = data_manager_->getDatasetName(static_cast<int>(dataset_id));
        }
        if (!dataset_name.isEmpty())
        {
            request.frozen_source_dataset_names[dataset_id] = dataset_name;
        }
    }

    auto appendImage = [this, &request](const int64_t image_id, const int64_t source_dataset_id)
    {
        const QString path = data_provider_->imagePath(image_id);
        QFileInfo     info(path);
        if (!info.isFile())
            return;

        const auto name_it = request.frozen_source_dataset_names.find(source_dataset_id);
        const QString source_name = (name_it != request.frozen_source_dataset_names.end())
                                        ? name_it->second : QStringLiteral("Dataset");

        request.items.push_back({image_id, toFsPath(info.absoluteFilePath())});
        request.frozen_items.push_back({image_id, source_dataset_id, source_name, toFsPath(info.absoluteFilePath())});
        request.frozen_image_source_dataset[image_id] = source_dataset_id;
    };

    for (const int64_t image_id : data_provider_->allImageIds())
    {
        const int64_t dataset_id = data_provider_->imageDatasetId(image_id);
        const auto    scope_it   = scope.find(dataset_id);
        if (scope_it == scope.end())
            continue;

        if (!scope_it->second.empty())
        {
            bool       matches_label_class = false;
            const auto label_ids           = data_provider_->imageLabelIds(image_id);
            if (!label_ids.empty())
            {
                for (const int64_t label_id : label_ids)
                {
                    if (scope_it->second.find(data_provider_->labelClassId(label_id)) != scope_it->second.end())
                    {
                        matches_label_class = true;
                        break;
                    }
                }
            }
            else
            {
                matches_label_class
                    = scope_it->second.find(data_provider_->imageLabelClassId(image_id)) != scope_it->second.end();
            }

            if (!matches_label_class)
                continue;
        }
        appendImage(image_id, dataset_id);
    }
}

void ImageClusterController::executeCluster(const ClusterRequest &request, ClusterResponse &response,
                                            const irt::features::ImageClusterProgressCallback &progress)
{
    try
    {
        if (request.cancellationRequested())
            return;

        addProgressMessage(spdlog::level::info, QString("正在抽取图像特征并聚类: %1 张图像").arg(request.items.size()));

        irt::features::ImageCluster cluster(request.config);
        const auto                  result = cluster.cluster(toFsPath(request.weights_file), request.items,
                                                             progress);

        if (request.cancellationRequested())
        {
            response.error = QStringLiteral("图像聚类已取消");
            return;
        }

        response.assignments.reserve(result.assignments.size());
        for (const auto &assignment : result.assignments)
        {
            response.assignments.push_back({assignment.image_id, assignment.cluster_id, assignment.probability});
        }

        response.success       = true;
        response.feature_dim   = result.feature_dim;
        response.cluster_count = result.cluster_count;
        response.noise_count   = result.noise_count;
        response.summary       = QString("图像聚类完成: %1 张图像, %2 个簇, 噪声 %3 张")
                               .arg(response.assignments.size())
                               .arg(static_cast<qlonglong>(response.cluster_count))
                               .arg(static_cast<qlonglong>(response.noise_count));
    }
    catch (const std::exception &e)
    {
        response.success = false;
        response.error   = QString(e.what());
    }
    catch (...)
    {
        response.success = false;
        response.error   = QString("未知图像聚类错误");
    }
}

void ImageClusterController::resetForNewCluster()
{
    setLastError(QString());
    last_summary_.clear();
    result_count_ = 0;
    emit resultsChanged();
}

void ImageClusterController::startProgress(const ClusterRequest &request)
{
    setRunning(true);
    current_cluster_task_id_ = QStringLiteral("image_cluster_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    ui::ProgressManager::getInstance()->startTask(QString("图像聚类"), current_cluster_task_id_);
    addProgressMessage(spdlog::level::info, QString("开始图像聚类: %1 张图像").arg(request.items.size()));
}

void ImageClusterController::finishProgress(bool success, const QString &message)
{
    const int level = success ? spdlog::level::info : spdlog::level::err;
    addProgressMessage(level, message);
    ui::ProgressManager::getInstance()->finishTask(current_cluster_task_id_, success);
    current_cluster_task_id_.clear();
}

void ImageClusterController::finishCluster(const ClusterResponse &response)
{
    if (shutting_down_.load(std::memory_order_acquire))
        return;

    if (response.request_id != current_request_id_.load(std::memory_order_acquire))
        return;

    if (!response.success)
    {
        setRunning(false);
        result_count_ = 0;
        last_summary_.clear();
        emit resultsChanged();
        setLastError(response.error);
        spdlog::error("图像聚类失败: {}, 耗时 {}", response.error.toUtf8().constData(),
                      formatElapsed(response.elapsed_ms).toUtf8().constData());
        finishProgress(false, QString("%1, 耗时 %2").arg(response.error, formatElapsed(response.elapsed_ms)));
        ui::SignalHelper::notifyError(QString("图像聚类失败"), response.error);
        return;
    }

    if (data_manager_ == nullptr || data_provider_ == nullptr)
    {
        const QString err = QStringLiteral("数据管理器未初始化");
        setRunning(false);
        result_count_ = 0;
        last_summary_.clear();
        emit resultsChanged();
        setLastError(err);
        finishProgress(false, err);
        ui::SignalHelper::notifyError(QString("图像聚类失败"), err);
        return;
    }

    std::map<QString, std::vector<int64_t>> target_groups;
    size_t skipped_noise_count = 0;

    for (const auto &assignment : response.assignments)
    {
        if (assignment.cluster_id < 0 && !response.include_noise)
        {
            ++skipped_noise_count;
            continue;
        }

        const auto ds_it = response.frozen_image_source_dataset.find(assignment.image_id);
        if (ds_it == response.frozen_image_source_dataset.end())
        {
            continue;
        }

        const int64_t expected_dataset_id = ds_it->second;
        const int64_t current_dataset_id  = data_provider_->imageDatasetId(assignment.image_id);
        if (current_dataset_id != expected_dataset_id)
        {
            const QString conflict_err = QString("聚类写回冲突: 图像 %1 已被移动或删除").arg(assignment.image_id);
            setRunning(false);
            result_count_ = 0;
            last_summary_.clear();
            emit resultsChanged();
            setLastError(conflict_err);
            spdlog::error("图像聚类写回冲突: {}", conflict_err.toUtf8().constData());
            finishProgress(false, QString("%1, 耗时 %2").arg(conflict_err, formatElapsed(response.elapsed_ms)));
            ui::SignalHelper::notifyError(QString("图像聚类失败"), conflict_err);
            return;
        }

        const auto name_it = response.frozen_source_dataset_names.find(expected_dataset_id);
        if (name_it == response.frozen_source_dataset_names.end() || name_it->second.isEmpty())
        {
            const QString err = QString("无法找到聚类源数据集名称: id=%1").arg(expected_dataset_id);
            setRunning(false);
            result_count_ = 0;
            last_summary_.clear();
            emit resultsChanged();
            setLastError(err);
            finishProgress(false, err);
            ui::SignalHelper::notifyError(QString("图像聚类失败"), err);
            return;
        }

        const QString target_name = clusterTargetDatasetName(name_it->second, assignment.cluster_id);
        target_groups[target_name].push_back(assignment.image_id);
    }

    if (target_groups.empty())
    {
        setRunning(false);
        setLastError(QString());
        result_count_ = 0;
        last_summary_ = clusterSummary(response.summary, 0, 0, 0, skipped_noise_count,
                                       response.apply_mode == ImageClusterApplyMode::Copy);
        spdlog::info("图像聚类完成: {}, 耗时 {}", last_summary_.toUtf8().constData(),
                     formatElapsed(response.elapsed_ms).toUtf8().constData());
        finishProgress(true, QString("%1, 耗时 %2").arg(last_summary_, formatElapsed(response.elapsed_ms)));
        ui::SignalHelper::notifySuccess(QString("图像聚类完成"), last_summary_);
        emit resultsChanged();
        return;
    }

    dltool::data::DataManager::ClusterWritebackRequest writeback_req;
    writeback_req.is_copy = (response.apply_mode == ImageClusterApplyMode::Copy);
    writeback_req.targets.reserve(target_groups.size());
    for (auto &[target_name, img_ids] : target_groups)
    {
        dltool::data::DataManager::ClusterTargetData target_data;
        target_data.target_dataset_name = target_name;
        target_data.image_ids           = std::move(img_ids);
        writeback_req.targets.push_back(std::move(target_data));
    }

    const uint64_t request_id    = response.request_id;
    const auto     response_copy = response;

    const bool started = data_manager_->writebackClusterAsync(
        writeback_req, this,
        [this, response_copy, request_id, skipped_noise_count](const dltool::data::DataManager::ClusterWritebackResult &wb_result)
        {
            if (shutting_down_.load(std::memory_order_acquire))
                return;

            if (request_id != current_request_id_.load(std::memory_order_acquire))
                return;

            setRunning(false);

            if (!wb_result.success)
            {
                result_count_ = 0;
                last_summary_.clear();
                emit resultsChanged();
                const QString err = wb_result.error.isEmpty() ? QStringLiteral("聚类写回失败") : wb_result.error;
                setLastError(err);
                spdlog::error("图像聚类失败: {}, 耗时 {}", err.toUtf8().constData(),
                              formatElapsed(response_copy.elapsed_ms).toUtf8().constData());
                finishProgress(false, QString("%1, 耗时 %2").arg(err, formatElapsed(response_copy.elapsed_ms)));
                ui::SignalHelper::notifyError(QString("图像聚类失败"), err);
                return;
            }

            const size_t total_applied = (response_copy.apply_mode == ImageClusterApplyMode::Copy)
                                             ? wb_result.copied_image_count
                                             : wb_result.moved_image_count;

            setLastError(QString());
            result_count_ = static_cast<int>(std::min<size_t>(total_applied,
                                                              static_cast<size_t>(std::numeric_limits<int>::max())));
            last_summary_ = clusterSummary(response_copy.summary, wb_result.moved_image_count, wb_result.copied_image_count,
                                           wb_result.target_dataset_count, skipped_noise_count,
                                           response_copy.apply_mode == ImageClusterApplyMode::Copy);

            spdlog::info("图像聚类完成: {}, 耗时 {}", last_summary_.toUtf8().constData(),
                         formatElapsed(response_copy.elapsed_ms).toUtf8().constData());
            finishProgress(true, QString("%1, 耗时 %2").arg(last_summary_, formatElapsed(response_copy.elapsed_ms)));
            ui::SignalHelper::notifySuccess(QString("图像聚类完成"), last_summary_);
            emit resultsChanged();
        });

    if (!started)
    {
        setRunning(false);
        result_count_ = 0;
        last_summary_.clear();
        emit resultsChanged();
        const QString err = QStringLiteral("无法启动聚类写回事务");
        setLastError(err);
        finishProgress(false, err);
        ui::SignalHelper::notifyError(QString("图像聚类失败"), err);
    }
}

irt::features::ImageClusterProgressCallback ImageClusterController::createProgressReporter(
    QPointer<ImageClusterController> controller, const size_t total_count,
    std::shared_ptr<std::atomic_bool> cancellation_token)
{
    return [controller, total_count, cancellation_token](const irt::features::ImageClusterProgress &progress)
    {
        if (!controller || !cancellation_token
            || cancellation_token->load(std::memory_order_acquire)
            || controller->shutting_down_.load(std::memory_order_acquire))
            return;

        if (progress.total_count > 0)
        {
            const int processed = static_cast<int>(
                std::min<size_t>(progress.processed_count, static_cast<size_t>(std::numeric_limits<int>::max())));
            const int total = static_cast<int>(
                std::min<size_t>(progress.total_count, static_cast<size_t>(std::numeric_limits<int>::max())));
            QMetaObject::invokeMethod(
                controller.data(),
                [controller, cancellation_token, processed, total]()
                {
                    if (controller && cancellation_token
                        && !cancellation_token->load(std::memory_order_acquire)
                        && !controller->shutting_down_.load(std::memory_order_acquire))
                        emit controller->buildProgressChanged(processed, total);
                },
                Qt::QueuedConnection);
        }

        const int pct = imageClusterProgressPercent(progress, total_count);
        if (pct >= 0)
        {
            QMetaObject::invokeMethod(
                controller.data(),
                [controller, cancellation_token, pct]()
                {
                    if (controller && cancellation_token
                        && !cancellation_token->load(std::memory_order_acquire)
                        && !controller->shutting_down_.load(std::memory_order_acquire))
                        ui::ProgressManager::getInstance()->updateProgress(pct);
                },
                Qt::QueuedConnection);
        }

        const QString message = imageClusterProgressMessage(progress, total_count);
        if (!message.isEmpty() && !cancellation_token->load(std::memory_order_acquire)
            && !controller->shutting_down_.load(std::memory_order_acquire))
            addProgressMessage(spdlog::level::info, message);
    };
}

void ImageClusterController::setRunning(bool running)
{
    if (running_ == running)
        return;
    running_ = running;
    emit runningChanged();
}

void ImageClusterController::setLastError(const QString &last_error)
{
    if (last_error_ == last_error)
        return;
    last_error_ = last_error;
    if (!last_error_.isEmpty())
        spdlog::error("图像聚类失败: {}", last_error_.toUtf8().constData());
    emit lastErrorChanged();
}

} // namespace dltool::feature
