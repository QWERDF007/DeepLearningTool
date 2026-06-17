#include "feature/ImageSearchController.h"

#include "feature/ImageSearchDataProvider.h"
#include "settings/GlobalSettings.h"
#include "ui/ProgressManager.h"

#include <inferrt/features/ImageSearch.hpp>
#include <inferrt/features/RoiSearch.hpp>
#include <inferrt/model/ModelFeatures.hpp>
#include <spdlog/spdlog.h>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace dltool::feature {

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

QString normalizedModelName(QString model_name, const QString &fallback)
{
    model_name = model_name.trimmed().toLower();
    return model_name.isEmpty() ? fallback.toLower() : model_name;
}

bool startsWithAny(const QString &value, std::initializer_list<const char *> prefixes)
{
    for (const char *prefix : prefixes)
    {
        if (value.startsWith(QString::fromLatin1(prefix)))
        {
            return true;
        }
    }
    return false;
}

bool containsAny(const QString &value, std::initializer_list<const char *> fragments)
{
    for (const char *fragment : fragments)
    {
        if (value.contains(QString::fromLatin1(fragment)))
        {
            return true;
        }
    }
    return false;
}

bool isKnownTokenFeatureModel(const QString &model_name, const QString &fallback_model)
{
    const QString model = normalizedModelName(model_name, fallback_model);
    return startsWithAny(model, {"vit_"}) || model == "vit";
}

bool isKnownDinoRoiModel(const QString &model_name, const QString &fallback_model)
{
    const QString model = normalizedModelName(model_name, fallback_model);
    return startsWithAny(model, {"dinov2_", "dinov3_", "timmdinov2_", "timmdinov3_"})
        || containsAny(model, {"_dinov2", "_dinov3"});
}

bool isKnownUnsupportedRoiModel(const QString &model_name, const QString &fallback_model)
{
    const QString model = normalizedModelName(model_name, fallback_model);
    return model == QStringLiteral("onnx") || isKnownTokenFeatureModel(model, fallback_model);
}

QStringList knownRoiFeatureNames(const QString &model_name, const QString &fallback_model)
{
    const QString model = normalizedModelName(model_name, fallback_model);

    if (isKnownDinoRoiModel(model, fallback_model))
    {
        return {QStringLiteral("x_norm_patchtokens")};
    }

    if (startsWithAny(model, {"resnet", "wide_resnet"}))
    {
        return {QStringLiteral("layer1"), QStringLiteral("layer2"), QStringLiteral("layer3"), QStringLiteral("layer4")};
    }

    if (startsWithAny(model, {"mobilenet_v2", "mobilenet_v3"}) || containsAny(model, {"mobilenetv2", "mobilenetv3"}))
    {
        return {QStringLiteral("features.5"), QStringLiteral("features.11"), QStringLiteral("features.16")};
    }

    QStringList                    result;
    const std::vector<std::string> feature_names = irt::model::modelFeatureNames(model.toStdString());
    for (const std::string &feature_name : feature_names)
    {
        const QString name  = QString::fromStdString(feature_name);
        const QString lower = name.toLower();
        if (lower == QStringLiteral("x_norm_patchtokens"))
        {
            result.append(name);
            continue;
        }
        if (name.isEmpty() || containsAny(lower, {"avgpool", "flatten", "classifier", "fc", "cls", "token", "norm"}))
        {
            continue;
        }
        if (lower.startsWith(QStringLiteral("layer")) || lower.startsWith(QStringLiteral("features."))
            || lower.startsWith(QStringLiteral("backbone.feature"))
            || lower.startsWith(QStringLiteral("image_encoder.features.")))
        {
            result.append(name);
        }
    }
    return result;
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

QString indexDirectoryForProject(const QString &database_path, const QString &custom_directory,
                                 const QString &default_subdirectory)
{
    if (!custom_directory.trimmed().isEmpty())
    {
        return QDir::cleanPath(custom_directory.trimmed());
    }

    if (!database_path.isEmpty())
    {
        return QFileInfo(database_path).absoluteDir().filePath(default_subdirectory);
    }

    QString fallback = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (fallback.isEmpty())
    {
        fallback = QDir::tempPath();
    }
    return QDir(fallback).filePath(default_subdirectory);
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
        = QString("%1_%2_%3_%4_%5_%6_%7_%8.faiss")
              .arg(sanitizeFilePart(model_name), sanitizeFilePart(feature_name), sanitizeFilePart(norm),
                   sanitizeFilePart(faiss_backend), sanitizeFilePart(index_storage), sanitizeFilePart(model_backend),
                   sanitizeFilePart(model_device), key);
    return index_dir.filePath(file_name);
}

QString roiIndexPathForRequest(const QString &index_dir_path, const std::vector<int64_t> &dataset_ids,
                               const std::vector<int64_t> &gallery_label_ids, const QString &model_name,
                               const QString &feature_name, const QString &norm, const QString &faiss_backend,
                               const QString &index_storage, const QString &model_backend, const QString &model_device,
                               int pooled_height, int pooled_width, int sampling_ratio, bool aligned, bool use_pca,
                               int pca_dim)
{
    QDir index_dir(index_dir_path);
    if (!index_dir.exists())
    {
        index_dir.mkpath(QStringLiteral("."));
    }

    const QString key = datasetHash(dataset_ids, gallery_label_ids);
    const QString file_name
        = QString("%1_%2_%3_%4_%5_%6_%7_ph%8_pw%9_sr%10_al%11_pca%12_pd%13_%14.roi.faiss")
              .arg(sanitizeFilePart(model_name), sanitizeFilePart(feature_name), sanitizeFilePart(norm),
                   sanitizeFilePart(faiss_backend), sanitizeFilePart(index_storage), sanitizeFilePart(model_backend),
                   sanitizeFilePart(model_device))
              .arg(pooled_height)
              .arg(pooled_width)
              .arg(sampling_ratio)
              .arg(aligned ? 1 : 0)
              .arg(use_pca ? 1 : 0)
              .arg(use_pca ? pca_dim : 0)
              .arg(key);
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
    return backend.trimmed().toLower() == QStringLiteral("gpu") ? irt::features::ImageSearchPreprocessBackend::GPU
                                                                : irt::features::ImageSearchPreprocessBackend::CPU;
}

irt::features::ImageSearchFaissBackend parseFaissBackend(const QString &backend)
{
    return backend.trimmed().toLower() == QStringLiteral("gpu") ? irt::features::ImageSearchFaissBackend::GPU
                                                                : irt::features::ImageSearchFaissBackend::CPU;
}

irt::features::ImageSearchIndexStorage parseIndexStorage(const QString &storage)
{
    return storage.trimmed().toLower() == QStringLiteral("disk") ? irt::features::ImageSearchIndexStorage::Disk
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
    return device.trimmed().toLower() == QStringLiteral("cpu") ? irt::model::ModelDevice::CPU
                                                               : irt::model::ModelDevice::GPU;
}

QString progressTimestamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hh:mm:ss"));
}

QString withProgressTimestamp(const QString &message)
{
    return QString("[%1] %2").arg(progressTimestamp(), message);
}

QString formatElapsed(qint64 elapsed_ms)
{
    const qint64 hours        = elapsed_ms / 3600000;
    const qint64 minutes      = (elapsed_ms / 60000) % 60;
    const qint64 seconds      = (elapsed_ms / 1000) % 60;
    const qint64 milliseconds = elapsed_ms % 1000;

    return QString("%1:%2:%3.%4")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(milliseconds, 3, 10, QChar('0'));
}

bool isOneStepStage(irt::features::ImageSearchBuildStage stage)
{
    using Stage = irt::features::ImageSearchBuildStage;
    switch (stage)
    {
    case Stage::LoadingModel:
    case Stage::TrainingIndex:
    case Stage::WritingIndex:
    case Stage::LoadingIndex:
    case Stage::SavingMetadata:
        return true;
    default:
        return false;
    }
}

bool resolveProgressCount(const irt::features::ImageSearchBuildProgress &progress, size_t gallery_count,
                          size_t &processed, size_t &total)
{
    using Stage = irt::features::ImageSearchBuildStage;

    if (progress.stage == Stage::Started || progress.stage == Stage::Unknown)
    {
        return false;
    }

    if (progress.stage == Stage::Finished && gallery_count > 0)
    {
        processed = gallery_count;
        total     = gallery_count;
        return true;
    }

    if (progress.total_count > 0 && progress.processed_count <= progress.total_count)
    {
        processed = progress.processed_count;
        total     = progress.total_count;
        return true;
    }

    if (progress.batch_count > 0 && gallery_count > 0)
    {
        processed = std::min(gallery_count, progress.batch_begin + progress.batch_count);
        total     = gallery_count;
        return true;
    }

    if (progress.stage == Stage::CollectingImages && gallery_count > 0
        && progress.processed_count > progress.total_count)
    {
        processed = gallery_count;
        total     = gallery_count;
        return true;
    }

    return false;
}

QString formatBuildProgressMessage(const irt::features::ImageSearchBuildProgress &progress, size_t gallery_count)
{
    using Stage = irt::features::ImageSearchBuildStage;

    if (progress.stage == Stage::Unknown)
    {
        return {};
    }

    const QString stage_name = QString::fromUtf8(irt::features::imageSearchBuildStageName(progress.stage));

    size_t processed = 0;
    size_t total     = 0;
    if (resolveProgressCount(progress, gallery_count, processed, total))
    {
        return QString("构建进度 [%1]: %2 / %3").arg(stage_name).arg(processed).arg(total);
    }

    if (progress.stage == Stage::Started || isOneStepStage(progress.stage) || progress.stage == Stage::CollectingImages)
    {
        return QString("构建阶段 [%1]").arg(stage_name);
    }

    return {};
}

int progressPercent(const irt::features::ImageSearchBuildProgress &progress, size_t gallery_count)
{
    size_t processed = 0;
    size_t total     = 0;
    if (!resolveProgressCount(progress, gallery_count, processed, total) || total == 0)
    {
        return -1;
    }
    return std::min(100, static_cast<int>(processed * 100 / total));
}

void addProgressMessage(int level, const QString &message)
{
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::AutoConnection, Q_ARG(int, level),
                              Q_ARG(QString, withProgressTimestamp(message)));
}

bool roiFromLabelData(const QVariantMap &data, irt::features::RoiSearchBox &box)
{
    bool ok_x = false;
    bool ok_y = false;
    bool ok_w = false;
    bool ok_h = false;

    const double x = data.value(QStringLiteral("x")).toDouble(&ok_x);
    const double y = data.value(QStringLiteral("y")).toDouble(&ok_y);
    const double w = data.value(QStringLiteral("width")).toDouble(&ok_w);
    const double h = data.value(QStringLiteral("height")).toDouble(&ok_h);
    if (!ok_x || !ok_y || !ok_w || !ok_h)
    {
        return false;
    }
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h) || w <= 1.0 || h <= 1.0)
    {
        return false;
    }

    const double x2 = x + w;
    const double y2 = y + h;
    if (!std::isfinite(x2) || !std::isfinite(y2) || x2 <= x || y2 <= y)
    {
        return false;
    }

    box.x1 = static_cast<float>(x);
    box.y1 = static_cast<float>(y);
    box.x2 = static_cast<float>(x2);
    box.y2 = static_cast<float>(y2);
    return true;
}

} // namespace

struct ImageSearchController::SearchRequest
{
    enum class Mode
    {
        Image,
        Roi,
    };

    Mode mode{Mode::Image};

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
    int  model_batch_size{1};

    irt::features::ImageSearchPreprocessBackend preprocess_backend{irt::features::ImageSearchPreprocessBackend::CPU};
    irt::features::ImageSearchFeatureNorm       norm_mode{irt::features::ImageSearchFeatureNorm::L2};
    irt::features::ImageSearchFaissBackend      faiss_backend_mode{irt::features::ImageSearchFaissBackend::CPU};
    irt::features::ImageSearchIndexStorage      index_storage_mode{irt::features::ImageSearchIndexStorage::RAM};
    irt::model::ModelBackend                    model_backend{irt::model::ModelBackend::TensorRT};
    irt::model::ModelDevice                     model_device{irt::model::ModelDevice::GPU};

    QString                               model_backend_str;
    QString                               model_device_str;
    std::chrono::steady_clock::time_point started_at;

    std::vector<std::filesystem::path> query_images;
    std::vector<std::filesystem::path> gallery_images;
    QHash<QString, int64_t>            path_to_image_id;

    int  pooled_height{7};
    int  pooled_width{7};
    int  sampling_ratio{-1};
    bool aligned{false};
    bool use_pca{false};
    int  pca_dim{0};

    std::vector<irt::features::RoiSearchItem> query_rois;
    std::vector<irt::features::RoiSearchItem> gallery_rois;
    std::vector<int64_t>                      gallery_roi_label_ids;
    std::vector<int64_t>                      gallery_roi_image_ids;
};

struct ImageSearchController::SearchResponse
{
    SearchRequest::Mode  mode{SearchRequest::Mode::Image};
    bool                 success{false};
    QString              error;
    QString              summary;
    qint64               elapsed_ms{0};
    std::vector<int64_t> result_ids;
};

ImageSearchController::ImageSearchController(ImageSearchDataProvider *data_provider, QObject *parent)
    : QObject(parent)
    , data_provider_(data_provider)
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
        QStringLiteral("dinov2_vitl14"),
        QStringLiteral("dinov2_vitg14"),
        QStringLiteral("dinov2_vits14_reg"),
        QStringLiteral("dinov2_vitb14_reg"),
        QStringLiteral("dinov2_vitl14_reg"),
        QStringLiteral("dinov2_vitg14_reg"),
        QStringLiteral("dinov2_vits14_reg4"),
        QStringLiteral("dinov2_vitb14_reg4"),
        QStringLiteral("dinov2_vitl14_reg4"),
        QStringLiteral("dinov2_vitg14_reg4"),
        QStringLiteral("dinov3_vits16"),
        QStringLiteral("dinov3_vits16plus"),
        QStringLiteral("dinov3_vitb16"),
        QStringLiteral("dinov3_vitl16"),
        QStringLiteral("dinov3_vitl16plus"),
        QStringLiteral("dinov3_vith16plus"),
        QStringLiteral("dinov3_vit7b16"),
        QStringLiteral("vit_base_patch16_224"),
        QStringLiteral("onnx"),
    };
}

QStringList ImageSearchController::modelFeatureNames(const QString &model_name) const
{
    const QString model = model_name.trimmed().isEmpty() ? defaultModelName() : model_name.trimmed();

    QStringList                    names;
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

QStringList ImageSearchController::roiModelPresets() const
{
    QStringList presets;
    for (const QString &model : supportedModelPresets())
    {
        if (!roiFeatureNames(model).isEmpty() && !presets.contains(model))
        {
            presets.append(model);
        }
    }
    return presets;
}

QStringList ImageSearchController::roiFeatureNames(const QString &model_name) const
{
    return knownRoiFeatureNames(model_name, defaultModelName());
}

QString ImageSearchController::defaultRoiFeatureName(const QString &model_name) const
{
    const QStringList names = roiFeatureNames(model_name);
    if (!names.isEmpty())
    {
        return names.last();
    }
    return defaultFeatureName();
}

QString ImageSearchController::suggestedWeightsPath(const QString &model_name) const
{
    const QString model = model_name.trimmed().isEmpty() ? defaultModelName() : model_name.trimmed();
    return QDir(QString("F:/models")).filePath(model + QString(".wts"));
}

// ────────────────────────────────────────────────────────────
//  公开接口
// ────────────────────────────────────────────────────────────

bool ImageSearchController::searchSelectedImages(const QVariantList &dataset_ids, const QString &model_name,
                                                 const QString &weights_file, const QString &feature_name,
                                                 bool rebuild_index, int top_k, const QString &norm,
                                                 const QString &preprocess_backend, const QString &faiss_backend,
                                                 const QString &index_storage, int disk_build_batch_size,
                                                 int model_batch_size, const QString &model_backend,
                                                 const QString &model_device)
{
    // 1. 前置校验
    if (running_)
    {
        setLastError(QString("图像搜索正在运行"));
        return false;
    }
    if (!data_provider_)
    {
        setLastError(QString("图像模型未初始化"));
        return false;
    }

    const auto query_ids = data_provider_->selectedImageIds();
    if (query_ids.empty())
    {
        setLastError(QString("请先选择要检索的图片"));
        return false;
    }

    // 2. 解析参数 & 构建请求
    const auto dataset_ids_set = parseDatasetIds(dataset_ids);
    if (!validateWeightsFile(weights_file))
        return false;

    SearchRequest request = buildSearchRequest(model_name, weights_file, feature_name, rebuild_index, top_k, norm,
                                               preprocess_backend, faiss_backend, index_storage, disk_build_batch_size,
                                               model_batch_size, model_backend, model_device);

    // 3. 收集图像
    collectGalleryImages(request, dataset_ids_set);
    if (request.gallery_images.empty())
    {
        setLastError(QString("选定数据集中没有可搜索的图像"));
        return false;
    }

    collectQueryImages(request, query_ids);
    if (request.query_images.empty())
    {
        setLastError(QString("选中的查询图片文件不存在"));
        return false;
    }

    // 4. 计算索引路径
    request.index_file = computeIndexPath(request);
    request.started_at = std::chrono::steady_clock::now();

    // 5. 清理状态并启动后台搜索
    resetForNewSearch();
    startProgress(request);

    QPointer<ImageSearchController> controller(this);
    QThread                        *work_thread = QThread::create([controller, request = std::move(request)]() mutable
                                           { executeSearchWorker(std::move(request), controller); });

    connect(work_thread, &QThread::finished, work_thread, &QObject::deleteLater);
    work_thread->start();
    return true;
}

bool ImageSearchController::searchImages(const QVariantList &image_ids, const QVariantList &dataset_ids,
                                         const QString &model_name, const QString &weights_file,
                                         const QString &feature_name, bool rebuild_index, int top_k,
                                         const QString &norm, const QString &preprocess_backend,
                                         const QString &faiss_backend, const QString &index_storage,
                                         int disk_build_batch_size, int model_batch_size, const QString &model_backend,
                                         const QString &model_device)
{
    if (running_)
    {
        setLastError(QString("图像搜索正在运行"));
        return false;
    }
    if (!data_provider_)
    {
        setLastError(QString("图像模型未初始化"));
        return false;
    }

    const auto query_ids = parseInt64List(image_ids);
    if (query_ids.empty())
    {
        setLastError(QString("请先选择要搜索的图像"));
        return false;
    }

    const auto dataset_ids_set = parseDatasetIds(dataset_ids);
    if (!validateWeightsFile(weights_file))
        return false;

    SearchRequest request = buildSearchRequest(model_name, weights_file, feature_name, rebuild_index, top_k, norm,
                                               preprocess_backend, faiss_backend, index_storage, disk_build_batch_size,
                                               model_batch_size, model_backend, model_device);
    request.mode          = SearchRequest::Mode::Image;

    collectGalleryImages(request, dataset_ids_set);
    if (request.gallery_images.empty())
    {
        setLastError(QString("选定数据集中没有可搜索的图像"));
        return false;
    }

    collectQueryImages(request, query_ids);
    if (request.query_images.empty())
    {
        setLastError(QString("选中的查询图像文件不存在"));
        return false;
    }

    request.index_file = computeIndexPath(request);
    request.started_at = std::chrono::steady_clock::now();

    resetForNewSearch();
    startProgress(request);

    QPointer<ImageSearchController> controller(this);
    QThread                        *work_thread = QThread::create([controller, request = std::move(request)]() mutable
                                           { executeSearchWorker(std::move(request), controller); });

    connect(work_thread, &QThread::finished, work_thread, &QObject::deleteLater);
    work_thread->start();
    return true;
}

bool ImageSearchController::searchLabelRois(const QVariantList &label_ids, const QVariantList &dataset_ids,
                                            const QString &model_name, const QString &weights_file,
                                            const QString &feature_name, bool rebuild_index, int top_k,
                                            const QString &norm, const QString &preprocess_backend,
                                            const QString &faiss_backend, const QString &index_storage,
                                            int disk_build_batch_size, int model_batch_size,
                                            const QString &model_backend, const QString &model_device,
                                            int pooled_height, int pooled_width, int sampling_ratio, bool aligned,
                                            bool use_pca, int pca_dim)
{
    if (running_)
    {
        setLastError(QString("标注搜索正在运行"));
        return false;
    }
    if (!data_provider_)
    {
        setLastError(QString("图像模型未初始化"));
        return false;
    }

    const auto query_label_ids = parseInt64List(label_ids);
    if (query_label_ids.empty())
    {
        setLastError(QString("请先选择要搜索的标注"));
        return false;
    }

    const auto dataset_ids_set = parseDatasetIds(dataset_ids);
    if (!validateWeightsFile(weights_file))
        return false;

    SearchRequest request  = buildSearchRequest(model_name, weights_file, feature_name, rebuild_index, top_k, norm,
                                                preprocess_backend, faiss_backend, index_storage, disk_build_batch_size,
                                                model_batch_size, model_backend, model_device);
    request.mode           = SearchRequest::Mode::Roi;
    request.pooled_height  = std::clamp(pooled_height, 1, 64);
    request.pooled_width   = std::clamp(pooled_width, 1, 64);
    request.sampling_ratio = std::clamp(sampling_ratio, -1, 32);
    request.aligned        = aligned;
    request.use_pca        = use_pca;
    request.pca_dim        = use_pca ? std::clamp(pca_dim, 1, 8192) : 0;

    const QStringList spatial_features = roiFeatureNames(request.model_name);
    if (spatial_features.isEmpty())
    {
        if (isKnownUnsupportedRoiModel(request.model_name, defaultModelName()))
        {
            setLastError(QString("标注搜索需要空间特征图，当前模型 %1 不支持标注搜索")
                             .arg(request.model_name));
        }
        else
        {
            setLastError(QString("标注搜索未找到当前模型 %1 的空间特征层")
                             .arg(request.model_name));
        }
        return false;
    }
    if (!spatial_features.isEmpty() && !spatial_features.contains(request.feature_name))
    {
        request.feature_name = spatial_features.last();
        dltool::settings::GlobalSettings::getInstance()->setValue(QStringLiteral("advanced.roiSearch"),
                                                                   QStringLiteral("featureName"),
                                                                   request.feature_name);
    }

    collectGalleryRois(request, dataset_ids_set);
    if (request.gallery_rois.empty())
    {
        setLastError(QString("选定数据集中没有可搜索的标注 ROI"));
        return false;
    }

    collectQueryRois(request, query_label_ids);
    if (request.query_rois.empty())
    {
        setLastError(QString("选中的标注没有有效 ROI 或图像文件不存在"));
        return false;
    }

    request.index_file = computeIndexPath(request);
    request.started_at = std::chrono::steady_clock::now();

    resetForNewSearch();
    startProgress(request);

    QPointer<ImageSearchController> controller(this);
    QThread                        *work_thread = QThread::create([controller, request = std::move(request)]() mutable
                                           { executeSearchWorker(std::move(request), controller); });

    connect(work_thread, &QThread::finished, work_thread, &QObject::deleteLater);
    work_thread->start();
    return true;
}

// ────────────────────────────────────────────────────────────
//  参数解析与校验
// ────────────────────────────────────────────────────────────

std::vector<int64_t> ImageSearchController::parseInt64List(const QVariantList &ids)
{
    std::vector<int64_t> result;
    result.reserve(static_cast<size_t>(ids.size()));
    for (const QVariant &value : ids)
    {
        bool    ok = false;
        int64_t id = value.toLongLong(&ok);
        if (ok)
        {
            result.push_back(id);
        }
    }
    return result;
}

std::set<int64_t> ImageSearchController::parseDatasetIds(const QVariantList &dataset_ids)
{
    const auto ids = parseInt64List(dataset_ids);
    return std::set<int64_t>(ids.begin(), ids.end());
}

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

ImageSearchController::SearchRequest ImageSearchController::buildSearchRequest(
    const QString &model_name, const QString &weights_file, const QString &feature_name, bool rebuild_index, int top_k,
    const QString &norm, const QString &preprocess_backend, const QString &faiss_backend, const QString &index_storage,
    int disk_build_batch_size, int model_batch_size, const QString &model_backend, const QString &model_device)
{
    const auto effective = [](const QString &value, const QString &fallback) -> QString
    {
        return value.trimmed().isEmpty() ? fallback : value.trimmed();
    };

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
    req.model_batch_size      = static_cast<int>(std::max(1, model_batch_size));
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

void ImageSearchController::collectGalleryImages(SearchRequest &request, const std::set<int64_t> &dataset_ids)
{
    const auto all_ids = data_provider_->allImageIds();

    for (const int64_t id : all_ids)
    {
        if (!dataset_ids.empty() && dataset_ids.find(data_provider_->imageDatasetId(id)) == dataset_ids.end())
            continue;

        const QString path = data_provider_->imagePath(id);
        if (!QFileInfo::exists(path))
            continue;

        request.gallery_images.push_back(toFsPath(QFileInfo(path).absoluteFilePath()));
        request.path_to_image_id.insert(normalizedPathKey(path), id);
    }
}

void ImageSearchController::collectQueryImages(SearchRequest &request, const std::vector<int64_t> &query_ids) const
{
    for (const int64_t id : query_ids)
    {
        const QString path = data_provider_->imagePath(id);
        if (QFileInfo::exists(path))
            request.query_images.push_back(toFsPath(QFileInfo(path).absoluteFilePath()));
    }
}

void ImageSearchController::collectGalleryRois(SearchRequest &request, const std::set<int64_t> &dataset_ids)
{
    const auto all_label_ids = data_provider_->allLabelIds();

    for (const int64_t label_id : all_label_ids)
    {
        const int64_t image_id = data_provider_->labelImageId(label_id);
        if (image_id < 0)
        {
            continue;
        }
        if (!dataset_ids.empty() && dataset_ids.find(data_provider_->imageDatasetId(image_id)) == dataset_ids.end())
        {
            continue;
        }

        const QString path = data_provider_->imagePath(image_id);
        if (!QFileInfo::exists(path))
        {
            continue;
        }

        irt::features::RoiSearchBox roi;
        if (!roiFromLabelData(data_provider_->labelData(label_id), roi))
        {
            continue;
        }

        request.gallery_rois.push_back({toFsPath(QFileInfo(path).absoluteFilePath()), roi});
        request.gallery_roi_label_ids.push_back(label_id);
        request.gallery_roi_image_ids.push_back(image_id);
    }
}

void ImageSearchController::collectQueryRois(SearchRequest &request, const std::vector<int64_t> &query_label_ids) const
{
    for (const int64_t label_id : query_label_ids)
    {
        const int64_t image_id = data_provider_->labelImageId(label_id);
        if (image_id < 0)
        {
            continue;
        }

        const QString path = data_provider_->imagePath(image_id);
        if (!QFileInfo::exists(path))
        {
            continue;
        }

        irt::features::RoiSearchBox roi;
        if (!roiFromLabelData(data_provider_->labelData(label_id), roi))
        {
            continue;
        }

        request.query_rois.push_back({toFsPath(QFileInfo(path).absoluteFilePath()), roi});
    }
}

// ────────────────────────────────────────────────────────────
//  索引路径
// ────────────────────────────────────────────────────────────

QString ImageSearchController::computeIndexPath(const SearchRequest &request) const
{
    if (request.mode == SearchRequest::Mode::Roi)
    {
        std::vector<int64_t> gallery_label_ids = request.gallery_roi_label_ids;
        std::sort(gallery_label_ids.begin(), gallery_label_ids.end());

        std::set<int64_t> dataset_ids;
        for (const int64_t image_id : request.gallery_roi_image_ids)
        {
            dataset_ids.insert(data_provider_->imageDatasetId(image_id));
        }

        const std::vector<int64_t> sorted_dataset_ids(dataset_ids.begin(), dataset_ids.end());
        const QString              index_dir = indexDirectoryForProject(
            data_provider_->databasePath(),
            dltool::settings::GlobalSettings::getInstance()
                ->value(QStringLiteral("advanced.roiSearch"), QStringLiteral("indexDirectory"))
                .toString(),
            QStringLiteral("roi_search"));

        return roiIndexPathForRequest(index_dir, sorted_dataset_ids, gallery_label_ids, request.model_name,
                                      request.feature_name, request.norm, request.faiss_backend, request.index_storage,
                                      request.model_backend_str, request.model_device_str, request.pooled_height,
                                      request.pooled_width, request.sampling_ratio, request.aligned, request.use_pca,
                                      request.pca_dim);
    }

    std::vector<int64_t> gallery_ids;
    gallery_ids.reserve(request.path_to_image_id.size());
    for (auto it = request.path_to_image_id.cbegin(); it != request.path_to_image_id.cend(); ++it)
        gallery_ids.push_back(it.value());
    std::sort(gallery_ids.begin(), gallery_ids.end());

    // 从 gallery_images 中反推 dataset IDs（保持原逻辑兼容）
    std::set<int64_t> dataset_ids;
    for (const int64_t id : gallery_ids) dataset_ids.insert(data_provider_->imageDatasetId(id));

    const std::vector<int64_t> sorted_dataset_ids(dataset_ids.begin(), dataset_ids.end());
    const QString              index_dir = indexDirectoryForProject(
        data_provider_->databasePath(),
        dltool::settings::GlobalSettings::getInstance()
            ->value(QStringLiteral("advanced.imageSearch"), QStringLiteral("indexDirectory"))
            .toString(),
        QStringLiteral("image_search"));

    return indexPathForRequest(index_dir, sorted_dataset_ids, gallery_ids, request.model_name, request.feature_name,
                               request.norm, request.faiss_backend, request.index_storage, request.model_backend_str,
                               request.model_device_str);
}

// ────────────────────────────────────────────────────────────
//  后台搜索执行
// ────────────────────────────────────────────────────────────

void ImageSearchController::executeSearchWorker(SearchRequest request, QPointer<ImageSearchController> controller)
{
    const size_t gallery_count
        = request.mode == SearchRequest::Mode::Roi ? request.gallery_rois.size() : request.gallery_images.size();

    auto reportProgress = [&controller, gallery_count](const irt::features::ImageSearchBuildProgress &progress)
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
        {
            addProgressMessage(spdlog::level::info, message);
        }
    };

    SearchResponse response;
    response.mode = request.mode;
    try
    {
        if (request.mode == SearchRequest::Mode::Roi)
        {
            irt::features::RoiSearchConfig config;
            config.model_name            = request.model_name.toStdString();
            config.feature_name          = request.feature_name.toStdString();
            config.preprocess_backend    = request.preprocess_backend;
            config.norm                  = request.norm_mode;
            config.faiss_backend         = request.faiss_backend_mode;
            config.index_storage         = request.index_storage_mode;
            config.disk_build_batch_size = static_cast<size_t>(request.disk_build_batch_size);
            config.model_batch_size      = static_cast<size_t>(request.model_batch_size);
            config.model_backend         = request.model_backend;
            config.model_device          = request.model_device;
            config.pooled_height         = request.pooled_height;
            config.pooled_width          = request.pooled_width;
            config.sampling_ratio        = request.sampling_ratio;
            config.aligned               = request.aligned;
            config.use_pca               = request.use_pca;
            config.pca_dim               = request.pca_dim;

            irt::features::RoiSearch search(config);
            const auto               weights_path = toFsPath(request.weights_file);
            const auto               index_path   = toFsPath(request.index_file);

            addProgressMessage(spdlog::level::info,
                               QString("正在准备标注搜索特征库: %1 个标注").arg(request.gallery_rois.size()));
            search.buildOrLoad(weights_path, request.gallery_rois, index_path, request.rebuild_index, reportProgress);

            std::map<int64_t, float> result_scores;
            for (const auto &query_item : request.query_rois)
            {
                for (const auto &result : search.search(query_item.image_path, query_item.roi, request.top_k))
                {
                    if (result.item_index >= request.gallery_roi_label_ids.size())
                    {
                        continue;
                    }

                    const int64_t label_id = request.gallery_roi_label_ids[result.item_index];
                    auto          it       = result_scores.find(label_id);
                    if (it == result_scores.end() || result.score > it->second)
                    {
                        result_scores[label_id] = result.score;
                    }
                }
            }

            std::vector<std::pair<int64_t, float>> sorted(result_scores.begin(), result_scores.end());
            std::sort(sorted.begin(), sorted.end(),
                      [](const auto &lhs, const auto &rhs) { return lhs.second > rhs.second; });

            response.result_ids.reserve(sorted.size());
            for (const auto &[id, _] : sorted) response.result_ids.push_back(id);

            response.success = true;
            response.summary = QString("标注搜索完成: 命中 %1 个标注").arg(response.result_ids.size());
        }
        else
        {
            irt::features::ImageSearchConfig config;
            config.model_name            = request.model_name.toStdString();
            config.feature_name          = request.feature_name.toStdString();
            config.preprocess_backend    = request.preprocess_backend;
            config.norm                  = request.norm_mode;
            config.faiss_backend         = request.faiss_backend_mode;
            config.index_storage         = request.index_storage_mode;
            config.disk_build_batch_size = static_cast<size_t>(request.disk_build_batch_size);
            config.model_batch_size      = static_cast<size_t>(request.model_batch_size);
            config.model_backend         = request.model_backend;
            config.model_device          = request.model_device;

            irt::features::ImageSearch search(config);
            const auto                 weights_path = toFsPath(request.weights_file);
            const auto                 index_path   = toFsPath(request.index_file);

            bool loaded = false;
            if (!request.rebuild_index && std::filesystem::exists(index_path))
            {
                try
                {
                    search.load(weights_path, index_path.parent_path(), index_path);
                    loaded = true;
                    addProgressMessage(spdlog::level::info,
                                       QString("已加载图像搜索特征库: %1").arg(request.index_file));
                }
                catch (const std::exception &e)
                {
                    addProgressMessage(spdlog::level::warn,
                                       QString("加载既有特征库失败，将重新构建: %1").arg(QString::fromUtf8(e.what())));
                }
            }

            if (!loaded)
            {
                addProgressMessage(spdlog::level::info,
                                   QString("正在构建图像搜索特征库: %1 张图像").arg(request.gallery_images.size()));
                search.build(weights_path, request.gallery_images, index_path, reportProgress);
            }

            // 对每张查询图执行检索
            std::map<int64_t, float> result_scores;
            for (const auto &query_image : request.query_images)
            {
                for (const auto &result : search.search(query_image, request.top_k))
                {
                    const auto found
                        = request.path_to_image_id.constFind(normalizedPathKey(fromFsPath(result.image_path)));
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
            for (const auto &[id, _] : sorted) response.result_ids.push_back(id);

            response.success = true;
            response.summary = QString("图像搜索完成: 命中 %1 张图像").arg(response.result_ids.size());
        }
    }
    catch (const std::exception &e)
    {
        response.success = false;
        response.error   = QString::fromUtf8(e.what());
        if (request.mode == SearchRequest::Mode::Roi
            && (response.error.contains(QStringLiteral("RoiSearch feature tensor must be NCHW"))
                || response.error.contains(QStringLiteral("RoiSearch requires NCHW feature tensor"))
                || response.error.contains(QStringLiteral("RoiSearch requires NCHW feature map"))))
        {
            response.error = QString("标注搜索需要空间特征图: CNN 模型请选择 layer4 等 NCHW 特征层，DINO 模型请选择 x_norm_patchtokens，并使用匹配的权重文件。");
        }
    }
    catch (...)
    {
        response.success = false;
        response.error   = request.mode == SearchRequest::Mode::Roi ? QString("未知标注搜索错误") : QString("未知图像搜索错误");
    }

    response.elapsed_ms = static_cast<qint64>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - request.started_at)
            .count());

    if (controller)
    {
        QMetaObject::invokeMethod(
            controller.data(),
            [controller, response]()
            {
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
    if (data_provider_)
    {
        data_provider_->clearImageSearchResults();
        data_provider_->clearLabelSearchResults();
    }
    emit resultsChanged();
}

void ImageSearchController::startProgress(const SearchRequest &request)
{
    setRunning(true);
    if (request.mode == SearchRequest::Mode::Roi)
    {
        ui::ProgressManager::getInstance()->startTask(QString("标注搜索"));
        addProgressMessage(spdlog::level::info, QString("开始标注搜索: 查询 %1 个标注, 图库 %2 个标注, TopK=%3")
                                                    .arg(request.query_rois.size())
                                                    .arg(request.gallery_rois.size())
                                                    .arg(request.top_k));
        return;
    }

    ui::ProgressManager::getInstance()->startTask(QString("图像搜索"));
    addProgressMessage(spdlog::level::info, QString("开始图像搜索: 查询 %1 张, 图库 %2 张, TopK=%3")
                                                .arg(request.query_images.size())
                                                .arg(request.gallery_images.size())
                                                .arg(request.top_k));
}

void ImageSearchController::finishProgress(bool success, const QString &message)
{
    const int level = success ? spdlog::level::info : spdlog::level::err;
    addProgressMessage(level, message);
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
        finishProgress(false, QString("%1, 耗时 %2").arg(response.error, formatElapsed(response.elapsed_ms)));
        return;
    }

    setLastError(QString());
    result_count_ = static_cast<int>(response.result_ids.size());
    last_summary_ = response.summary;

    if (data_provider_)
    {
        if (response.mode == SearchRequest::Mode::Roi)
        {
            data_provider_->setLabelSearchResults(response.result_ids, !response.result_ids.empty());
        }
        else
        {
            data_provider_->setImageSearchResults(response.result_ids, !response.result_ids.empty());
        }
    }

    finishProgress(true, QString("%1, 耗时 %2").arg(response.summary, formatElapsed(response.elapsed_ms)));
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
        const QString message = QString("搜索失败: %1").arg(last_error_);
        spdlog::error("{}", message.toUtf8().constData());
    }
    emit lastErrorChanged();
}

} // namespace dltool::feature
