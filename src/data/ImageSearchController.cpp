#include "data/ImageSearchController.h"

#include "data/DataManager.h"
#include "data/GlobalFilter.h"
#include "data/Images.h"
#include "settings/GlobalSettings.h"
#include "ui/ProgressManager.h"

#include <inferrt/features/ImageSearch.hpp>
#include <inferrt/model/ModelFeatures.hpp>
#include <spdlog/spdlog.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>

namespace dltool::data {

namespace {

constexpr const char *kInferRtRoot = "F:/Projects/InferRT/InferRT-0.0.1";

std::filesystem::path toFsPath(const QString &path)
{
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

QString fromFsPath(const std::filesystem::path &path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

QString normalizedPathKey(const QString &path)
{
    QFileInfo info(path);
    QString   normalized = info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
    normalized           = QDir::cleanPath(normalized);
#ifdef _WIN32
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}

QString sanitizeFilePart(QString value)
{
    value = value.trimmed();
    if (value.isEmpty())
    {
        value = QStringLiteral("default");
    }
    value.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
    return value;
}

QString datasetHash(const std::vector<int64_t> &dataset_ids, const std::vector<int64_t> &gallery_image_ids)
{
    QByteArray payload;
    for (const int64_t dataset_id : dataset_ids)
    {
        payload += QByteArray::number(dataset_id);
        payload += ';';
    }
    payload += '|';
    for (const int64_t image_id : gallery_image_ids)
    {
        payload += QByteArray::number(image_id);
        payload += ';';
    }
    return QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha1).toHex().left(16));
}

QString indexDirectoryForProject(const QString &database_path, const QString &custom_directory)
{
    if (!custom_directory.trimmed().isEmpty())
    {
        return QDir::cleanPath(custom_directory.trimmed());
    }

    if (!database_path.isEmpty())
    {
        return QFileInfo(database_path).absoluteDir().filePath(QStringLiteral(".image_search"));
    }

    QString fallback = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (fallback.isEmpty())
    {
        fallback = QDir::tempPath();
    }
    return QDir(fallback).filePath(QStringLiteral("image_search"));
}

QString indexPathForRequest(const QString &index_dir_path, const std::vector<int64_t> &dataset_ids,
                            const std::vector<int64_t> &gallery_image_ids, const QString &model_name,
                            const QString &feature_name, const QString &norm, const QString &faiss_backend,
                            const QString &index_storage, const QString &model_backend, const QString &model_device)
{
    QDir index_dir(index_dir_path);
    if (!index_dir.exists())
    {
        index_dir.mkpath(QStringLiteral("."));
    }

    const QString key = datasetHash(dataset_ids, gallery_image_ids);
    const QString file_name
        = QStringLiteral("%1_%2_%3_%4_%5_%6_%7_%8.faiss")
              .arg(sanitizeFilePart(model_name), sanitizeFilePart(feature_name), sanitizeFilePart(norm),
                   sanitizeFilePart(faiss_backend), sanitizeFilePart(index_storage),
                   sanitizeFilePart(model_backend), sanitizeFilePart(model_device), key);
    return index_dir.filePath(file_name);
}

irt::features::ImageSearchFeatureNorm parseNorm(const QString &norm)
{
    const QString value = norm.trimmed().toLower();
    if (value == QStringLiteral("none"))
    {
        return irt::features::ImageSearchFeatureNorm::None;
    }
    if (value == QStringLiteral("l1"))
    {
        return irt::features::ImageSearchFeatureNorm::L1;
    }
    return irt::features::ImageSearchFeatureNorm::L2;
}

irt::features::ImageSearchPreprocessBackend parsePreprocessBackend(const QString &backend)
{
    return backend.trimmed().toLower() == QStringLiteral("gpu")
               ? irt::features::ImageSearchPreprocessBackend::GPU
               : irt::features::ImageSearchPreprocessBackend::CPU;
}

irt::features::ImageSearchFaissBackend parseFaissBackend(const QString &backend)
{
    return backend.trimmed().toLower() == QStringLiteral("gpu")
               ? irt::features::ImageSearchFaissBackend::GPU
               : irt::features::ImageSearchFaissBackend::CPU;
}

irt::features::ImageSearchIndexStorage parseIndexStorage(const QString &storage)
{
    return storage.trimmed().toLower() == QStringLiteral("disk")
               ? irt::features::ImageSearchIndexStorage::Disk
               : irt::features::ImageSearchIndexStorage::RAM;
}

irt::model::ModelBackend parseModelBackend(const QString &backend)
{
    const QString value = backend.trimmed().toLower();
    if (value == QStringLiteral("openvino"))
    {
        return irt::model::ModelBackend::OpenVINO;
    }
    if (value == QStringLiteral("onnxruntime"))
    {
        return irt::model::ModelBackend::ONNXRuntime;
    }
    return irt::model::ModelBackend::TensorRT;
}

irt::model::ModelDevice parseModelDevice(const QString &device)
{
    return device.trimmed().toLower() == QStringLiteral("cpu")
               ? irt::model::ModelDevice::CPU
               : irt::model::ModelDevice::GPU;
}

void addProgressMessage(int level, const QString &message)
{
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::QueuedConnection,
                              Q_ARG(int, level), Q_ARG(QString, message));
}

} // namespace

struct ImageSearchController::SearchRequest
{
    QString model_name;
    QString feature_name;
    QString weights_file;
    QString index_file;
    QString norm;
    QString faiss_backend;
    QString index_storage;

    bool rebuild_index{false};
    int  top_k{5};
    int  disk_build_batch_size{256};

    irt::features::ImageSearchPreprocessBackend preprocess_backend{irt::features::ImageSearchPreprocessBackend::CPU};
    irt::features::ImageSearchFeatureNorm       norm_mode{irt::features::ImageSearchFeatureNorm::L2};
    irt::features::ImageSearchFaissBackend      faiss_backend_mode{irt::features::ImageSearchFaissBackend::CPU};
    irt::features::ImageSearchIndexStorage      index_storage_mode{irt::features::ImageSearchIndexStorage::RAM};
    irt::model::ModelBackend                    model_backend{irt::model::ModelBackend::TensorRT};
    irt::model::ModelDevice                     model_device{irt::model::ModelDevice::GPU};

    QString model_backend_str;
    QString model_device_str;

    std::vector<std::filesystem::path> query_images;
    std::vector<std::filesystem::path> gallery_images;
    QHash<QString, int64_t>            path_to_image_id;
};

struct ImageSearchController::SearchResponse
{
    bool                 success{false};
    QString              error;
    QString              summary;
    std::vector<int64_t> result_ids;
};

ImageSearchController::ImageSearchController(DataManager *data_manager, QObject *parent)
    : QObject(parent)
    , data_manager_(data_manager)
{
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

QString ImageSearchController::defaultInferRtRoot() const
{
    return QString::fromLatin1(kInferRtRoot);
}

QString ImageSearchController::defaultModelName() const
{
    return QString::fromLatin1(irt::features::ImageSearch::kDefaultModelName);
}

QString ImageSearchController::defaultFeatureName() const
{
    return QString::fromLatin1(irt::features::ImageSearch::kDefaultFeatureName);
}

QStringList ImageSearchController::supportedModelPresets() const
{
    return {
        QStringLiteral("resnet18"),
        QStringLiteral("resnet34"),
        QStringLiteral("resnet50"),
        QStringLiteral("mobilenet_v3_small"),
        QStringLiteral("mobilenet_v3_large"),
        QStringLiteral("dinov2_vits14"),
        QStringLiteral("dinov2_vitb14"),
        QStringLiteral("dinov3_vits16"),
        QStringLiteral("vit_base_patch16_224"),
        QStringLiteral("onnx"),
    };
}

QStringList ImageSearchController::modelFeatureNames(const QString &model_name) const
{
    const QString model = model_name.trimmed().isEmpty() ? defaultModelName() : model_name.trimmed();

    QStringList names;
    const std::vector<std::string> feature_names = irt::model::modelFeatureNames(model.toStdString());
    for (const std::string &feature_name : feature_names)
    {
        const QString name = QString::fromStdString(feature_name);
        if (!name.isEmpty() && !names.contains(name))
        {
            names.append(name);
        }
    }

    if (names.isEmpty())
    {
        names.append(defaultFeatureName());
    }

    return names;
}

QString ImageSearchController::suggestedWeightsPath(const QString &model_name) const
{
    const QString model = model_name.trimmed().isEmpty() ? defaultModelName() : model_name.trimmed();
    return QDir(QStringLiteral("F:/models")).filePath(model + QStringLiteral(".wts"));
}

// ────────────────────────────────────────────────────────────
//  公开接口
// ────────────────────────────────────────────────────────────

bool ImageSearchController::searchSelectedImages(const QVariantList &dataset_ids, const QString &model_name,
                                                 const QString &weights_file, const QString &feature_name,
                                                 bool rebuild_index, int top_k, const QString &norm,
                                                 const QString &preprocess_backend, const QString &faiss_backend,
                                                 const QString &index_storage, int disk_build_batch_size,
                                                 const QString &model_backend, const QString &model_device)
{
    // 1. 前置校验
    if (running_)
    {
        setLastError(QStringLiteral("图像搜索正在运行"));
        return false;
    }
    if (!data_manager_ || !data_manager_->imageInstances())
    {
        setLastError(QStringLiteral("图像模型未初始化"));
        return false;
    }

    ImageInstancesListModel *images     = data_manager_->imageInstances();
    const auto               query_ids  = images->getSelectedImagesId();
    if (query_ids.empty())
    {
        setLastError(QStringLiteral("请先选择要检索的图片"));
        return false;
    }

    // 2. 解析参数 & 构建请求
    const auto dataset_ids_set = parseDatasetIds(dataset_ids);
    if (!validateWeightsFile(weights_file))
        return false;

    SearchRequest request = buildSearchRequest(model_name, weights_file, feature_name, rebuild_index,
                                               top_k, norm, preprocess_backend, faiss_backend,
                                               index_storage, disk_build_batch_size, model_backend, model_device);

    // 3. 收集图像
    collectGalleryImages(request, dataset_ids_set);
    if (request.gallery_images.empty())
    {
        setLastError(QStringLiteral("选定数据集中没有可搜索的图像"));
        return false;
    }

    collectQueryImages(request, query_ids);
    if (request.query_images.empty())
    {
        setLastError(QStringLiteral("选中的查询图片文件不存在"));
        return false;
    }

    // 4. 计算索引路径
    request.index_file = computeIndexPath(request);

    // 5. 清理状态并启动后台搜索
    resetForNewSearch();
    startProgress(request);

    QPointer<ImageSearchController> controller(this);
    QThread *work_thread = QThread::create(
        [controller, request = std::move(request)]() mutable
        { executeSearchWorker(std::move(request), controller); });

    connect(work_thread, &QThread::finished, work_thread, &QObject::deleteLater);
    work_thread->start();
    return true;
}

// ────────────────────────────────────────────────────────────
//  参数解析与校验
// ────────────────────────────────────────────────────────────

std::set<int64_t> ImageSearchController::parseDatasetIds(const QVariantList &dataset_ids)
{
    std::set<int64_t> result;
    for (const QVariant &value : dataset_ids)
    {
        bool    ok         = false;
        int64_t dataset_id = value.toLongLong(&ok);
        if (ok)
        {
            result.insert(dataset_id);
        }
    }
    return result;
}

bool ImageSearchController::validateWeightsFile(const QString &path)
{
    QFileInfo info(path);
    if (path.trimmed().isEmpty() || !info.isFile())
    {
        setLastError(QStringLiteral("模型权重文件不存在: %1").arg(path));
        return false;
    }
    return true;
}

ImageSearchController::SearchRequest ImageSearchController::buildSearchRequest(
    const QString &model_name, const QString &weights_file, const QString &feature_name,
    bool rebuild_index, int top_k, const QString &norm, const QString &preprocess_backend,
    const QString &faiss_backend, const QString &index_storage, int disk_build_batch_size,
    const QString &model_backend, const QString &model_device)
{
    const auto effective = [](const QString &value, const QString &fallback) -> QString
    { return value.trimmed().isEmpty() ? fallback : value.trimmed(); };

    const QString effective_norm    = effective(norm, QStringLiteral("l2")).toLower();
    QString       effective_faiss   = effective(faiss_backend, QStringLiteral("cpu")).toLower();
    QString       effective_storage = effective(index_storage, QStringLiteral("ram")).toLower();
    if (effective_faiss == QStringLiteral("gpu"))
        effective_storage = QStringLiteral("ram");

    QFileInfo weights_info(weights_file);

    SearchRequest req;
    req.model_name    = effective(model_name, QString::fromLatin1(irt::features::ImageSearch::kDefaultModelName));
    req.feature_name  = effective(feature_name, QString::fromLatin1(irt::features::ImageSearch::kDefaultFeatureName));
    req.weights_file  = weights_info.absoluteFilePath();
    req.rebuild_index = rebuild_index;
    req.top_k         = std::max(1, top_k);
    req.disk_build_batch_size = static_cast<int>(std::max(1, disk_build_batch_size));
    req.norm                  = effective_norm;
    req.faiss_backend         = effective_faiss;
    req.index_storage         = effective_storage;
    req.preprocess_backend    = parsePreprocessBackend(preprocess_backend);
    req.norm_mode             = parseNorm(effective_norm);
    req.faiss_backend_mode    = parseFaissBackend(effective_faiss);
    req.index_storage_mode    = parseIndexStorage(effective_storage);
    req.model_backend         = parseModelBackend(model_backend);
    req.model_device          = parseModelDevice(model_device);
    req.model_backend_str     = model_backend.trimmed().toLower();
    req.model_device_str      = model_device.trimmed().toLower();
    return req;
}

// ────────────────────────────────────────────────────────────
//  图像收集
// ────────────────────────────────────────────────────────────

void ImageSearchController::collectGalleryImages(SearchRequest            &request,
                                                  const std::set<int64_t> &dataset_ids)
{
    ImageInstancesListModel *images = data_manager_->imageInstances();
    const auto               all_ids = images->getAllImageIds();

    for (const int64_t id : all_ids)
    {
        if (!dataset_ids.empty() && dataset_ids.find(images->getImageDatasetId(id)) == dataset_ids.end())
            continue;

        const QString path = images->getImagePath(id);
        if (!QFileInfo::exists(path))
            continue;

        request.gallery_images.push_back(toFsPath(QFileInfo(path).absoluteFilePath()));
        request.path_to_image_id.insert(normalizedPathKey(path), id);
    }
}

void ImageSearchController::collectQueryImages(SearchRequest                 &request,
                                                const std::vector<int64_t> &query_ids) const
{
    ImageInstancesListModel *images = data_manager_->imageInstances();
    for (const int64_t id : query_ids)
    {
        const QString path = images->getImagePath(id);
        if (QFileInfo::exists(path))
            request.query_images.push_back(toFsPath(QFileInfo(path).absoluteFilePath()));
    }
}

// ────────────────────────────────────────────────────────────
//  索引路径
// ────────────────────────────────────────────────────────────

QString ImageSearchController::computeIndexPath(const SearchRequest &request) const
{
    std::vector<int64_t> gallery_ids;
    gallery_ids.reserve(request.path_to_image_id.size());
    for (auto it = request.path_to_image_id.cbegin(); it != request.path_to_image_id.cend(); ++it)
        gallery_ids.push_back(it.value());
    std::sort(gallery_ids.begin(), gallery_ids.end());

    // 从 gallery_images 中反推 dataset IDs（保持原逻辑兼容）
    std::set<int64_t> dataset_ids;
    ImageInstancesListModel *images = data_manager_->imageInstances();
    for (const int64_t id : gallery_ids)
        dataset_ids.insert(images->getImageDatasetId(id));

    const std::vector<int64_t> sorted_dataset_ids(dataset_ids.begin(), dataset_ids.end());
    const QString index_dir = indexDirectoryForProject(
        data_manager_->databasePath(),
        dltool::settings::GlobalSettings::getInstance()->data()->featureExtractionIndexDirectory());

    return indexPathForRequest(index_dir, sorted_dataset_ids, gallery_ids,
                               request.model_name, request.feature_name, request.norm,
                               request.faiss_backend, request.index_storage,
                               request.model_backend_str, request.model_device_str);
}

// ────────────────────────────────────────────────────────────
//  后台搜索执行
// ────────────────────────────────────────────────────────────

void ImageSearchController::executeSearchWorker(SearchRequest                     request,
                                                 QPointer<ImageSearchController> controller)
{
    auto reportProgress = [&controller](const irt::features::ImageSearchBuildProgress &progress)
    {
        const int processed = static_cast<int>(progress.processed_count);
        const int total     = static_cast<int>(progress.total_count);

        if (controller)
        {
            QMetaObject::invokeMethod(controller.data(),
                                      [controller, processed, total]() {
                                          if (controller)
                                              emit controller->buildProgressChanged(processed, total);
                                      },
                                      Qt::QueuedConnection);
        }

        if (total > 0)
        {
            const int pct = std::min(100, static_cast<int>(processed * 100 / total));
            QMetaObject::invokeMethod(ui::ProgressManager::getInstance(),
                                      "updateProgress", Qt::QueuedConnection, Q_ARG(int, pct));
        }

        addProgressMessage(spdlog::level::info,
                           QStringLiteral("构建进度 [%1]: %2 / %3")
                               .arg(QString::fromUtf8(irt::features::imageSearchBuildStageName(progress.stage)))
                               .arg(processed)
                               .arg(total));
    };

    SearchResponse response;
    try
    {
        irt::features::ImageSearchConfig config;
        config.model_name            = request.model_name.toStdString();
        config.feature_name          = request.feature_name.toStdString();
        config.preprocess_backend    = request.preprocess_backend;
        config.norm                  = request.norm_mode;
        config.faiss_backend         = request.faiss_backend_mode;
        config.index_storage         = request.index_storage_mode;
        config.disk_build_batch_size = static_cast<size_t>(request.disk_build_batch_size);
        config.model_backend         = request.model_backend;
        config.model_device          = request.model_device;

        irt::features::ImageSearch search(config);
        const auto weights_path = toFsPath(request.weights_file);
        const auto index_path   = toFsPath(request.index_file);

        bool loaded = false;
        if (!request.rebuild_index && std::filesystem::exists(index_path))
        {
            try
            {
                search.load(weights_path, index_path.parent_path(), index_path);
                loaded = true;
                addProgressMessage(spdlog::level::info,
                                   QStringLiteral("已加载图像搜索特征库: %1").arg(request.index_file));
            }
            catch (const std::exception &e)
            {
                addProgressMessage(spdlog::level::warn,
                                   QStringLiteral("加载既有特征库失败，将重新构建: %1")
                                       .arg(QString::fromUtf8(e.what())));
            }
        }

        if (!loaded)
        {
            addProgressMessage(spdlog::level::info,
                               QStringLiteral("正在构建图像搜索特征库: %1 张图像")
                                   .arg(request.gallery_images.size()));
            search.build(weights_path, request.gallery_images, index_path, reportProgress);
        }

        // 对每张查询图执行检索
        std::map<int64_t, float> result_scores;
        for (const auto &query_image : request.query_images)
        {
            for (const auto &result : search.search(query_image, request.top_k))
            {
                const auto found = request.path_to_image_id.constFind(
                    normalizedPathKey(fromFsPath(result.image_path)));
                if (found == request.path_to_image_id.constEnd())
                    continue;

                const int64_t image_id = found.value();
                auto          it       = result_scores.find(image_id);
                if (it == result_scores.end() || result.score > it->second)
                    result_scores[image_id] = result.score;
            }
        }

        // 按分数降序排列
        std::vector<std::pair<int64_t, float>> sorted(result_scores.begin(), result_scores.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto &lhs, const auto &rhs) { return lhs.second > rhs.second; });

        response.result_ids.reserve(sorted.size());
        for (const auto &[id, _] : sorted)
            response.result_ids.push_back(id);

        response.success = true;
        response.summary = QStringLiteral("图像搜索完成: 命中 %1 张图像").arg(response.result_ids.size());
    }
    catch (const std::exception &e)
    {
        response.success = false;
        response.error   = QString::fromUtf8(e.what());
    }
    catch (...)
    {
        response.success = false;
        response.error   = QStringLiteral("未知图像搜索错误");
    }

    if (controller)
    {
        QMetaObject::invokeMethod(controller.data(),
                                  [controller, response]() {
                                      if (controller)
                                          controller->finishSearch(response);
                                  },
                                  Qt::QueuedConnection);
    }
}

// ────────────────────────────────────────────────────────────
//  UI 反馈
// ────────────────────────────────────────────────────────────

void ImageSearchController::resetForNewSearch()
{
    setLastError(QString());
    last_summary_.clear();
    result_count_ = 0;
    if (data_manager_ && data_manager_->globalFilter())
        data_manager_->globalFilter()->clearImageSearchResults();
    emit resultsChanged();
}

void ImageSearchController::startProgress(const SearchRequest &request)
{
    setRunning(true);
    ui::ProgressManager::getInstance()->startTask(QStringLiteral("图像搜索"));
    ui::ProgressManager::getInstance()->addMessage(
        spdlog::level::info,
        QStringLiteral("开始图像搜索: 查询 %1 张, 图库 %2 张, TopK=%3")
            .arg(request.query_images.size())
            .arg(request.gallery_images.size())
            .arg(request.top_k));
}

void ImageSearchController::finishProgress(bool success, const QString &message)
{
    const int level = success ? spdlog::level::info : spdlog::level::err;
    ui::ProgressManager::getInstance()->addMessage(level, message);
    ui::ProgressManager::getInstance()->completeTask();
}

void ImageSearchController::finishSearch(const SearchResponse &response)
{
    setRunning(false);

    if (!response.success)
    {
        result_count_ = 0;
        last_summary_.clear();
        emit resultsChanged();
        setLastError(response.error);
        finishProgress(false, response.error);
        return;
    }

    setLastError(QString());
    result_count_  = static_cast<int>(response.result_ids.size());
    last_summary_  = response.summary;

    if (data_manager_ && data_manager_->globalFilter())
        data_manager_->globalFilter()->setImageSearchResults(response.result_ids, !response.result_ids.empty());

    finishProgress(true, response.summary);
    emit resultsChanged();
}

void ImageSearchController::setRunning(bool running)
{
    if (running_ == running)
    {
        return;
    }
    running_ = running;
    emit runningChanged();
}

void ImageSearchController::setLastError(const QString &last_error)
{
    if (last_error_ == last_error)
    {
        return;
    }
    last_error_ = last_error;
    if (!last_error_.isEmpty())
    {
        spdlog::error("图像搜索失败: {}", last_error_.toStdString());
    }
    emit lastErrorChanged();
}

} // namespace dltool::data
