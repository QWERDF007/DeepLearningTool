#include "feature/ImageSearchController.h"

#include "feature/ImageSearchDataProvider.h"
#include "feature/Utils.h"
#include "settings/GlobalSettings.h"
#include "settings/SettingsValue.h"
#include "ui/ProgressManager.h"

#include <inferrt/features/ImageSearch.hpp>
#include <inferrt/features/RoiSearch.hpp>
#include <spdlog/spdlog.h>

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
#include <string_view>

namespace dltool::feature {

namespace {

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

QStringList variantListToStringList(const QVariantList &values)
{
    QStringList result;
    for (const QVariant &value : values)
    {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty() && !result.contains(text))
        {
            result.append(text);
        }
    }
    return result;
}

using dltool::settings::settingBool;
using dltool::settings::settingInt;
using dltool::settings::settingString;

QStringList configuredFeatureNames(dltool::settings::generated::AccessorKey accessor_key, std::string_view field_name,
                                   const QString &model_name)
{
    const QString model = model_name.trimmed();
    if (model.isEmpty())
    {
        return {};
    }

    auto *settings = dltool::settings::GlobalSettings::getInstance();
    if (settings == nullptr || settings->catalog() == nullptr)
    {
        return {};
    }

    const QVariantList options = settings->catalog()->optionsForAccessor(
        dltool::settings::toQString(dltool::settings::generated::accessorPath(accessor_key)),
        dltool::settings::toQString(field_name), model);
    return variantListToStringList(options);
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

// ponytail: InferRT 新 API 将元数据存在 .manifest.yaml，文件名只需区分模型+特征
QString indexPathForRequest(const QString &index_dir_path, const QString &model_name, const QString &feature_name)
{
    QDir index_dir(index_dir_path);
    if (!index_dir.exists())
        index_dir.mkpath(QStringLiteral("."));
    return index_dir.filePath(QString("%1_%2.faiss").arg(sanitizeFilePart(model_name), sanitizeFilePart(feature_name)));
}

QString roiIndexPathForRequest(const QString &index_dir_path, const QString &model_name, const QString &feature_name)
{
    QDir index_dir(index_dir_path);
    if (!index_dir.exists())
        index_dir.mkpath(QStringLiteral("."));
    return index_dir.filePath(
        QString("%1_%2.roi.faiss").arg(sanitizeFilePart(model_name), sanitizeFilePart(feature_name)));
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

    QString weights_file;
    QString index_file;

    bool rebuild_index{false};
    int  top_k{5};

    irt::features::ImageSearchConfig image_config;
    irt::features::RoiSearchConfig   roi_config;

    std::chrono::steady_clock::time_point started_at;

    std::vector<std::filesystem::path> query_images;
    std::vector<std::filesystem::path> gallery_images;
    QHash<QString, int64_t>            path_to_image_id;

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

// ────────────────────────────────────────────────────────────
//  公开接口
// ────────────────────────────────────────────────────────────

bool ImageSearchController::searchSelectedImages(const QVariantList &dataset_ids)
{
    if (!data_provider_)
    {
        setLastError(QString("图像模型未初始化"));
        return false;
    }

    const auto query_ids = data_provider_->selectedImageIds();
    return startImageSearch(query_ids, dataset_ids, QString("请先选择要检索的图片"),
                            QString("选中的查询图片文件不存在"));
}

bool ImageSearchController::searchImages(const QVariantList &image_ids, const QVariantList &dataset_ids)
{
    return startImageSearch(parseInt64Ids(image_ids), dataset_ids, QString("请先选择要搜索的图像"),
                            QString("选中的查询图像文件不存在"));
}

bool ImageSearchController::startImageSearch(const std::vector<int64_t> &query_ids, const QVariantList &dataset_ids,
                                             const QString &empty_query_message,
                                             const QString &missing_query_file_message)
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

    if (query_ids.empty())
    {
        setLastError(empty_query_message);
        return false;
    }

    const auto settings_accessor = dltool::settings::generated::AccessorKey::ImageSearch;
    if (!ensureSearchSettingsEnabled(settings_accessor, QString("图像搜索")))
    {
        return false;
    }

    const auto dataset_ids_set = parseDatasetIds(dataset_ids);

    SearchRequest request = buildSearchRequest(settings_accessor);
    request.mode          = SearchRequest::Mode::Image;

    const QString model_name = QString::fromStdString(request.image_config.model_name);
    if (model_name.trimmed().isEmpty())
    {
        setLastError(QString("请先配置图像搜索模型"));
        return false;
    }
    const QString feature_name = QString::fromStdString(request.image_config.feature_name);
    if (feature_name.trimmed().isEmpty())
    {
        setLastError(QString("请先配置图像搜索特征层"));
        return false;
    }
    if (!validateWeightsFile(request.weights_file))
        return false;

    collectGalleryImages(request, dataset_ids_set);
    if (request.gallery_images.empty())
    {
        setLastError(QString("选定数据集中没有可搜索的图像"));
        return false;
    }

    collectQueryImages(request, query_ids);
    if (request.query_images.empty())
    {
        setLastError(missing_query_file_message);
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

bool ImageSearchController::searchLabelRois(const QVariantList &label_ids, const QVariantList &dataset_ids)
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

    const auto query_label_ids = parseInt64Ids(label_ids);
    if (query_label_ids.empty())
    {
        setLastError(QString("请先选择要搜索的标注"));
        return false;
    }

    const auto settings_accessor = dltool::settings::generated::AccessorKey::RoiSearch;
    if (!ensureSearchSettingsEnabled(settings_accessor, QString("标注搜索")))
    {
        return false;
    }

    const auto dataset_ids_set = parseDatasetIds(dataset_ids);

    SearchRequest request = buildSearchRequest(settings_accessor);
    request.mode          = SearchRequest::Mode::Roi;

    const QString model_name = QString::fromStdString(request.roi_config.model_name);
    if (model_name.trimmed().isEmpty())
    {
        setLastError(QString("请先配置标注搜索模型"));
        return false;
    }
    const QString feature_name = QString::fromStdString(request.roi_config.feature_name);
    if (feature_name.trimmed().isEmpty())
    {
        setLastError(QString("请先配置标注搜索特征层"));
        return false;
    }
    if (!validateWeightsFile(request.weights_file))
        return false;

    namespace generated_field = dltool::settings::generated::field;
    const auto *settings      = dltool::settings::GlobalSettings::getInstance();
    request.roi_config.pooled_height
        = std::clamp(settingInt(settings, generated_field::RoiSearch::PooledHeight, 7), 1, 64);
    request.roi_config.pooled_width
        = std::clamp(settingInt(settings, generated_field::RoiSearch::PooledWidth, 7), 1, 64);
    request.roi_config.sampling_ratio
        = std::clamp(settingInt(settings, generated_field::RoiSearch::SamplingRatio, -1), -1, 32);
    request.roi_config.aligned = settingBool(settings, generated_field::RoiSearch::Aligned, false);
    request.roi_config.use_pca = settingBool(settings, generated_field::RoiSearch::UsePca, false);
    request.roi_config.pca_dim = request.roi_config.use_pca
                                   ? std::clamp(settingInt(settings, generated_field::RoiSearch::PcaDim, 0), 1, 8192)
                                   : 0;

    const QStringList spatial_features = configuredFeatureNames(
        dltool::settings::generated::AccessorKey::RoiSearch,
        dltool::settings::generated::fieldName(generated_field::RoiSearch::FeatureName), model_name);
    if (spatial_features.isEmpty())
    {
        setLastError(QString("标注搜索未在配置中找到模型 %1 的空间特征层").arg(model_name));
        return false;
    }
    if (!spatial_features.contains(feature_name))
    {
        const QString effective_feature_name = spatial_features.last();
        request.roi_config.feature_name      = effective_feature_name.toStdString();
        dltool::settings::GlobalSettings::getInstance()->setFieldValue(generated_field::RoiSearch::FeatureName,
                                                                       effective_feature_name);
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

std::set<int64_t> ImageSearchController::parseDatasetIds(const QVariantList &dataset_ids)
{
    const auto ids = parseInt64Ids(dataset_ids);
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

bool ImageSearchController::ensureSearchSettingsEnabled(const dltool::settings::generated::AccessorKey accessor_key,
                                                        const QString                                 &display_name)
{
    namespace generated_field = dltool::settings::generated::field;

    const auto *settings = dltool::settings::GlobalSettings::getInstance();
    if (settings == nullptr || settings->settingsGroup(accessor_key) == nullptr)
    {
        setLastError(QString("%1设置未加载").arg(display_name));
        return false;
    }

    bool enabled = true;
    switch (accessor_key)
    {
    case dltool::settings::generated::AccessorKey::ImageSearch:
        enabled = settingBool(settings, generated_field::ImageSearch::Enabled, true);
        break;
    case dltool::settings::generated::AccessorKey::RoiSearch:
        enabled = settingBool(settings, generated_field::RoiSearch::Enabled, true);
        break;
    default:
        enabled = true;
        break;
    }

    if (!enabled)
    {
        setLastError(QString("%1未启用").arg(display_name));
        return false;
    }
    return true;
}

ImageSearchController::SearchRequest ImageSearchController::buildSearchRequest(
    const dltool::settings::generated::AccessorKey accessor_key)
{
    const auto effective = [](const QString &value, const QString &fallback) -> QString
    {
        return value.trimmed().isEmpty() ? fallback : value.trimmed();
    };

    const auto *settings = dltool::settings::GlobalSettings::getInstance();

    auto readRequest
        = [&](const auto model_key, const auto model_path_key, const auto feature_name_key, const auto norm_key,
              const auto preprocess_backend_key, const auto faiss_backend_key, const auto index_storage_key,
              const auto model_backend_key, const auto model_device_key, const auto rebuild_index_key,
              const auto top_k_key, const auto model_batch_size_key) -> SearchRequest
    {
        const QString model_name   = settingString(settings, model_key);
        const QString weights_file = settingString(settings, model_path_key);
        const QString feature_name = settingString(settings, feature_name_key);

        const QString norm               = settingString(settings, norm_key, QStringLiteral("l2"));
        const QString preprocess_backend = settingString(settings, preprocess_backend_key, QStringLiteral("cpu"));
        const QString faiss_backend      = settingString(settings, faiss_backend_key, QStringLiteral("cpu"));
        const QString index_storage      = settingString(settings, index_storage_key, QStringLiteral("ram"));
        const QString model_backend      = settingString(settings, model_backend_key, QStringLiteral("tensorrt"));
        const QString model_device       = settingString(settings, model_device_key, QStringLiteral("gpu"));

        const QString effective_norm    = effective(norm, QStringLiteral("l2")).toLower();
        QString       effective_faiss   = effective(faiss_backend, QStringLiteral("cpu")).toLower();
        QString       effective_storage = effective(index_storage, QStringLiteral("ram")).toLower();
        if (effective_faiss == QStringLiteral("gpu"))
            effective_storage = QStringLiteral("ram");

        const QString cleaned_weights_file = weights_file.trimmed();

        SearchRequest req;
        req.weights_file
            = cleaned_weights_file.isEmpty() ? QString() : QFileInfo(cleaned_weights_file).absoluteFilePath();
        req.rebuild_index = settingBool(settings, rebuild_index_key, false);
        req.top_k         = std::max(1, settingInt(settings, top_k_key, 5));

        auto apply_common_config = [&](irt::features::ImageSearchConfig &config)
        {
            config.model_name         = model_name.toStdString();
            config.feature_name       = feature_name.toStdString();
            config.preprocess_backend = parsePreprocessBackend(preprocess_backend);
            config.norm               = parseNorm(effective_norm);
            config.faiss_backend      = parseFaissBackend(effective_faiss);
            config.index_storage      = parseIndexStorage(effective_storage);
            config.model_batch_size   = static_cast<size_t>(std::max(1, settingInt(settings, model_batch_size_key, 1)));
            config.model_backend      = parseModelBackend(model_backend);
            config.model_device       = parseModelDevice(model_device);
        };
        apply_common_config(req.image_config);
        apply_common_config(req.roi_config);
        return req;
    };

    namespace generated_field = dltool::settings::generated::field;
    if (accessor_key == dltool::settings::generated::AccessorKey::RoiSearch)
    {
        return readRequest(generated_field::RoiSearch::Model, generated_field::RoiSearch::ModelPath,
                           generated_field::RoiSearch::FeatureName, generated_field::RoiSearch::Norm,
                           generated_field::RoiSearch::PreprocessBackend, generated_field::RoiSearch::FaissBackend,
                           generated_field::RoiSearch::IndexStorage, generated_field::RoiSearch::ModelBackend,
                           generated_field::RoiSearch::ModelDevice, generated_field::RoiSearch::RebuildIndex,
                           generated_field::RoiSearch::TopK, generated_field::RoiSearch::ModelBatchSize);
    }
    return readRequest(generated_field::ImageSearch::Model, generated_field::ImageSearch::ModelPath,
                       generated_field::ImageSearch::FeatureName, generated_field::ImageSearch::Norm,
                       generated_field::ImageSearch::PreprocessBackend, generated_field::ImageSearch::FaissBackend,
                       generated_field::ImageSearch::IndexStorage, generated_field::ImageSearch::ModelBackend,
                       generated_field::ImageSearch::ModelDevice, generated_field::ImageSearch::RebuildIndex,
                       generated_field::ImageSearch::TopK, generated_field::ImageSearch::ModelBatchSize);
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
    namespace generated_field = dltool::settings::generated::field;
    const auto *settings      = dltool::settings::GlobalSettings::getInstance();

    if (request.mode == SearchRequest::Mode::Roi)
    {
        const QString model_name   = QString::fromStdString(request.roi_config.model_name);
        const QString feature_name = QString::fromStdString(request.roi_config.feature_name);
        const QString index_dir    = indexDirectoryForProject(
            data_provider_->databasePath(), settingString(settings, generated_field::RoiSearch::IndexDirectory),
            QStringLiteral("roi_search"));

        return roiIndexPathForRequest(index_dir, model_name, feature_name);
    }

    const QString model_name   = QString::fromStdString(request.image_config.model_name);
    const QString feature_name = QString::fromStdString(request.image_config.feature_name);
    const QString index_dir    = indexDirectoryForProject(
        data_provider_->databasePath(), settingString(settings, generated_field::ImageSearch::IndexDirectory),
        QStringLiteral("image_search"));

    return indexPathForRequest(index_dir, model_name, feature_name);
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
            irt::features::RoiSearch search(request.roi_config);
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
            irt::features::ImageSearch search(request.image_config);
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
            response.error
                = QString("标注搜索需要空间特征图，请在配置中选择当前模型对应的 NCHW 特征层并使用匹配的权重文件。");
        }
    }
    catch (...)
    {
        response.success = false;
        response.error
            = request.mode == SearchRequest::Mode::Roi ? QString("未知标注搜索错误") : QString("未知图像搜索错误");
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
