#include "feature/RoiClusterController.h"

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
#include <limits>
#include <map>
#include <set>

namespace dltool::feature {
namespace {

using AccessorKey = dltool::settings::generated::AccessorKey;

constexpr AccessorKey kSettingsAccessor = AccessorKey::RoiCluster;

int roiClusterProgressPercent(const irt::features::RoiClusterProgress &progress, const size_t total_count)
{
    using Stage = irt::features::RoiClusterStage;
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

QString roiClusterProgressMessage(const irt::features::RoiClusterProgress &progress, const size_t total_count)
{
    if (progress.stage == irt::features::RoiClusterStage::Unknown)
        return {};

    const QString stage_name = QString::fromUtf8(irt::features::roiClusterStageName(progress.stage));
    const size_t  total      = progress.total_count > 0 ? progress.total_count : total_count;
    if (total > 0 && progress.stage == irt::features::RoiClusterStage::ExtractingFeatures)
    {
        return QString("标注聚类进度 [%1]: %2 / %3")
            .arg(stage_name)
            .arg(static_cast<qlonglong>(progress.processed_count))
            .arg(static_cast<qlonglong>(total));
    }
    return QString("标注聚类阶段 [%1]").arg(stage_name);
}

QString clusterTagName(const QString &class_name, const int64_t cluster_id)
{
    return QStringLiteral("%1_%2").arg(class_name).arg(QString::number(cluster_id));
}

} // namespace

RoiClusterController::RoiClusterController(RoiClusterDataProvider *data_provider,
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
                    const bool enabled = value.toBool();
                    if (enabled != enabled_)
                    {
                        enabled_ = enabled;
                        emit enabledChanged();
                    }
                }
            });
}

bool RoiClusterController::enabled() const
{
    return enabled_;
}

bool RoiClusterController::isRunning() const
{
    return running_;
}

bool RoiClusterController::hasResults() const
{
    return result_count_ > 0;
}

int RoiClusterController::resultCount() const
{
    return result_count_;
}

QString RoiClusterController::lastError() const
{
    return last_error_;
}

QString RoiClusterController::lastSummary() const
{
    return last_summary_;
}

QString RoiClusterController::validationError() const
{
    if (running_)
        return QString("标注聚类正在运行");
    if (data_provider_ == nullptr)
        return QString("标注聚类模型未初始化");
    if (data_manager_ == nullptr)
        return QString("数据管理器未初始化");

    const auto *settings = dltool::settings::GlobalSettings::getInstance();
    if (settings == nullptr || settings->settingsGroup(kSettingsAccessor) == nullptr)
        return QString("标注聚类设置未加载");
    if (!searchSettingsEnabled(settings, kSettingsAccessor))
        return QString("标注聚类未启用");

    Request request;
    buildRequest(request);
    return requestValidationError(request);
}

bool RoiClusterController::cluster(const QVariantList &dataset_class_scope)
{
    if (running_)
    {
        setLastError(QString("标注聚类正在运行"));
        return false;
    }
    if (data_provider_ == nullptr)
    {
        setLastError(QString("标注聚类模型未初始化"));
        return false;
    }
    if (data_manager_ == nullptr)
    {
        setLastError(QString("数据管理器未初始化"));
        return false;
    }

    const auto scope = parseDatasetClassScope(dataset_class_scope);
    if (scope.empty())
    {
        setLastError(QString("请选择要聚类的数据集或类别"));
        return false;
    }

    const auto *settings = dltool::settings::GlobalSettings::getInstance();
    if (settings == nullptr || settings->settingsGroup(kSettingsAccessor) == nullptr)
    {
        setLastError(QString("标注聚类设置未加载"));
        return false;
    }
    if (!searchSettingsEnabled(settings, kSettingsAccessor))
    {
        setLastError(QString("标注聚类未启用"));
        return false;
    }

    Request request;
    buildRequest(request);
    if (!validateRequest(request))
        return false;

    collectClusterItems(request, scope);
    if (request.items.empty())
    {
        setLastError(QString("选定范围内没有可聚类的标注 ROI"));
        return false;
    }
    if (request.items.size() <= 1)
    {
        setLastError(QString("标注聚类至少需要 2 个标注"));
        return false;
    }

    request.weights_file = QFileInfo(request.weights_file).absoluteFilePath();
    request.started_at   = std::chrono::steady_clock::now();
    request.controller   = QPointer<RoiClusterController>(this);

    resetForNewCluster();
    startProgress(request);

    QThread *work_thread = QThread::create(
        [request = std::move(request)]() mutable
        {
            Response response;
            response.include_noise = request.include_noise;
            if (request.controller)
                request.controller->executeCluster(request, response);

            response.elapsed_ms = static_cast<qint64>(
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()
                                                                      - request.started_at)
                    .count());

            if (request.controller)
            {
                const auto controller = request.controller;
                QMetaObject::invokeMethod(
                    controller.data(),
                    [controller, response]()
                    {
                        if (controller)
                            controller->finishCluster(response);
                    },
                    Qt::QueuedConnection);
            }
        });

    connect(work_thread, &QThread::finished, work_thread, &QObject::deleteLater);
    work_thread->start();
    return true;
}

void RoiClusterController::buildRequest(Request &request) const
{
    const auto settings      = readRoiClusterSettings(dltool::settings::GlobalSettings::getInstance());
    request.weights_file    = settings.base.weights_file.isEmpty()
                                  ? QString()
                                  : QFileInfo(settings.base.weights_file).absoluteFilePath();
    request.include_noise   = settings.include_noise;
    applyRoiClusterConfig(request.config, settings);
}

bool RoiClusterController::validateRequest(const Request &request)
{
    const QString error = requestValidationError(request);
    if (!error.isEmpty())
    {
        setLastError(error);
        return false;
    }
    return true;
}

QString RoiClusterController::requestValidationError(const Request &request) const
{
    if (QString::fromStdString(request.config.model_name).trimmed().isEmpty())
        return QString("请先配置标注聚类模型");
    if (QString::fromStdString(request.config.feature_name).trimmed().isEmpty())
        return QString("请先配置标注聚类特征层");

    const QFileInfo weights_info(request.weights_file);
    if (request.weights_file.trimmed().isEmpty() || !weights_info.isFile())
        return QString("模型权重文件不存在: %1").arg(request.weights_file);
    if (request.config.pooled_height <= 0 || request.config.pooled_width <= 0)
        return QString("ROI 输出尺寸必须大于 0");
    if (request.config.hdbscan.min_cluster_size < 2)
        return QString("最小簇大小必须至少为 2");
    return {};
}

void RoiClusterController::collectClusterItems(Request &request,
                                               const std::map<int64_t, std::set<int64_t>> &scope)
{
    std::set<int64_t> seen_ids;
    for (const int64_t label_id : data_provider_->allLabelIds())
    {
        if (!seen_ids.insert(label_id).second)
            continue;

        const int64_t image_id = data_provider_->labelImageId(label_id);
        if (image_id < 0)
            continue;

        const int64_t dataset_id = data_provider_->imageDatasetId(image_id);
        const auto    scope_it   = scope.find(dataset_id);
        if (scope_it == scope.end())
            continue;

        if (!scope_it->second.empty() && scope_it->second.find(data_provider_->labelClassId(label_id))
                                             == scope_it->second.end())
            continue;

        const QFileInfo image_info(data_provider_->imagePath(image_id));
        if (!image_info.isFile())
            continue;

        irt::features::RoiClusterBox roi;
        if (!roiFromLabelData(data_provider_->labelData(label_id), roi))
            continue;

        request.items.push_back({label_id, toFsPath(image_info.absoluteFilePath()), roi});
    }
}

void RoiClusterController::executeCluster(const Request &request, Response &response)
{
    try
    {
        addProgressMessage(spdlog::level::info,
                           QString("正在抽取标注 ROI 特征并聚类: %1 个标注").arg(request.items.size()));

        irt::features::RoiCluster cluster(request.config);
        const auto result = cluster.cluster(toFsPath(request.weights_file), request.items,
                                            createProgressReporter(request.controller, request.items.size()));

        response.assignments = result.assignments;
        response.feature_dim = result.feature_dim;
        response.cluster_count = result.cluster_count;
        response.noise_count = result.noise_count;
        response.success = true;
        response.summary = QString("标注聚类完成: %1 个标注, %2 个簇, 噪声 %3 个")
                               .arg(response.assignments.size())
                               .arg(static_cast<qlonglong>(response.cluster_count))
                               .arg(static_cast<qlonglong>(response.noise_count));
    }
    catch (const std::exception &e)
    {
        response.success = false;
        response.error = QString(e.what());
    }
    catch (...)
    {
        response.success = false;
        response.error = QString("未知标注聚类错误");
    }
}

bool RoiClusterController::applyClusterResult(const Response &response, size_t &assigned_count,
                                              size_t &tag_count, size_t &skipped_noise_count, QString &err_msg)
{
    assigned_count = 0;
    tag_count = 0;
    skipped_noise_count = 0;
    err_msg.clear();

    std::map<QString, std::set<int64_t>> labels_by_tag;
    for (const auto &assignment : response.assignments)
    {
        if (assignment.cluster_id < 0 && !response.include_noise)
        {
            ++skipped_noise_count;
            continue;
        }

        const QString class_name
            = data_provider_->labelClassName(data_provider_->labelClassId(assignment.roi_id)).trimmed();
        if (class_name.isEmpty())
        {
            spdlog::warn("跳过无效标注类别: label_id={}", assignment.roi_id);
            continue;
        }
        labels_by_tag[clusterTagName(class_name, assignment.cluster_id)].insert(assignment.roi_id);
    }

    for (const auto &[tag_name, label_id_set] : labels_by_tag)
    {
        int64_t tag_id = data_manager_->findTagClassId(tag_name);
        if (tag_id < 0)
        {
            data_manager_->addTagClass(tag_name);
            tag_id = data_manager_->findTagClassId(tag_name);
        }
        if (tag_id < 0)
        {
            err_msg = QString("创建聚类 Tag 失败: %1").arg(tag_name);
            return false;
        }

        const std::vector<int64_t> label_ids(label_id_set.begin(), label_id_set.end());
        if (!data_manager_->setLabelsTag(label_ids, tag_id))
        {
            err_msg = QString("设置聚类 Tag 失败: %1").arg(tag_name);
            return false;
        }
        assigned_count += label_ids.size();
        ++tag_count;
    }
    return true;
}

void RoiClusterController::resetForNewCluster()
{
    setLastError(QString());
    last_summary_.clear();
    result_count_ = 0;
    emit resultsChanged();
}

void RoiClusterController::startProgress(const Request &request)
{
    setRunning(true);
    ui::ProgressManager::getInstance()->startTask(QString("标注聚类"));
    addProgressMessage(spdlog::level::info,
                       QString("开始标注聚类: %1 个标注").arg(request.items.size()));
}

void RoiClusterController::finishProgress(const bool success, const QString &message)
{
    addProgressMessage(success ? spdlog::level::info : spdlog::level::err, message);
    ui::ProgressManager::getInstance()->completeTask();
}

void RoiClusterController::finishCluster(const Response &response)
{
    setRunning(false);

    if (!response.success)
    {
        result_count_ = 0;
        last_summary_.clear();
        emit resultsChanged();
        setLastError(response.error);
        spdlog::error("标注聚类失败: {}, 耗时 {}", response.error.toUtf8().constData(),
                      formatElapsed(response.elapsed_ms).toUtf8().constData());
        finishProgress(false, QString("%1, 耗时 %2").arg(response.error, formatElapsed(response.elapsed_ms)));
        ui::SignalHelper::notifyError(QString("标注聚类失败"), response.error);
        return;
    }

    size_t assigned_count = 0;
    size_t tag_count = 0;
    size_t skipped_noise_count = 0;
    QString err_msg;
    if (!applyClusterResult(response, assigned_count, tag_count, skipped_noise_count, err_msg))
    {
        result_count_ = 0;
        last_summary_.clear();
        emit resultsChanged();
        setLastError(err_msg);
        spdlog::error("标注聚类失败: {}, 耗时 {}", err_msg.toUtf8().constData(),
                      formatElapsed(response.elapsed_ms).toUtf8().constData());
        finishProgress(false, QString("%1, 耗时 %2").arg(err_msg, formatElapsed(response.elapsed_ms)));
        ui::SignalHelper::notifyError(QString("标注聚类失败"), err_msg);
        return;
    }

    result_count_ = static_cast<int>(std::min(assigned_count, static_cast<size_t>(std::numeric_limits<int>::max())));
    last_summary_ = QString("%1，已设置 %2 个 Tag，覆盖 %3 个标注")
                        .arg(response.summary)
                        .arg(static_cast<qlonglong>(tag_count))
                        .arg(static_cast<qlonglong>(assigned_count));
    if (skipped_noise_count > 0)
        last_summary_ += QString("，跳过噪声 %1 个").arg(static_cast<qlonglong>(skipped_noise_count));

    setLastError(QString());
    spdlog::info("标注聚类完成: {}, 耗时 {}", last_summary_.toUtf8().constData(),
                 formatElapsed(response.elapsed_ms).toUtf8().constData());
    finishProgress(true, QString("%1, 耗时 %2").arg(last_summary_, formatElapsed(response.elapsed_ms)));
    ui::SignalHelper::notifySuccess(QString("标注聚类完成"), last_summary_);
    emit resultsChanged();
}

irt::features::RoiClusterProgressCallback RoiClusterController::createProgressReporter(
    QPointer<RoiClusterController> controller, const size_t total_count)
{
    return [controller, total_count](const irt::features::RoiClusterProgress &progress)
    {
        if (progress.total_count > 0 && controller)
        {
            const int processed = static_cast<int>(std::min<size_t>(
                progress.processed_count, static_cast<size_t>(std::numeric_limits<int>::max())));
            const int total = static_cast<int>(std::min<size_t>(
                progress.total_count, static_cast<size_t>(std::numeric_limits<int>::max())));
            QMetaObject::invokeMethod(
                controller.data(),
                [controller, processed, total]()
                {
                    if (controller)
                        emit controller->buildProgressChanged(processed, total);
                },
                Qt::QueuedConnection);
        }

        const int percent = roiClusterProgressPercent(progress, total_count);
        if (percent >= 0)
        {
            QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "updateProgress", Qt::QueuedConnection,
                                      Q_ARG(int, percent));
        }

        const QString message = roiClusterProgressMessage(progress, total_count);
        if (!message.isEmpty())
            addProgressMessage(spdlog::level::info, message);
    };
}

void RoiClusterController::setRunning(const bool running)
{
    if (running_ == running)
        return;
    running_ = running;
    emit runningChanged();
}

void RoiClusterController::setLastError(const QString &last_error)
{
    if (last_error_ == last_error)
        return;
    last_error_ = last_error;
    if (!last_error_.isEmpty())
        spdlog::error("标注聚类失败: {}", last_error_.toUtf8().constData());
    emit lastErrorChanged();
}

} // namespace dltool::feature
