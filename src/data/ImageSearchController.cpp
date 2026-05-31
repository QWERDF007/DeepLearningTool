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

bool ImageSearchController::searchSelectedImages(const QVariantList &dataset_ids, const QString &model_name,
                                                 const QString &weights_file, const QString &feature_name,
                                                 bool rebuild_index, int top_k, const QString &norm,
                                                 const QString &preprocess_backend, const QString &faiss_backend,
                                                 const QString &index_storage, int disk_build_batch_size,
                                                 const QString &model_backend, const QString &model_device)
{
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

    ImageInstancesListModel *image_model = data_manager_->imageInstances();
    const std::vector<int64_t> query_image_ids = image_model->getSelectedImagesId();
    if (query_image_ids.empty())
    {
        setLastError(QStringLiteral("请先选择要检索的图片"));
        return false;
    }

    clearPreviousResultsForNewSearch();

    const QString effective_model_name
        = model_name.trimmed().isEmpty() ? QString::fromLatin1(irt::features::ImageSearch::kDefaultModelName)
                                         : model_name.trimmed();
    const QString effective_feature_name
        = feature_name.trimmed().isEmpty() ? QString::fromLatin1(irt::features::ImageSearch::kDefaultFeatureName)
                                           : feature_name.trimmed();
    const QString effective_norm          = norm.trimmed().isEmpty() ? QStringLiteral("l2") : norm.trimmed().toLower();
    const QString effective_faiss_backend = faiss_backend.trimmed().isEmpty() ? QStringLiteral("cpu")
                                                                              : faiss_backend.trimmed().toLower();
    QString       effective_index_storage = index_storage.trimmed().isEmpty() ? QStringLiteral("ram")
                                                                              : index_storage.trimmed().toLower();
    if (effective_faiss_backend == QStringLiteral("gpu"))
    {
        effective_index_storage = QStringLiteral("ram");
    }

    QFileInfo weights_info(weights_file);
    if (weights_file.trimmed().isEmpty() || !weights_info.isFile())
    {
        setLastError(QStringLiteral("模型权重文件不存在: %1").arg(weights_file));
        return false;
    }

    std::set<int64_t> selected_dataset_ids;
    for (const QVariant &value : dataset_ids)
    {
        bool    ok         = false;
        int64_t dataset_id = value.toLongLong(&ok);
        if (ok)
        {
            selected_dataset_ids.insert(dataset_id);
        }
    }

    std::vector<int64_t> sorted_dataset_ids(selected_dataset_ids.begin(), selected_dataset_ids.end());
    std::vector<int64_t> gallery_image_ids;
    SearchRequest        request;
    request.model_name              = effective_model_name;
    request.feature_name            = effective_feature_name;
    request.weights_file            = weights_info.absoluteFilePath();
    request.rebuild_index           = rebuild_index;
    request.top_k                   = std::max(1, top_k);
    request.disk_build_batch_size   = static_cast<int>(std::max(1, disk_build_batch_size));
    request.norm                    = effective_norm;
    request.faiss_backend           = effective_faiss_backend;
    request.index_storage           = effective_index_storage;
    request.norm_mode               = parseNorm(effective_norm);
    request.preprocess_backend      = parsePreprocessBackend(preprocess_backend);
    request.faiss_backend_mode      = parseFaissBackend(effective_faiss_backend);
    request.index_storage_mode      = parseIndexStorage(effective_index_storage);
    request.model_backend           = parseModelBackend(model_backend);
    request.model_device            = parseModelDevice(model_device);
    request.model_backend_str       = model_backend.trimmed().toLower();
    request.model_device_str        = model_device.trimmed().toLower();

    const std::vector<int64_t> all_image_ids = image_model->getAllImageIds();
    for (const int64_t image_id : all_image_ids)
    {
        const int64_t dataset_id = image_model->getImageDatasetId(image_id);
        if (!selected_dataset_ids.empty() && selected_dataset_ids.find(dataset_id) == selected_dataset_ids.end())
        {
            continue;
        }

        const QString image_path = image_model->getImagePath(image_id);
        if (!QFileInfo::exists(image_path))
        {
            continue;
        }

        request.gallery_images.push_back(toFsPath(QFileInfo(image_path).absoluteFilePath()));
        request.path_to_image_id.insert(normalizedPathKey(image_path), image_id);
        gallery_image_ids.push_back(image_id);
    }

    if (request.gallery_images.empty())
    {
        setLastError(QStringLiteral("选定数据集中没有可搜索的图像"));
        return false;
    }

    for (const int64_t query_image_id : query_image_ids)
    {
        const QString query_path = image_model->getImagePath(query_image_id);
        if (QFileInfo::exists(query_path))
        {
            request.query_images.push_back(toFsPath(QFileInfo(query_path).absoluteFilePath()));
        }
    }

    if (request.query_images.empty())
    {
        setLastError(QStringLiteral("选中的查询图片文件不存在"));
        return false;
    }

    std::sort(gallery_image_ids.begin(), gallery_image_ids.end());
    QString index_dir = indexDirectoryForProject(data_manager_->databasePath(),
                                                   dltool::settings::GlobalSettings::getInstance()->data()->featureExtractionIndexDirectory());
    request.index_file
        = indexPathForRequest(index_dir, sorted_dataset_ids, gallery_image_ids,
                              request.model_name, request.feature_name, request.norm, request.faiss_backend,
                              request.index_storage, request.model_backend_str, request.model_device_str);

    setLastError(QString());
    last_summary_.clear();
    result_count_ = 0;
    emit resultsChanged();
    setRunning(true);

    ui::ProgressManager::getInstance()->startTask(QStringLiteral("图像搜索"));
    ui::ProgressManager::getInstance()->addMessage(
        spdlog::level::info,
        QStringLiteral("开始图像搜索: 查询 %1 张, 图库 %2 张, TopK=%3")
            .arg(request.query_images.size())
            .arg(request.gallery_images.size())
            .arg(request.top_k));

    QPointer<ImageSearchController> controller(this);
    QThread                        *worker_thread = QThread::create(
        [controller, request = std::move(request)]() mutable
        {
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

                const std::filesystem::path weights_file = toFsPath(request.weights_file);
                const std::filesystem::path index_file   = toFsPath(request.index_file);

                bool loaded_existing_index = false;
                if (!request.rebuild_index && std::filesystem::exists(index_file))
                {
                    try
                    {
                        search.load(weights_file, index_file.parent_path(), index_file);
                        loaded_existing_index = true;
                        addProgressMessage(spdlog::level::info,
                                           QStringLiteral("已加载图像搜索特征库: %1").arg(request.index_file));
                    }
                    catch (const std::exception &e)
                    {
                        addProgressMessage(
                            spdlog::level::warn,
                            QStringLiteral("加载既有特征库失败，将重新构建: %1").arg(QString::fromUtf8(e.what())));
                    }
                }

                if (!loaded_existing_index)
                {
                    addProgressMessage(spdlog::level::info,
                                       QStringLiteral("正在构建图像搜索特征库: %1 张图像")
                                           .arg(request.gallery_images.size()));
                    search.build(
                        weights_file, request.gallery_images, index_file,
                        [&](const irt::features::ImageSearchBuildProgress &progress)
                        {
                            const int processed = static_cast<int>(progress.processed_count);
                            const int total     = static_cast<int>(progress.total_count);

                            if (controller)
                            {
                                QMetaObject::invokeMethod(
                                    controller.data(),
                                    [controller, processed, total]()
                                    {
                                        if (controller)
                                        {
                                            emit controller->buildProgressChanged(processed, total);
                                        }
                                    },
                                    Qt::QueuedConnection);
                            }

                            if (total > 0)
                            {
                                const int pct = std::min(100, static_cast<int>(processed * 100 / total));
                                QMetaObject::invokeMethod(
                                    ui::ProgressManager::getInstance(), "updateProgress", Qt::QueuedConnection,
                                    Q_ARG(int, pct));
                            }

                            addProgressMessage(
                                spdlog::level::info,
                                QStringLiteral("构建进度 [%1]: %2 / %3")
                                    .arg(QString::fromUtf8(
                                        irt::features::imageSearchBuildStageName(progress.stage)))
                                    .arg(processed)
                                    .arg(total));
                        });
                }

                std::map<int64_t, float> result_scores;
                for (const std::filesystem::path &query_image : request.query_images)
                {
                    const std::vector<irt::features::ImageSearchResult> results
                        = search.search(query_image, request.top_k);
                    for (const irt::features::ImageSearchResult &result : results)
                    {
                        const QString result_path = fromFsPath(result.image_path);
                        const QString key         = normalizedPathKey(result_path);
                        auto          found       = request.path_to_image_id.constFind(key);
                        if (found == request.path_to_image_id.constEnd())
                        {
                            continue;
                        }

                        const int64_t image_id = found.value();
                        auto          score_it = result_scores.find(image_id);
                        if (score_it == result_scores.end() || result.score > score_it->second)
                        {
                            result_scores[image_id] = result.score;
                        }
                    }
                }

                std::vector<std::pair<int64_t, float>> sorted_results;
                sorted_results.reserve(result_scores.size());
                for (const auto &[image_id, score] : result_scores)
                {
                    sorted_results.emplace_back(image_id, score);
                }
                std::sort(sorted_results.begin(), sorted_results.end(),
                          [](const auto &lhs, const auto &rhs) { return lhs.second > rhs.second; });

                response.result_ids.reserve(sorted_results.size());
                for (const auto &[image_id, _] : sorted_results)
                {
                    response.result_ids.push_back(image_id);
                }

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
                QMetaObject::invokeMethod(
                    controller.data(),
                    [controller, response]()
                    {
                        if (controller)
                        {
                            controller->finishSearch(response);
                        }
                    },
                    Qt::QueuedConnection);
            }
        });

    connect(worker_thread, &QThread::finished, worker_thread, &QObject::deleteLater);
    worker_thread->start();
    return true;
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

void ImageSearchController::finishSearch(const SearchResponse &response)
{
    setRunning(false);

    if (!response.success)
    {
        result_count_ = 0;
        last_summary_.clear();
        emit resultsChanged();
        setLastError(response.error);
        ui::ProgressManager::getInstance()->addMessage(spdlog::level::err, response.error);
        ui::ProgressManager::getInstance()->completeTask();
        return;
    }

    setLastError(QString());
    result_count_ = static_cast<int>(response.result_ids.size());
    last_summary_ = response.summary;

    if (data_manager_ && data_manager_->globalFilter())
    {
        data_manager_->globalFilter()->setImageSearchResults(response.result_ids, !response.result_ids.empty());
    }

    ui::ProgressManager::getInstance()->addMessage(spdlog::level::info, response.summary);
    ui::ProgressManager::getInstance()->completeTask();

    emit resultsChanged();
}

void ImageSearchController::clearPreviousResultsForNewSearch()
{
    result_count_ = 0;
    last_summary_.clear();
    if (data_manager_ && data_manager_->globalFilter())
    {
        data_manager_->globalFilter()->clearImageSearchResults();
    }
    emit resultsChanged();
}

} // namespace dltool::data
