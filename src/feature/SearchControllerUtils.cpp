#include "SearchControllerUtils.h"

#include "settings/SettingsValue.h"
#include "ui/ProgressManager.h"

#include <QDateTime>
#include <QDir>
#include <QMetaObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <inferrt/model/ModelRuntime.hpp>
#include <algorithm>
#include <cmath>

namespace dltool::feature {
namespace {

using dltool::settings::settingBool;
using dltool::settings::settingInt;
using dltool::settings::settingString;

/**
 * @brief 清理文件名中的非法字符
 * @param value 输入字符串
 * @return 清理后的安全文件名
 */
QString sanitizeFilePart(QString value)
{
    value = value.trimmed();
    if (value.isEmpty())
        value = QStringLiteral("default");
    value.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
    return value;
}

/**
 * @brief 生成带时间戳的进度消息前缀
 * @return 时间戳字符串
 */
QString progressTimestamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hh:mm:ss"));
}

/**
 * @brief 为消息添加时间戳前缀
 * @param message 原始消息
 * @return 带时间戳的消息
 */
QString withProgressTimestamp(const QString &message)
{
    return QString("[%1] %2").arg(progressTimestamp(), message);
}

/**
 * @brief 判断构建阶段是否为单步阶段
 * @param stage 构建阶段
 * @return 是单步阶段返回 true
 */
bool isOneStepStage(irt::features::ImageSearchBuildStage stage)
{
    using Stage = irt::features::ImageSearchBuildStage;
    switch (stage)
    {
    case Stage::LoadingModel:
    case Stage::BuildingIndex:
    case Stage::LoadingIndex:
        return true;
    default:
        return false;
    }
}

/**
 * @brief 将基础设置应用到搜索配置（模板实现）
 * @param config 目标搜索配置
 * @param settings 基础设置
 */
template<typename Config>
void applyBaseConfig(Config &config, const ImageSearchBaseSettings &settings)
{
    auto parsed_faiss_backend = static_cast<irt::features::ImageSearchFaissBackend>(settings.faiss_backend);
    auto parsed_index_storage = static_cast<irt::features::ImageSearchIndexStorage>(settings.index_storage);
    if (parsed_faiss_backend == irt::features::ImageSearchFaissBackend::GPU)
        parsed_index_storage = irt::features::ImageSearchIndexStorage::RAM;

    config.model_name         = settings.model_name.toStdString();
    config.feature_name       = settings.feature_name.toStdString();
    config.preprocess_backend = static_cast<irt::features::ImageSearchPreprocessBackend>(settings.preprocess_backend);
    config.norm               = static_cast<irt::features::ImageSearchFeatureNorm>(settings.norm);
    config.faiss_backend      = parsed_faiss_backend;
    config.index_storage      = parsed_index_storage;
    config.model_batch_size   = static_cast<size_t>(settings.model_batch_size);
    config.model_runtime      = irt::model::ModelRuntime::parse(settings.model_runtime.toStdString());
    config.model_precision    = static_cast<irt::model::ModelPrecision>(settings.model_precision);
}

/**
 * @brief 安全地从全局设置读取字段值
 * @param settings 全局设置实例
 * @param field_key 字段键
 * @param fallback 默认值
 * @return 字段值
 */
template<typename Field>
QVariant valueForField(const dltool::settings::GlobalSettings *settings, Field field_key, const QVariant &fallback = {})
{
    return settings != nullptr ? settings->valueForField(field_key, fallback) : fallback;
}

/**
 * @brief 从全局设置中读取图像搜索配置
 * @param settings 全局设置实例
 * @return 图像搜索基础设置
 */
ImageSearchBaseSettings readImageSearchSettings(const dltool::settings::GlobalSettings *settings)
{
    namespace generated_field = dltool::settings::generated::field;

    ImageSearchBaseSettings result;
    result.weights_file       = settingString(settings, generated_field::ImageSearch::ModelPath);
    result.model_name         = settingString(settings, generated_field::ImageSearch::Model);
    result.feature_name       = settingString(settings, generated_field::ImageSearch::FeatureName);
    result.rebuild_index      = valueForField(settings, generated_field::ImageSearch::RebuildIndex).toBool();
    result.top_k              = valueForField(settings, generated_field::ImageSearch::TopK).toInt();
    result.norm               = valueForField(settings, generated_field::ImageSearch::Norm).toInt();
    result.preprocess_backend = valueForField(settings, generated_field::ImageSearch::PreprocessBackend).toInt();
    result.faiss_backend      = valueForField(settings, generated_field::ImageSearch::FaissBackend).toInt();
    result.index_storage      = valueForField(settings, generated_field::ImageSearch::IndexStorage).toInt();
    result.model_runtime      = settingString(settings, generated_field::ImageSearch::ModelRuntime,
                                               QStringLiteral("tensorrt:0"));
    result.model_precision    = valueForField(
                                   settings, generated_field::ImageSearch::ModelPrecision,
                                   static_cast<int>(irt::model::ModelPrecision::FP32))
                                   .toInt();
    result.model_batch_size   = valueForField(settings, generated_field::ImageSearch::ModelBatchSize).toInt();
    return result;
}

/**
 * @brief 从全局设置中读取 ROI 搜索配置
 * @param settings 全局设置实例
 * @return ROI 搜索基础设置
 */
ImageSearchBaseSettings readRoiSearchSettings(const dltool::settings::GlobalSettings *settings)
{
    namespace generated_field = dltool::settings::generated::field;

    ImageSearchBaseSettings result;
    result.weights_file       = settingString(settings, generated_field::RoiSearch::ModelPath);
    result.model_name         = settingString(settings, generated_field::RoiSearch::Model);
    result.feature_name       = settingString(settings, generated_field::RoiSearch::FeatureName);
    result.rebuild_index      = valueForField(settings, generated_field::RoiSearch::RebuildIndex).toBool();
    result.top_k              = valueForField(settings, generated_field::RoiSearch::TopK).toInt();
    result.norm               = valueForField(settings, generated_field::RoiSearch::Norm).toInt();
    result.preprocess_backend = valueForField(settings, generated_field::RoiSearch::PreprocessBackend).toInt();
    result.faiss_backend      = valueForField(settings, generated_field::RoiSearch::FaissBackend).toInt();
    result.index_storage      = valueForField(settings, generated_field::RoiSearch::IndexStorage).toInt();
    result.model_runtime      = settingString(settings, generated_field::RoiSearch::ModelRuntime,
                                               QStringLiteral("tensorrt:0"));
    result.model_precision    = valueForField(
                                   settings, generated_field::RoiSearch::ModelPrecision,
                                   static_cast<int>(irt::model::ModelPrecision::FP32))
                                   .toInt();
    result.model_batch_size   = valueForField(settings, generated_field::RoiSearch::ModelBatchSize).toInt();
    return result;
}

ImageClusterSettings readImageClusterSettingsImpl(const dltool::settings::GlobalSettings *settings)
{
    namespace generated_field = dltool::settings::generated::field;

    ImageClusterSettings result;
    result.base.weights_file = settingString(settings, generated_field::ImageCluster::ModelPath);
    result.base.model_name   = settingString(settings, generated_field::ImageCluster::Model);
    result.base.feature_name = settingString(settings, generated_field::ImageCluster::FeatureName);
    result.base.norm         = valueForField(settings, generated_field::ImageCluster::Norm, 2).toInt();
    result.base.preprocess_backend
        = valueForField(settings, generated_field::ImageCluster::PreprocessBackend, 0).toInt();
    result.base.model_runtime    = settingString(settings, generated_field::ImageCluster::ModelRuntime,
                                                  QStringLiteral("tensorrt:0"));
    result.base.model_precision  = valueForField(
                                      settings, generated_field::ImageCluster::ModelPrecision,
                                      static_cast<int>(irt::model::ModelPrecision::FP32))
                                      .toInt();
    result.base.model_batch_size = valueForField(settings, generated_field::ImageCluster::ModelBatchSize, 1).toInt();

    result.use_pca       = valueForField(settings, generated_field::ImageCluster::UsePca, false).toBool();
    result.pca_dim       = valueForField(settings, generated_field::ImageCluster::PcaDim, 0).toInt();
    result.include_noise = valueForField(settings, generated_field::ImageCluster::IncludeNoise, false).toBool();
    result.apply_mode    = valueForField(settings, generated_field::ImageCluster::ApplyMode, 0).toInt();

    result.min_cluster_size = valueForField(settings, generated_field::ImageCluster::MinClusterSize, 5).toLongLong();
    result.min_samples      = valueForField(settings, generated_field::ImageCluster::MinSamples, 0).toLongLong();
    result.cluster_selection_epsilon
        = valueForField(settings, generated_field::ImageCluster::ClusterSelectionEpsilon, 0.0).toDouble();
    result.max_cluster_size = valueForField(settings, generated_field::ImageCluster::MaxClusterSize, 0).toLongLong();
    result.algorithm        = valueForField(settings, generated_field::ImageCluster::Algorithm,
                                            static_cast<int>(irt::ops::ClusteringAlgorithm::KDTree))
                           .toInt();
    result.metric = valueForField(settings, generated_field::ImageCluster::Metric,
                                  static_cast<int>(irt::ops::kDefaultHDBSCANMetric))
                        .toInt();
    result.cluster_selection_method
        = valueForField(settings, generated_field::ImageCluster::ClusterSelectionMethod, 0).toInt();
    return result;
}

RoiClusterSettings readRoiClusterSettingsImpl(const dltool::settings::GlobalSettings *settings)
{
    namespace generated_field = dltool::settings::generated::field;

    RoiClusterSettings result;
    result.base.weights_file = settingString(settings, generated_field::RoiCluster::ModelPath);
    result.base.model_name   = settingString(settings, generated_field::RoiCluster::Model);
    result.base.feature_name = settingString(settings, generated_field::RoiCluster::FeatureName);
    result.base.norm         = valueForField(settings, generated_field::RoiCluster::Norm, 2).toInt();
    result.base.preprocess_backend
        = valueForField(settings, generated_field::RoiCluster::PreprocessBackend, 0).toInt();
    result.base.model_runtime = settingString(settings, generated_field::RoiCluster::ModelRuntime,
                                               QStringLiteral("tensorrt:0"));
    result.base.model_precision
        = valueForField(settings, generated_field::RoiCluster::ModelPrecision,
                        static_cast<int>(irt::model::ModelPrecision::FP32))
              .toInt();
    result.base.model_batch_size = valueForField(settings, generated_field::RoiCluster::ModelBatchSize, 1).toInt();

    result.use_pca       = valueForField(settings, generated_field::RoiCluster::UsePca, false).toBool();
    result.pca_dim       = valueForField(settings, generated_field::RoiCluster::PcaDim, 0).toInt();
    result.pooled_height = valueForField(settings, generated_field::RoiCluster::PooledHeight, 7).toInt();
    result.pooled_width  = valueForField(settings, generated_field::RoiCluster::PooledWidth, 7).toInt();
    result.sampling_ratio = valueForField(settings, generated_field::RoiCluster::SamplingRatio, -1).toInt();
    result.aligned        = valueForField(settings, generated_field::RoiCluster::Aligned, false).toBool();
    result.include_noise  = valueForField(settings, generated_field::RoiCluster::IncludeNoise, false).toBool();

    result.min_cluster_size
        = valueForField(settings, generated_field::RoiCluster::MinClusterSize, 5).toLongLong();
    result.min_samples = valueForField(settings, generated_field::RoiCluster::MinSamples, 0).toLongLong();
    result.cluster_selection_epsilon
        = valueForField(settings, generated_field::RoiCluster::ClusterSelectionEpsilon, 0.0).toDouble();
    result.max_cluster_size
        = valueForField(settings, generated_field::RoiCluster::MaxClusterSize, 0).toLongLong();
    result.algorithm = valueForField(settings, generated_field::RoiCluster::Algorithm,
                                     static_cast<int>(irt::ops::ClusteringAlgorithm::KDTree))
                           .toInt();
    result.metric = valueForField(settings, generated_field::RoiCluster::Metric,
                                  static_cast<int>(irt::ops::kDefaultHDBSCANMetric))
                        .toInt();
    result.cluster_selection_method
        = valueForField(settings, generated_field::RoiCluster::ClusterSelectionMethod, 0).toInt();
    return result;
}

} // namespace

/**
 * @brief 将 QString 转换为 std::filesystem::path
 * @param path 输入路径
 * @return 文件系统路径
 */
std::filesystem::path toFsPath(const QString &path)
{
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

std::map<int64_t, std::set<int64_t>> parseDatasetClassScope(const QVariantList &scope)
{
    std::map<int64_t, std::set<int64_t>> result;
    std::set<int64_t>                   all_classes_selected;

    for (const QVariant &value : scope)
    {
        const QVariantMap item = value.toMap();
        bool               dataset_ok = false;
        const int64_t      dataset_id = item.value(QStringLiteral("dataset_id")).toLongLong(&dataset_ok);
        if (!dataset_ok || dataset_id < 0)
            continue;

        bool          class_ok = false;
        const int64_t label_class_id
            = item.value(QStringLiteral("label_class_id"), -1).toLongLong(&class_ok);
        if (!class_ok || label_class_id < 0)
        {
            result[dataset_id].clear();
            all_classes_selected.insert(dataset_id);
            continue;
        }

        if (all_classes_selected.find(dataset_id) == all_classes_selected.end())
            result[dataset_id].insert(label_class_id);
    }
    return result;
}

/**
 * @brief 根据访问键读取对应的搜索基础设置
 * @param settings 全局设置实例
 * @param accessor 设置访问键
 * @return 搜索基础设置
 */
ImageSearchBaseSettings readImageSearchBaseSettings(const dltool::settings::GlobalSettings  *settings,
                                                    dltool::settings::generated::AccessorKey accessor)
{
    switch (accessor)
    {
    case dltool::settings::generated::AccessorKey::RoiSearch:
        return readRoiSearchSettings(settings);
    case dltool::settings::generated::AccessorKey::ImageCluster:
        return readImageClusterSettingsImpl(settings).base;
    case dltool::settings::generated::AccessorKey::RoiCluster:
        return readRoiClusterSettingsImpl(settings).base;
    case dltool::settings::generated::AccessorKey::ImageSearch:
    default:
        return readImageSearchSettings(settings);
    }
}

ImageClusterSettings readImageClusterSettings(const dltool::settings::GlobalSettings *settings)
{
    return readImageClusterSettingsImpl(settings);
}

RoiClusterSettings readRoiClusterSettings(const dltool::settings::GlobalSettings *settings)
{
    return readRoiClusterSettingsImpl(settings);
}

/**
 * @brief 将基础设置应用到 ImageSearchConfig
 * @param config 目标配置
 * @param settings 基础设置
 */
void applyImageSearchBaseConfig(irt::features::ImageSearchConfig &config, const ImageSearchBaseSettings &settings)
{
    applyBaseConfig(config, settings);
}

/**
 * @brief 将基础设置应用到 RoiSearchConfig
 * @param config 目标配置
 * @param settings 基础设置
 */
void applyImageSearchBaseConfig(irt::features::RoiSearchConfig &config, const ImageSearchBaseSettings &settings)
{
    applyBaseConfig(config, settings);
}

void applyImageClusterConfig(irt::features::ImageClusterConfig &config, const ImageClusterSettings &settings)
{
    applyBaseConfig(config, settings.base);
    config.use_pca                           = settings.use_pca;
    config.pca_dim                           = settings.pca_dim;
    config.hdbscan.min_cluster_size          = settings.min_cluster_size;
    config.hdbscan.min_samples               = settings.min_samples;
    config.hdbscan.cluster_selection_epsilon = settings.cluster_selection_epsilon;
    config.hdbscan.max_cluster_size          = settings.max_cluster_size;
    // config.hdbscan.alpha                              = settings.alpha;
    config.hdbscan.algorithm = static_cast<irt::ops::ClusteringAlgorithm>(settings.algorithm);
    // config.hdbscan.leaf_size                          = settings.leaf_size;
    config.hdbscan.metric = static_cast<irt::ops::ClusteringMetric>(settings.metric);
    config.hdbscan.cluster_selection_method
        = static_cast<irt::ops::HDBSCANClusterSelectionMethod>(settings.cluster_selection_method);
    // config.hdbscan.allow_single_cluster = settings.allow_single_cluster;
}

void applyRoiClusterConfig(irt::features::RoiClusterConfig &config, const RoiClusterSettings &settings)
{
    applyBaseConfig(config, settings.base);
    config.use_pca       = settings.use_pca;
    config.pca_dim       = settings.pca_dim;
    config.pooled_height = settings.pooled_height;
    config.pooled_width  = settings.pooled_width;
    config.sampling_ratio = settings.sampling_ratio;
    config.aligned        = settings.aligned;
    config.hdbscan.min_cluster_size          = settings.min_cluster_size;
    config.hdbscan.min_samples               = settings.min_samples;
    config.hdbscan.cluster_selection_epsilon = settings.cluster_selection_epsilon;
    config.hdbscan.max_cluster_size          = settings.max_cluster_size;
    config.hdbscan.algorithm = static_cast<irt::ops::ClusteringAlgorithm>(settings.algorithm);
    config.hdbscan.metric    = static_cast<irt::ops::ClusteringMetric>(settings.metric);
    config.hdbscan.cluster_selection_method
        = static_cast<irt::ops::HDBSCANClusterSelectionMethod>(settings.cluster_selection_method);
}

/**
 * @brief 检查搜索功能是否在设置中启用
 * @param settings 全局设置实例
 * @param accessor 设置访问键
 * @return 已启用返回 true
 */
bool searchSettingsEnabled(const dltool::settings::GlobalSettings  *settings,
                           dltool::settings::generated::AccessorKey accessor)
{
    namespace generated_field = dltool::settings::generated::field;
    switch (accessor)
    {
    case dltool::settings::generated::AccessorKey::RoiSearch:
        return settingBool(settings, generated_field::RoiSearch::Enabled, true);
    case dltool::settings::generated::AccessorKey::ImageCluster:
        return settingBool(settings, generated_field::ImageCluster::Enabled, true);
    case dltool::settings::generated::AccessorKey::RoiCluster:
        return settingBool(settings, generated_field::RoiCluster::Enabled, true);
    case dltool::settings::generated::AccessorKey::ImageSearch:
    default:
        return settingBool(settings, generated_field::ImageSearch::Enabled, true);
    }
}

/**
 * @brief 确定项目的索引存储目录
 * @param project_dir 项目目录
 * @param default_subdirectory 项目目录下的默认子目录名
 * @return 索引目录路径
 */
QString indexDirectoryForProject(const QString &project_dir, const QString &default_subdirectory)
{
    if (!project_dir.trimmed().isEmpty())
        return QDir(project_dir).filePath(default_subdirectory);

    QString fallback = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (fallback.isEmpty())
        fallback = QDir::tempPath();
    return QDir(fallback).filePath(default_subdirectory);
}

/**
 * @brief 生成索引文件完整路径
 * @param index_dir_path 索引目录路径
 * @param model_name 模型名称
 * @param feature_name 特征层名称
 * @param suffix 文件后缀
 * @return 索引文件完整路径
 */
QString indexPathForRequest(const QString &index_dir_path, const QString &model_name, const QString &feature_name,
                            const QString &suffix)
{
    QDir index_dir(index_dir_path);
    if (!index_dir.exists())
        index_dir.mkpath(QStringLiteral("."));
    return index_dir.filePath(
        QString("%1_%2%3").arg(sanitizeFilePart(model_name), sanitizeFilePart(feature_name), suffix));
}

/**
 * @brief 格式化毫秒耗时为 HH:MM:SS.mmm 格式
 * @param elapsed_ms 耗时毫秒数
 * @return 格式化后的时间字符串
 */
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

/**
 * @brief 从构建进度中解析已处理和总数量
 * @param progress 构建进度
 * @param gallery_count 搜索库项数量
 * @param processed 已处理数量（输出）
 * @param total 总数量（输出）
 * @return 解析成功返回 true
 */
bool resolveProgressCount(const irt::features::ImageSearchBuildProgress &progress, size_t gallery_count,
                          size_t &processed, size_t &total)
{
    using Stage = irt::features::ImageSearchBuildStage;

    if (progress.stage == Stage::Unknown)
        return false;

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

    return false;
}

/**
 * @brief 计算构建进度百分比
 * @param progress 构建进度
 * @param gallery_count 搜索库项数量
 * @return 进度百分比（0-100），无法计算时返回 -1
 */
int progressPercent(const irt::features::ImageSearchBuildProgress &progress, size_t gallery_count)
{
    size_t processed = 0;
    size_t total     = 0;
    if (!resolveProgressCount(progress, gallery_count, processed, total) || total == 0)
        return -1;
    return std::min(100, static_cast<int>(processed * 100 / total));
}

/**
 * @brief 格式化构建进度消息
 * @param progress 构建进度
 * @param gallery_count 搜索库项数量
 * @return 格式化的进度消息
 */
QString formatBuildProgressMessage(const irt::features::ImageSearchBuildProgress &progress, size_t gallery_count)
{
    using Stage = irt::features::ImageSearchBuildStage;

    if (progress.stage == Stage::Unknown)
        return {};

    const QString stage_name = QString::fromUtf8(irt::features::imageSearchBuildStageName(progress.stage));

    size_t processed = 0;
    size_t total     = 0;
    if (resolveProgressCount(progress, gallery_count, processed, total))
        return QString("构建进度 [%1]: %2 / %3").arg(stage_name).arg(processed).arg(total);

    if (isOneStepStage(progress.stage))
        return QString("构建阶段 [%1]").arg(stage_name);

    return {};
}

/**
 * @brief 向进度管理器添加消息
 * @param level 日志级别
 * @param message 消息内容
 */
void addProgressMessage(int level, const QString &message)
{
    QMetaObject::invokeMethod(ui::ProgressManager::getInstance(), "addMessage", Qt::AutoConnection, Q_ARG(int, level),
                              Q_ARG(QString, withProgressTimestamp(message)));
}

/**
 * @brief 从标注数据中解析 ROI 搜索框
 * @param data 标注数据
 * @param box ROI 搜索框（输出）
 * @return 解析成功返回 true
 */
bool roiFromLabelData(const QVariantMap &data, irt::features::RoiSearchBox &box)
{
    bool         ok_x = false, ok_y = false, ok_w = false, ok_h = false;
    const double x = data.value(QStringLiteral("x")).toDouble(&ok_x);
    const double y = data.value(QStringLiteral("y")).toDouble(&ok_y);
    const double w = data.value(QStringLiteral("width")).toDouble(&ok_w);
    const double h = data.value(QStringLiteral("height")).toDouble(&ok_h);
    if (!ok_x || !ok_y || !ok_w || !ok_h)
        return false;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h) || w <= 1.0 || h <= 1.0)
        return false;

    const double x2 = x + w;
    const double y2 = y + h;
    if (!std::isfinite(x2) || !std::isfinite(y2) || x2 <= x || y2 <= y)
        return false;

    box.x1 = static_cast<float>(x);
    box.y1 = static_cast<float>(y);
    box.x2 = static_cast<float>(x2);
    box.y2 = static_cast<float>(y2);
    return true;
}

/**
 * @brief 将搜索评分结果按分数降序排序并返回 ID 列表
 * @param result_scores ID 到分数的映射
 * @return 按分数降序排列的 ID 列表
 */
std::vector<int64_t> sortedSearchResultIds(const std::map<int64_t, float> &result_scores)
{
    std::vector<std::pair<int64_t, float>> sorted(result_scores.begin(), result_scores.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto &lhs, const auto &rhs) { return lhs.second > rhs.second; });

    std::vector<int64_t> result_ids;
    result_ids.reserve(sorted.size());
    for (const auto &[id, _] : sorted) result_ids.push_back(id);
    return result_ids;
}

} // namespace dltool::feature
