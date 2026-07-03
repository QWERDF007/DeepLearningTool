#include "feature/SearchControllerBase.h"

#include "SearchControllerUtils.h"
#include "feature/FeatureDataProvider.h"
#include "feature/Utils.h"
#include "settings/GlobalSettings.h"
#include "ui/ProgressManager.h"

#include <inferrt/features/ImageSearch.hpp>
#include <spdlog/spdlog.h>

#include <QFileInfo>
#include <QMetaObject>
#include <QThread>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <map>
#include <set>

namespace dltool::feature {

SearchControllerBase::SearchControllerBase(dltool::settings::generated::AccessorKey settings_accessor, QObject *parent)
    : QObject(parent)
    , settings_accessor_(settings_accessor)
{
    auto *gs = dltool::settings::GlobalSettings::getInstance();
    enabled_ = searchSettingsEnabled(gs, settings_accessor_);

    const QString watched_group
        = dltool::settings::toQString(dltool::settings::generated::groupKey(settings_accessor_));
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

bool SearchControllerBase::enabled() const
{
    return enabled_;
}

bool SearchControllerBase::isRunning() const
{
    return running_;
}

bool SearchControllerBase::hasResults() const
{
    return result_count_ > 0;
}

int SearchControllerBase::resultCount() const
{
    return result_count_;
}

QString SearchControllerBase::lastError() const
{
    return last_error_;
}

QString SearchControllerBase::lastSummary() const
{
    return last_summary_;
}

bool SearchControllerBase::search(const QVariantList &ids, const QVariantList &dataset_ids)
{
    if (running_)
    {
        setLastError(searchDisplayName() + QString("正在运行"));
        return false;
    }
    if (!dataProvider())
    {
        setLastError(QString("%1数据未初始化").arg(searchDisplayName()));
        return false;
    }

    const auto int_ids = parseInt64Ids(ids);
    if (int_ids.empty())
    {
        setLastError(emptyQuerySelectionErrorMessage());
        return false;
    }

    if (!ensureSearchSettingsEnabled(searchDisplayName()))
        return false;

    const auto dataset_ids_set = parseDatasetIds(dataset_ids);

    SearchRequest req;
    buildSearchRequest(req);

    if (!validateSearchRequest(req))
        return false;

    req.weights_file = QFileInfo(req.weights_file).absoluteFilePath();

    collectGallery(req, dataset_ids_set);
    if (galleryItemCount(req) == 0)
    {
        setLastError(emptyGalleryErrorMessage());
        return false;
    }

    collectQuery(req, int_ids);
    if (queryItemCount(req) == 0)
    {
        setLastError(emptyPreparedQueryErrorMessage());
        return false;
    }

    req.index_file = computeIndexPath(req);
    req.started_at = std::chrono::steady_clock::now();
    req.controller = QPointer<SearchControllerBase>(this);

    resetForNewSearch();
    startProgress(req);

    QThread *work_thread = QThread::create(
        [req = std::move(req)]() mutable
        {
            SearchResponse response;
            if (req.controller)
                req.controller->executeSearch(req, response);

            response.elapsed_ms = static_cast<qint64>(
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - req.started_at)
                    .count());

            if (req.controller)
            {
                const auto ctrl = req.controller;
                QMetaObject::invokeMethod(
                    ctrl.data(),
                    [ctrl, response]()
                    {
                        if (ctrl)
                            ctrl->finishSearch(response);
                    },
                    Qt::QueuedConnection);
            }
        });

    connect(work_thread, &QThread::finished, work_thread, &QObject::deleteLater);
    work_thread->start();
    return true;
}

std::set<int64_t> SearchControllerBase::parseDatasetIds(const QVariantList &dataset_ids)
{
    const auto ids = parseInt64Ids(dataset_ids);
    return std::set<int64_t>(ids.begin(), ids.end());
}

bool SearchControllerBase::validateWeightsFile(const QString &path)
{
    QFileInfo info(path);
    if (path.trimmed().isEmpty() || !info.isFile())
    {
        setLastError(QString("模型权重文件不存在: %1").arg(path));
        return false;
    }
    return true;
}

bool SearchControllerBase::ensureSearchSettingsEnabled(const QString &display_name)
{
    const auto *settings = dltool::settings::GlobalSettings::getInstance();
    if (settings == nullptr || settings->settingsGroup(settingsAccessor()) == nullptr)
    {
        setLastError(QString("%1设置未加载").arg(display_name));
        return false;
    }

    if (!searchSettingsEnabled(settings, settingsAccessor()))
    {
        setLastError(QString("%1未启用").arg(display_name));
        return false;
    }
    return true;
}

dltool::settings::generated::AccessorKey SearchControllerBase::settingsAccessor() const
{
    return settings_accessor_;
}

QString SearchControllerBase::modelNameForRequest(const SearchRequest &req) const
{
    return QString::fromStdString(req.image_config.model_name);
}

QString SearchControllerBase::featureNameForRequest(const SearchRequest &req) const
{
    return QString::fromStdString(req.image_config.feature_name);
}

bool SearchControllerBase::validateSearchRequest(SearchRequest &req)
{
    if (modelNameForRequest(req).trimmed().isEmpty())
    {
        setLastError(QString("请先配置%1模型").arg(searchDisplayName()));
        return false;
    }
    if (featureNameForRequest(req).trimmed().isEmpty())
    {
        setLastError(QString("请先配置%1特征层").arg(searchDisplayName()));
        return false;
    }
    return validateWeightsFile(req.weights_file);
}

QString SearchControllerBase::searchDisplayName() const
{
    return QString("图像搜索");
}

QString SearchControllerBase::emptyQuerySelectionErrorMessage() const
{
    return QString("请先选择要搜索的图像");
}

QString SearchControllerBase::emptyGalleryErrorMessage() const
{
    return QString("选定数据集中没有可搜索的图像");
}

QString SearchControllerBase::emptyPreparedQueryErrorMessage() const
{
    return QString("选中的查询图像文件不存在");
}

size_t SearchControllerBase::queryItemCount(const SearchRequest &request) const
{
    return request.query_images.size();
}

size_t SearchControllerBase::galleryItemCount(const SearchRequest &request) const
{
    return request.gallery_images.size();
}

QStringList SearchControllerBase::featureOptionsForModel(const QString &) const
{
    return {};
}

void SearchControllerBase::buildSearchRequest(SearchRequest &req)
{
    const auto base_settings
        = readImageSearchBaseSettings(dltool::settings::GlobalSettings::getInstance(), settingsAccessor());
    req.weights_file
        = base_settings.weights_file.isEmpty() ? QString() : QFileInfo(base_settings.weights_file).absoluteFilePath();
    req.rebuild_index = base_settings.rebuild_index;
    req.top_k         = base_settings.top_k;
    applyImageSearchBaseConfig(req.image_config, base_settings);
}

QString SearchControllerBase::computeIndexPath(const SearchRequest &request) const
{
    const auto base_settings
        = readImageSearchBaseSettings(dltool::settings::GlobalSettings::getInstance(), settingsAccessor());
    const auto *provider = dataProvider();
    const QString index_dir = indexDirectoryForProject(provider ? provider->databasePath() : QString(),
                                                       base_settings.index_directory, QStringLiteral("image_search"));
    return indexPathForRequest(index_dir, modelNameForRequest(request), featureNameForRequest(request),
                               QStringLiteral(".faiss"));
}

void SearchControllerBase::collectGallery(SearchRequest &request, const std::set<int64_t> &dataset_ids)
{
    const auto *provider = dataProvider();
    if (!provider)
        return;

    const auto all_ids = provider->allImageIds();
    for (const int64_t id : all_ids)
    {
        if (!dataset_ids.empty() && dataset_ids.find(provider->imageDatasetId(id)) == dataset_ids.end())
            continue;

        const QString path = provider->imagePath(id);
        if (!QFileInfo::exists(path))
            continue;

        request.gallery_images.push_back({id, toFsPath(QFileInfo(path).absoluteFilePath())});
    }
}

void SearchControllerBase::collectQuery(SearchRequest &request, const std::vector<int64_t> &ids)
{
    const auto *provider = dataProvider();
    if (!provider)
        return;

    for (const int64_t id : ids)
    {
        const QString path = provider->imagePath(id);
        if (QFileInfo::exists(path))
            request.query_images.push_back(toFsPath(QFileInfo(path).absoluteFilePath()));
    }
}

void SearchControllerBase::executeSearch(const SearchRequest &request, SearchResponse &response)
{
    const size_t gallery_count = request.gallery_images.size();
    const auto   ctrl          = request.controller;

    auto reportProgress = createBuildProgressReporter(ctrl, gallery_count);

    try
    {
        irt::features::ImageSearch search(request.image_config);
        const auto                 weights_path = toFsPath(request.weights_file);
        const auto                 index_path   = toFsPath(request.index_file);

        addProgressMessage(spdlog::level::info,
                           QString("正在准备图像搜索特征库: %1 张图像").arg(request.gallery_images.size()));
        search.buildOrLoad(weights_path, request.gallery_images, index_path, request.rebuild_index, reportProgress);

        std::map<int64_t, float> result_scores;
        for (const auto &query_image : request.query_images)
        {
            for (const auto &result : search.search(query_image, request.top_k))
            {
                const int64_t image_id = result.image_id;
                auto          it       = result_scores.find(image_id);
                if (it == result_scores.end() || result.score > it->second)
                    result_scores[image_id] = result.score;
            }
        }

        response.result_ids = sortedSearchResultIds(result_scores);
        response.success    = true;
        response.summary    = QString("图像搜索完成: 命中 %1 张图像").arg(response.result_ids.size());
    }
    catch (const std::exception &e)
    {
        response.success = false;
        response.error   = QString::fromUtf8(e.what());
    }
    catch (...)
    {
        response.success = false;
        response.error   = QString("未知图像搜索错误");
    }
}

void SearchControllerBase::resetForNewSearch()
{
    setLastError(QString());
    last_summary_.clear();
    result_count_ = 0;
    clearProviderResults();
    emit resultsChanged();
}

void SearchControllerBase::startProgress(const SearchRequest &request)
{
    setRunning(true);
    ui::ProgressManager::getInstance()->startTask(searchDisplayName());
    addProgressMessage(spdlog::level::info, QString("开始%1: 查询 %2 项, 搜索库 %3 项, TopK=%4")
                                                .arg(searchDisplayName())
                                                .arg(queryItemCount(request))
                                                .arg(galleryItemCount(request))
                                                .arg(request.top_k));
}

void SearchControllerBase::finishProgress(bool success, const QString &message)
{
    const int level = success ? spdlog::level::info : spdlog::level::err;
    addProgressMessage(level, message);
    ui::ProgressManager::getInstance()->completeTask();
}

void SearchControllerBase::finishSearch(const SearchResponse &response)
{
    setRunning(false);

    if (!response.success)
    {
        result_count_ = 0;
        last_summary_.clear();
        emit resultsChanged();
        setLastError(response.error);
        finishProgress(false, QString("%1, 耗时 %2").arg(response.error, formatElapsed(response.elapsed_ms)));
        return;
    }

    setLastError(QString());
    result_count_ = static_cast<int>(response.result_ids.size());
    last_summary_ = response.summary;

    applyResults(response);

    finishProgress(true, QString("%1, 耗时 %2").arg(response.summary, formatElapsed(response.elapsed_ms)));
    emit resultsChanged();
}

SearchControllerBase::BuildProgressCallback SearchControllerBase::createBuildProgressReporter(
    QPointer<SearchControllerBase> controller, size_t gallery_count)
{
    return [controller, gallery_count](const irt::features::ImageSearchBuildProgress &progress)
    {
        size_t     resolved_processed = 0;
        size_t     resolved_total     = 0;
        const bool has_count = resolveProgressCount(progress, gallery_count, resolved_processed, resolved_total);

        if (controller && has_count)
        {
            const int processed
                = static_cast<int>(std::min<size_t>(resolved_processed, std::numeric_limits<int>::max()));
            const int total = static_cast<int>(std::min<size_t>(resolved_total, std::numeric_limits<int>::max()));
            QMetaObject::invokeMethod(
                controller.data(),
                [controller, processed, total]()
                {
                    if (controller)
                        emit controller->buildProgressChanged(processed, total);
                },
                Qt::QueuedConnection);
        }

        const int pct = progressPercent(progress, gallery_count);
        if (pct >= 0)
        {
            QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "updateProgress", Qt::QueuedConnection,
                                      Q_ARG(int, pct));
        }

        const QString message = formatBuildProgressMessage(progress, gallery_count);
        if (!message.isEmpty())
            addProgressMessage(spdlog::level::info, message);
    };
}

void SearchControllerBase::setRunning(bool running)
{
    if (running_ == running)
        return;
    running_ = running;
    emit runningChanged();
}

void SearchControllerBase::setLastError(const QString &last_error)
{
    if (last_error_ == last_error)
        return;
    last_error_ = last_error;
    if (!last_error_.isEmpty())
        spdlog::error("搜索失败: {}", last_error_.toUtf8().constData());
    emit lastErrorChanged();
}

} // namespace dltool::feature
