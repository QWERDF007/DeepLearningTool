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
#include <algorithm>
#include <chrono>
#include <limits>

namespace dltool::feature {
namespace {

using AccessorKey = dltool::settings::generated::AccessorKey;

constexpr AccessorKey kSettingsAccessor = AccessorKey::ImageCluster;

int imageClusterProgressPercent(const irt::features::ImageClusterProgress &progress, size_t total_count)
{
    using Stage = irt::features::ImageClusterStage;
    switch (progress.stage)
    {
    case Stage::Started:
        return 0;
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
    case Stage::Finished:
        return 100;
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
        return QStringLiteral("聚类进度 [%1]: %2 / %3")
            .arg(stage_name)
            .arg(progress.processed_count)
            .arg(total);

    return QStringLiteral("聚类阶段 [%1]").arg(stage_name);
}

QString clusterTargetDatasetName(const QString &source_dataset_name, const int64_t cluster_id)
{
    if (cluster_id < 0)
        return QStringLiteral("%1-noise").arg(source_dataset_name);
    return QStringLiteral("%1-%2").arg(source_dataset_name).arg(cluster_id);
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

QString clusterSummary(const QString &base_summary,
                       size_t moved_image_count,
                       size_t copied_image_count,
                       size_t target_dataset_count,
                       size_t skipped_noise_count,
                       bool copy_mode)
{
    const size_t applied_count = moved_image_count + copied_image_count;
    const QString action       = copy_mode ? QStringLiteral("复制") : QStringLiteral("移动");
    QString summary = QStringLiteral("%1，已%2 %3 张图像到 %4 个数据集")
                          .arg(base_summary)
                          .arg(action)
                          .arg(static_cast<qlonglong>(applied_count))
                          .arg(static_cast<qlonglong>(target_dataset_count));
    if (skipped_noise_count > 0)
    {
        summary += QStringLiteral("，跳过噪声 %1 张").arg(static_cast<qlonglong>(skipped_noise_count));
    }
    return summary;
}

} // namespace

ImageClusterController::ImageClusterController(ImageClusterDataProvider *data_provider,
                                               dltool::data::DataManager *data_manager,
                                               QObject *parent)
    : QObject(parent)
    , data_provider_(data_provider)
    , data_manager_(data_manager)
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

bool ImageClusterController::clusterSelectedImages()
{
    if (data_provider_ == nullptr)
    {
        setLastError(QStringLiteral("图像聚类模型未初始化"));
        return false;
    }
    if (data_manager_ == nullptr)
    {
        setLastError(QStringLiteral("数据管理器未初始化"));
        return false;
    }

    const auto selected_ids = data_provider_->selectedImageIds();
    if (selected_ids.empty())
    {
        setLastError(QStringLiteral("请先选择要聚类的图像"));
        return false;
    }

    QVariantList ids;
    ids.reserve(static_cast<int>(selected_ids.size()));
    for (const int64_t id : selected_ids) ids.append(static_cast<qlonglong>(id));
    return cluster(ids, {});
}

bool ImageClusterController::cluster(const QVariantList &image_ids, const QVariantList &dataset_ids)
{
    if (running_)
    {
        setLastError(QStringLiteral("图像聚类正在运行"));
        return false;
    }
    if (data_provider_ == nullptr)
    {
        setLastError(QStringLiteral("图像聚类模型未初始化"));
        return false;
    }
    if (data_manager_ == nullptr)
    {
        setLastError(QStringLiteral("数据管理器未初始化"));
        return false;
    }

    const auto int_image_ids  = parseInt64Ids(image_ids, true, true);
    const auto dataset_id_set = parseDatasetIds(dataset_ids);
    if (int_image_ids.empty() && dataset_id_set.empty())
    {
        setLastError(QStringLiteral("请选择要聚类的数据集或图像"));
        return false;
    }

    const auto *settings = dltool::settings::GlobalSettings::getInstance();
    if (settings == nullptr || settings->settingsGroup(kSettingsAccessor) == nullptr)
    {
        setLastError(QStringLiteral("图像聚类设置未加载"));
        return false;
    }
    if (!searchSettingsEnabled(settings, kSettingsAccessor))
    {
        setLastError(QStringLiteral("图像聚类未启用"));
        return false;
    }

    ClusterRequest request;
    buildClusterRequest(request);
    if (!validateClusterRequest(request))
        return false;

    collectClusterItems(request, int_image_ids, dataset_id_set);
    if (request.items.empty())
    {
        setLastError(QStringLiteral("选定范围内没有可聚类图像"));
        return false;
    }
    if (request.items.size() <= 1)
    {
        setLastError(QStringLiteral("图像聚类至少需要 2 张图像"));
        return false;
    }

    request.weights_file = QFileInfo(request.weights_file).absoluteFilePath();
    request.started_at   = std::chrono::steady_clock::now();
    request.controller   = QPointer<ImageClusterController>(this);

    resetForNewCluster();
    startProgress(request);

    QThread *work_thread = QThread::create(
        [request = std::move(request)]() mutable
        {
            ClusterResponse response;
            response.include_noise = request.include_noise;
            response.apply_mode    = request.apply_mode;
            if (request.controller)
                request.controller->executeCluster(request, response);

            response.elapsed_ms = static_cast<qint64>(
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()
                                                                      - request.started_at)
                    .count());

            if (request.controller)
            {
                const auto ctrl = request.controller;
                QMetaObject::invokeMethod(
                    ctrl.data(),
                    [ctrl, response]()
                    {
                        if (ctrl)
                            ctrl->finishCluster(response);
                    },
                    Qt::QueuedConnection);
            }
        });

    connect(work_thread, &QThread::finished, work_thread, &QObject::deleteLater);
    work_thread->start();
    return true;
}

std::set<int64_t> ImageClusterController::parseDatasetIds(const QVariantList &dataset_ids)
{
    const auto ids = parseInt64Ids(dataset_ids, true, true);
    return std::set<int64_t>(ids.begin(), ids.end());
}

void ImageClusterController::buildClusterRequest(ClusterRequest &request)
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
    if (QString::fromStdString(request.config.model_name).trimmed().isEmpty())
    {
        setLastError(QStringLiteral("请先配置图像聚类模型"));
        return false;
    }
    if (QString::fromStdString(request.config.feature_name).trimmed().isEmpty())
    {
        setLastError(QStringLiteral("请先配置图像聚类特征层"));
        return false;
    }

    QFileInfo weights_info(request.weights_file);
    if (request.weights_file.trimmed().isEmpty() || !weights_info.isFile())
    {
        setLastError(QStringLiteral("模型权重文件不存在: %1").arg(request.weights_file));
        return false;
    }
    return true;
}

void ImageClusterController::collectClusterItems(ClusterRequest &request, const std::vector<int64_t> &image_ids,
                                                 const std::set<int64_t> &dataset_ids)
{
    auto appendImage = [this, &request](const int64_t image_id)
    {
        const QString path = data_provider_->imagePath(image_id);
        QFileInfo     info(path);
        if (!info.isFile())
            return;

        request.items.push_back({image_id, toFsPath(info.absoluteFilePath())});
    };

    if (!image_ids.empty())
    {
        for (const int64_t image_id : image_ids) appendImage(image_id);
        return;
    }

    for (const int64_t image_id : data_provider_->allImageIds())
    {
        const int64_t dataset_id = data_provider_->imageDatasetId(image_id);
        if (dataset_ids.find(dataset_id) == dataset_ids.end())
            continue;
        appendImage(image_id);
    }
}

void ImageClusterController::executeCluster(const ClusterRequest &request, ClusterResponse &response)
{
    const auto ctrl = request.controller;

    try
    {
        addProgressMessage(spdlog::level::info,
                           QStringLiteral("正在抽取图像特征并聚类: %1 张图像").arg(request.items.size()));

        irt::features::ImageCluster cluster(request.config);
        const auto result = cluster.cluster(toFsPath(request.weights_file), request.items,
                                            createProgressReporter(ctrl, request.items.size()));

        response.assignments.reserve(result.assignments.size());
        for (const auto &assignment : result.assignments)
        {
            response.assignments.push_back(
                {assignment.image_id, assignment.cluster_id, assignment.probability});
        }

        response.success       = true;
        response.feature_dim   = result.feature_dim;
        response.cluster_count = result.cluster_count;
        response.noise_count   = result.noise_count;
        response.summary       = QStringLiteral("图像聚类完成: %1 张图像, %2 个簇, 噪声 %3 张")
                               .arg(response.assignments.size())
                               .arg(static_cast<qlonglong>(response.cluster_count))
                               .arg(static_cast<qlonglong>(response.noise_count));
    }
    catch (const std::exception &e)
    {
        response.success = false;
        response.error   = QString::fromUtf8(e.what());
    }
    catch (...)
    {
        response.success = false;
        response.error   = QStringLiteral("未知图像聚类错误");
    }
}

bool ImageClusterController::ensureClusterTargetDataset(const QString &target_dataset_name,
                                                        int64_t &dataset_id,
                                                        QString &err_msg)
{
    dataset_id = -1;
    err_msg.clear();

    if (data_manager_ == nullptr)
    {
        err_msg = QStringLiteral("数据管理器未初始化");
        return false;
    }

    dataset_id = data_manager_->getDatasetId(target_dataset_name);
    if (dataset_id >= 0)
        return true;

    data_manager_->addDataset(target_dataset_name);
    dataset_id = data_manager_->getDatasetId(target_dataset_name);
    if (dataset_id < 0)
    {
        err_msg = QStringLiteral("创建聚类目标数据集失败: %1").arg(target_dataset_name);
        return false;
    }
    return true;
}

bool ImageClusterController::buildClusterApplyPlan(const std::vector<ImageClusterAssignment> &assignments,
                                                   const bool include_noise,
                                                   ClusterApplyPlan &plan,
                                                   QString &err_msg)
{
    plan = {};
    err_msg.clear();

    if (data_provider_ == nullptr || data_manager_ == nullptr)
    {
        err_msg = QStringLiteral("数据管理器未初始化");
        return false;
    }
    if (assignments.empty())
    {
        err_msg = QStringLiteral("没有图像聚类结果");
        return false;
    }

    std::map<int64_t, QString> source_dataset_names;
    std::map<QString, int64_t> target_dataset_ids;

    for (const ImageClusterAssignment &assignment : assignments)
    {
        if (assignment.cluster_id < 0 && !include_noise)
        {
            ++plan.skipped_noise_count;
            continue;
        }

        const int64_t source_dataset_id = data_provider_->imageDatasetId(assignment.image_id);
        if (source_dataset_id < 0 || source_dataset_id > std::numeric_limits<int>::max())
        {
            spdlog::warn("跳过无效聚类图像数据集: image_id={}", assignment.image_id);
            continue;
        }

        auto source_name_it = source_dataset_names.find(source_dataset_id);
        if (source_name_it == source_dataset_names.end())
        {
            source_name_it = source_dataset_names
                                 .emplace(source_dataset_id,
                                          data_manager_->getDatasetName(static_cast<int>(source_dataset_id)))
                                 .first;
        }

        const QString &source_dataset_name = source_name_it->second;
        if (source_dataset_name.isEmpty())
        {
            spdlog::warn("跳过无效聚类图像数据集名称: image_id={}, dataset_id={}", assignment.image_id,
                         source_dataset_id);
            continue;
        }

        const QString target_dataset_name = clusterTargetDatasetName(source_dataset_name, assignment.cluster_id);
        auto          target_it           = target_dataset_ids.find(target_dataset_name);
        if (target_it == target_dataset_ids.end())
        {
            int64_t target_dataset_id = -1;
            if (!ensureClusterTargetDataset(target_dataset_name, target_dataset_id, err_msg))
                return false;
            target_it = target_dataset_ids.emplace(target_dataset_name, target_dataset_id).first;
        }

        plan.image_ids_by_target_dataset[target_it->second].push_back(assignment.image_id);
    }

    return true;
}

ImageClusterController::ImageClusterApplyResult ImageClusterController::applyClusterPlan(
    const ClusterApplyPlan &plan, ImageClusterApplyMode apply_mode)
{
    ImageClusterApplyResult result;
    result.skipped_noise_count  = plan.skipped_noise_count;
    result.target_dataset_count = plan.image_ids_by_target_dataset.size();

    const size_t image_count = plannedImageCount(plan.image_ids_by_target_dataset);
    for (const auto &[target_dataset_id, image_ids] : plan.image_ids_by_target_dataset)
    {
        if (apply_mode == ImageClusterApplyMode::Copy)
        {
            data_manager_->copyToDataset(image_ids, target_dataset_id);
        }
        else
        {
            data_manager_->moveToDataset(image_ids, target_dataset_id);
        }
    }

    if (apply_mode == ImageClusterApplyMode::Copy)
        result.copied_image_count = image_count;
    else
        result.moved_image_count = image_count;
    return result;
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
    ui::ProgressManager::getInstance()->startTask(QStringLiteral("图像聚类"));
    addProgressMessage(spdlog::level::info,
                       QStringLiteral("开始图像聚类: %1 张图像").arg(request.items.size()));
}

void ImageClusterController::finishProgress(bool success, const QString &message)
{
    const int level = success ? spdlog::level::info : spdlog::level::err;
    addProgressMessage(level, message);
    ui::ProgressManager::getInstance()->completeTask();
}

void ImageClusterController::finishCluster(const ClusterResponse &response)
{
    setRunning(false);

    if (!response.success)
    {
        result_count_ = 0;
        last_summary_.clear();
        emit resultsChanged();
        setLastError(response.error);
        finishProgress(false, QStringLiteral("%1, 耗时 %2").arg(response.error, formatElapsed(response.elapsed_ms)));
        ui::SignalHelper::notifyError(QString("图像聚类失败"), response.error);
        return;
    }

    ClusterApplyPlan apply_plan;
    QString          err_msg;
    if (!buildClusterApplyPlan(response.assignments, response.include_noise, apply_plan, err_msg))
    {
        result_count_ = 0;
        last_summary_.clear();
        emit resultsChanged();
        setLastError(err_msg);
        finishProgress(false, QStringLiteral("%1, 耗时 %2").arg(err_msg, formatElapsed(response.elapsed_ms)));
        ui::SignalHelper::notifyError(QString("图像聚类失败"), err_msg);
        return;
    }

    const ImageClusterApplyResult apply_result = applyClusterPlan(apply_plan, response.apply_mode);

    setLastError(QString());
    result_count_ = static_cast<int>(std::min<size_t>(apply_result.moved_image_count
                                                          + apply_result.copied_image_count,
                                                      static_cast<size_t>(std::numeric_limits<int>::max())));
    last_summary_ = clusterSummary(response.summary,
                                   apply_result.moved_image_count,
                                   apply_result.copied_image_count,
                                   apply_result.target_dataset_count,
                                   apply_result.skipped_noise_count,
                                   response.apply_mode == ImageClusterApplyMode::Copy);

    finishProgress(true, QStringLiteral("%1, 耗时 %2").arg(last_summary_, formatElapsed(response.elapsed_ms)));
    ui::SignalHelper::notifySuccess(QString("图像聚类完成"), last_summary_);
    emit resultsChanged();
}

irt::features::ImageClusterProgressCallback ImageClusterController::createProgressReporter(
    QPointer<ImageClusterController> controller, size_t total_count)
{
    return [controller, total_count](const irt::features::ImageClusterProgress &progress)
    {
        if (progress.total_count > 0 && controller)
        {
            const int processed = static_cast<int>(std::min<size_t>(progress.processed_count,
                                                                    static_cast<size_t>(std::numeric_limits<int>::max())));
            const int total = static_cast<int>(std::min<size_t>(progress.total_count,
                                                                static_cast<size_t>(std::numeric_limits<int>::max())));
            QMetaObject::invokeMethod(
                controller.data(),
                [controller, processed, total]()
                {
                    if (controller)
                        emit controller->buildProgressChanged(processed, total);
                },
                Qt::QueuedConnection);
        }

        const int pct = imageClusterProgressPercent(progress, total_count);
        if (pct >= 0)
        {
            QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "updateProgress", Qt::QueuedConnection,
                                      Q_ARG(int, pct));
        }

        const QString message = imageClusterProgressMessage(progress, total_count);
        if (!message.isEmpty())
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
