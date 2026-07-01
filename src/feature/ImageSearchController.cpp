#include "feature/ImageSearchController.h"

#include "SearchControllerUtils.h"
#include "feature/ImageSearchDataProvider.h"
#include "feature/Utils.h"
#include "settings/GlobalSettings.h"
#include "ui/ProgressManager.h"

#include <inferrt/features/ImageSearch.hpp>
#include <spdlog/spdlog.h>

#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <map>
#include <set>

namespace dltool::feature {

ImageSearchController::ImageSearchController(ImageSearchDataProvider *data_provider, QObject *parent)
    : ImageSearchController(data_provider, dltool::settings::generated::AccessorKey::ImageSearch, parent)
{
}

ImageSearchController::ImageSearchController(ImageSearchDataProvider                 *data_provider,
                                             dltool::settings::generated::AccessorKey settings_accessor,
                                             QObject                                 *parent)
    : QObject(parent)
    , data_provider_(data_provider)
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

bool ImageSearchController::enabled() const
{
    return enabled_;
}

bool ImageSearchController::isRunning() const
{
    return running_;
}

bool ImageSearchController::hasResults() const
{
    return result_count_ > 0;
}

int ImageSearchController::resultCount() const
{
    return result_count_;
}

QString ImageSearchController::lastError() const
{
    return last_error_;
}

QString ImageSearchController::lastSummary() const
{
    return last_summary_;
}

/**
 * @brief 以当前选中的图像作为查询进行搜索
 * @param dataset_ids 数据集 ID 列表
 * @return 启动成功返回 true
 */
bool ImageSearchController::searchSelectedImages(const QVariantList &dataset_ids)
{
    const auto query_ids = data_provider_->selectedImageIds();
    if (query_ids.empty())
    {
        setLastError(QString("请先选择要检索的图片"));
        return false;
    }

    QVariantList ids;
    ids.reserve(static_cast<int>(query_ids.size()));
    for (const int64_t id : query_ids) ids.append(static_cast<qlonglong>(id));
    return search(ids, dataset_ids);
}

/**
 * @brief 以指定 ID 列表作为查询进行搜索
 * @param ids 图像/标注 ID 列表
 * @param dataset_ids 数据集 ID 列表
 * @return 启动成功返回 true
 */
bool ImageSearchController::search(const QVariantList &ids, const QVariantList &dataset_ids)
{
    if (running_)
    {
        setLastError(searchDisplayName() + QString("正在运行"));
        return false;
    }
    if (!data_provider_)
    {
        setLastError(QString("图像模型未初始化"));
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
    req.controller = QPointer<ImageSearchController>(this);

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

/**
 * @brief 解析数据集 ID 列表
 * @param dataset_ids QVariantList 格式的数据集 ID
 * @return 去重后的数据集 ID 集合
 */
std::set<int64_t> ImageSearchController::parseDatasetIds(const QVariantList &dataset_ids)
{
    const auto ids = parseInt64Ids(dataset_ids);
    return std::set<int64_t>(ids.begin(), ids.end());
}

/**
 * @brief 验证权重文件是否存在
 * @param path 权重文件路径
 * @return 文件存在返回 true
 */
bool ImageSearchController::validateWeightsFile(const QString &path)
{
    QFileInfo info(path);
    if (path.trimmed().isEmpty() || !info.isFile())
    {
        setLastError(QString("模型权重文件不存在: %1").arg(path));
        return false;
    }
    return true;
}

/**
 * @brief 确认搜索设置已启用
 * @param display_name 搜索功能显示名称
 * @return 已启用返回 true
 */
bool ImageSearchController::ensureSearchSettingsEnabled(const QString &display_name)
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

// ── 虚函数默认实现 ──

dltool::settings::generated::AccessorKey ImageSearchController::settingsAccessor() const
{
    return settings_accessor_;
}

/**
 * @brief 获取请求对应的模型名称
 * @param req 搜索请求
 * @return 模型名称
 */
QString ImageSearchController::modelNameForRequest(const SearchRequest &req) const
{
    return QString::fromStdString(req.image_config.model_name);
}

/**
 * @brief 获取请求对应的特征层名称
 * @param req 搜索请求
 * @return 特征层名称
 */
QString ImageSearchController::featureNameForRequest(const SearchRequest &req) const
{
    return QString::fromStdString(req.image_config.feature_name);
}

/**
 * @brief 验证搜索请求参数
 * @param req 搜索请求
 * @return 验证通过返回 true
 */
bool ImageSearchController::validateSearchRequest(SearchRequest &req)
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

/**
 * @brief 获取搜索功能的显示名称
 * @return 显示名称
 */
QString ImageSearchController::searchDisplayName() const
{
    return QString("图像搜索");
}

/**
 * @brief 获取查询为空时的错误提示
 * @return 错误提示文本
 */
QString ImageSearchController::emptyQuerySelectionErrorMessage() const
{
    return QString("请先选择要搜索的图像");
}

/**
 * @brief 获取搜索库为空时的错误提示
 * @return 错误提示文本
 */
QString ImageSearchController::emptyGalleryErrorMessage() const
{
    return QString("选定数据集中没有可搜索的图像");
}

/**
 * @brief 获取查询准备为空时的错误提示
 * @return 错误提示文本
 */
QString ImageSearchController::emptyPreparedQueryErrorMessage() const
{
    return QString("选中的查询图像文件不存在");
}

/**
 * @brief 获取查询项数量
 * @param request 搜索请求
 * @return 查询项数量
 */
size_t ImageSearchController::queryItemCount(const SearchRequest &request) const
{
    return request.query_images.size();
}

/**
 * @brief 获取搜索库项数量
 * @param request 搜索请求
 * @return 搜索库项数量
 */
size_t ImageSearchController::galleryItemCount(const SearchRequest &request) const
{
    return request.gallery_images.size();
}

/**
 * @brief 获取模型可用的特征层选项
 * @param model_name 模型名称
 * @return 特征层名称列表
 */
QStringList ImageSearchController::featureOptionsForModel(const QString &) const
{
    return {};
}

/**
 * @brief 构建搜索请求配置参数
 * @param req 搜索请求
 */
void ImageSearchController::buildSearchRequest(SearchRequest &req)
{
    const auto base_settings
        = readImageSearchBaseSettings(dltool::settings::GlobalSettings::getInstance(), settingsAccessor());
    req.weights_file
        = base_settings.weights_file.isEmpty() ? QString() : QFileInfo(base_settings.weights_file).absoluteFilePath();
    req.rebuild_index = base_settings.rebuild_index;
    req.top_k         = base_settings.top_k;
    applyImageSearchBaseConfig(req.image_config, base_settings);
}

/**
 * @brief 计算特征索引文件路径
 * @param request 搜索请求
 * @return 索引文件路径
 */
QString ImageSearchController::computeIndexPath(const SearchRequest &request) const
{
    const auto base_settings
        = readImageSearchBaseSettings(dltool::settings::GlobalSettings::getInstance(), settingsAccessor());
    const QString index_dir = indexDirectoryForProject(data_provider_->databasePath(), base_settings.index_directory,
                                                       QStringLiteral("image_search"));
    return indexPathForRequest(index_dir, modelNameForRequest(request), featureNameForRequest(request),
                               QStringLiteral(".faiss"));
}

/**
 * @brief 从数据集中收集图像作为搜索库
 * @param request 搜索请求
 * @param dataset_ids 数据集 ID 集合
 */
void ImageSearchController::collectGallery(SearchRequest &request, const std::set<int64_t> &dataset_ids)
{
    const auto all_ids = data_provider_->allImageIds();

    for (const int64_t id : all_ids)
    {
        if (!dataset_ids.empty() && dataset_ids.find(data_provider_->imageDatasetId(id)) == dataset_ids.end())
            continue;

        const QString path = data_provider_->imagePath(id);
        if (!QFileInfo::exists(path))
            continue;

        request.gallery_images.push_back({id, toFsPath(QFileInfo(path).absoluteFilePath())});
    }
}

/**
 * @brief 收集查询图像路径
 * @param request 搜索请求
 * @param ids 图像 ID 列表
 */
void ImageSearchController::collectQuery(SearchRequest &request, const std::vector<int64_t> &ids)
{
    for (const int64_t id : ids)
    {
        const QString path = data_provider_->imagePath(id);
        if (QFileInfo::exists(path))
            request.query_images.push_back(toFsPath(QFileInfo(path).absoluteFilePath()));
    }
}

/**
 * @brief 执行图像搜索
 * @param request 搜索请求
 * @param response 搜索响应（输出）
 */
void ImageSearchController::executeSearch(const SearchRequest &request, SearchResponse &response)
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

// ── UI 反馈 ──

/// 清除数据提供者的图像搜索结果
void ImageSearchController::clearProviderResults()
{
    if (data_provider_)
        data_provider_->clearImageSearchResults();
}

/// 重置状态以开始新搜索
void ImageSearchController::resetForNewSearch()
{
    setLastError(QString());
    last_summary_.clear();
    result_count_ = 0;
    clearProviderResults();
    emit resultsChanged();
}

/**
 * @brief 启动进度显示
 * @param request 搜索请求
 */
void ImageSearchController::startProgress(const SearchRequest &request)
{
    setRunning(true);
    ui::ProgressManager::getInstance()->startTask(searchDisplayName());
    addProgressMessage(spdlog::level::info, QString("开始%1: 查询 %2 张, 图库 %3 张, TopK=%4")
                                                .arg(searchDisplayName())
                                                .arg(queryItemCount(request))
                                                .arg(galleryItemCount(request))
                                                .arg(request.top_k));
}

/**
 * @brief 结束进度显示
 * @param success 是否成功
 * @param message 日志消息
 */
void ImageSearchController::finishProgress(bool success, const QString &message)
{
    const int level = success ? spdlog::level::info : spdlog::level::err;
    addProgressMessage(level, message);
    ui::ProgressManager::getInstance()->completeTask();
}

/**
 * @brief 完成搜索并处理结果
 * @param response 搜索响应
 */
void ImageSearchController::finishSearch(const SearchResponse &response)
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

/**
 * @brief 创建构建进度报告回调
 * @param controller 控制器指针
 * @param gallery_count 搜索库项数量
 * @return 进度回调函数
 */
ImageSearchController::BuildProgressCallback ImageSearchController::createBuildProgressReporter(
    QPointer<ImageSearchController> controller, size_t gallery_count)
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

/**
 * @brief 将搜索结果应用到数据提供者
 * @param response 搜索响应
 */
void ImageSearchController::applyResults(const SearchResponse &response)
{
    if (data_provider_)
        data_provider_->setImageSearchResults(response.result_ids, !response.result_ids.empty());
}

void ImageSearchController::setRunning(bool running)
{
    if (running_ == running)
        return;
    running_ = running;
    emit runningChanged();
}

void ImageSearchController::setLastError(const QString &last_error)
{
    if (last_error_ == last_error)
        return;
    last_error_ = last_error;
    if (!last_error_.isEmpty())
        spdlog::error("搜索失败: {}", last_error_.toUtf8().constData());
    emit lastErrorChanged();
}

} // namespace dltool::feature
