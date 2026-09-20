#pragma once

#include "dltool/feature/Export.h"
#include "feature/RoiClusterController.h"
#include "settings/GlobalSettings.h"
#include "settings/SettingsKeys.h"

#include <inferrt/features/DinoRegionSearch.hpp>
#include <inferrt/features/ImageCluster.hpp>
#include <inferrt/features/ImageSearch.hpp>
#include <inferrt/features/RoiCluster.hpp>
#include <inferrt/features/RoiSearch.hpp>

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <vector>

namespace dltool::feature {

/// 图像搜索基础设置
struct ImageSearchBaseSettings
{
    QString weights_file;       ///< 模型权重文件路径
    QString model_name;         ///< 模型名称
    QString feature_name;       ///< 特征层名称

    bool rebuild_index{false};  ///< 是否重建索引
    int  top_k{5};              ///< 返回结果数量
    int  norm{0};               ///< 特征归一化方式
    int  preprocess_backend{0}; ///< 预处理后端
    int  faiss_backend{0};      ///< Faiss 后端
    int  index_storage{0};      ///< 索引存储方式
    QString model_runtime;      ///< 模型推理运行时
    int  model_precision{0};    ///< 模型推理精度
    int  model_batch_size{1};   ///< 模型批处理大小
};

/// 图像聚类设置
struct ImageClusterSettings
{
    ImageSearchBaseSettings base;

    bool use_pca{false};
    int  pca_dim{0};
    bool include_noise{false};
    int  apply_mode{0};

    int64_t min_cluster_size{5};
    int64_t min_samples{0};
    double  cluster_selection_epsilon{0.0};
    int64_t max_cluster_size{0};
    double  alpha{1.0};
    int     algorithm{static_cast<int>(irt::ops::ClusteringAlgorithm::KDTree)};
    int64_t leaf_size{40};
    int     metric{static_cast<int>(irt::ops::kDefaultHDBSCANMetric)};
    int     cluster_selection_method{static_cast<int>(irt::ops::HDBSCANClusterSelectionMethod::Eom)};
    bool    allow_single_cluster{false};
};

/// 标注聚类范围
using RoiClusterScope = RoiClusterController::RoiClusterScope;

/// 标注聚类设置
struct RoiClusterSettings
{
    ImageSearchBaseSettings base;

    RoiClusterScope cluster_scope{RoiClusterScope::ByClass};
    QString         mode{"crop_masked_mean"};
    float           crop_margin{0.05f};
    int             patch_size{16};
    bool            include_noise{false};

    int64_t min_cluster_size{5};
    int64_t min_samples{0};
    double  cluster_selection_epsilon{0.0};
    int64_t max_cluster_size{0};
    int     algorithm{static_cast<int>(irt::ops::ClusteringAlgorithm::KDTree)};
    int     metric{static_cast<int>(irt::ops::kDefaultHDBSCANMetric)};
    int     cluster_selection_method{static_cast<int>(irt::ops::HDBSCANClusterSelectionMethod::Eom)};
};

/**
 * @brief 将 QString 转换为 std::filesystem::path
 * @param path 输入路径
 * @return 文件系统路径
 */
std::filesystem::path toFsPath(const QString &path);

/**
 * @brief 将 std::filesystem::path 转换为 QString
 * @param path 文件系统路径
 * @return QString
 */
QString fromFsPath(const std::filesystem::path &path);

/**
 * @brief 将 std::filesystem::path 转换为规范化 UTF-8 路径字符串（统一使用 '/' 分隔符）
 * @param path 文件系统路径
 * @return UTF-8 路径字符串
 */
std::string toDinoPathUtf8(const std::filesystem::path &path);

/**
 * @brief 将数据集/类别树选择转换为按数据集分组的范围。
 *
 * 空类别集合表示选择该数据集下的全部类别。
 */
std::map<int64_t, std::set<int64_t>> parseDatasetClassScope(const QVariantList &scope);

/**
 * @brief 从全局设置中读取搜索基础配置
 * @param settings 全局设置实例
 * @param accessor 设置访问键
 * @return 搜索基础配置
 */
FEATURE_API ImageSearchBaseSettings readImageSearchBaseSettings(
    const dltool::settings::GlobalSettings *settings,
    dltool::settings::generated::AccessorKey accessor);

/**
 * @brief 从全局设置中读取图像聚类配置
 * @param settings 全局设置实例
 * @return 图像聚类配置
 */
FEATURE_API ImageClusterSettings readImageClusterSettings(const dltool::settings::GlobalSettings *settings);

/**
 * @brief 从全局设置中读取标注聚类配置
 */
FEATURE_API RoiClusterSettings readRoiClusterSettings(const dltool::settings::GlobalSettings *settings);

/**
 * @brief 将基础设置应用到 ImageSearchConfig
 * @param config 目标配置
 * @param settings 基础设置
 */
FEATURE_API void applyImageSearchBaseConfig(irt::features::ImageSearchConfig &config, const ImageSearchBaseSettings &settings);

/**
 * @brief 将基础设置应用到 RoiSearchConfig
 * @param config 目标配置
 * @param settings 基础设置
 */
FEATURE_API void applyImageSearchBaseConfig(irt::features::RoiSearchConfig &config, const ImageSearchBaseSettings &settings);

/**
 * @brief 将图像聚类设置应用到 ImageClusterConfig
 */
FEATURE_API void applyImageClusterConfig(irt::features::ImageClusterConfig &config,
                                         const ImageClusterSettings &settings);

/**
 * @brief 将标注聚类设置应用到 RoiClusterConfig
 */
FEATURE_API void applyRoiClusterConfig(irt::features::RoiClusterConfig &config,
                                       const RoiClusterSettings &settings);

/**
 * @brief 检查搜索功能是否在设置中启用
 * @param settings 全局设置实例
 * @param accessor 设置访问键
 * @return 已启用返回 true
 */
FEATURE_API bool searchSettingsEnabled(const dltool::settings::GlobalSettings *settings,
                                       dltool::settings::generated::AccessorKey accessor);

/**
 * @brief 确定项目的索引存储目录
 * @param project_dir 项目目录
 * @param default_subdirectory 项目目录下的默认子目录名
 * @return 索引目录路径
 */
QString indexDirectoryForProject(const QString &project_dir, const QString &default_subdirectory);

/**
 * @brief 生成索引文件完整路径
 * @param index_dir_path 索引目录路径
 * @param model_name 模型名称
 * @param feature_name 特征层名称
 * @param suffix 文件后缀
 * @return 索引文件完整路径
 */
FEATURE_API QString indexPathForRequest(const QString &index_dir_path, const QString &model_name, const QString &feature_name,
                                        const QString &suffix);

FEATURE_API QString formatElapsed(qint64 elapsed_ms);

FEATURE_API bool resolveProgressCount(const irt::features::ImageSearchBuildProgress &progress, size_t gallery_count,
                                      size_t &processed, size_t &total);

FEATURE_API int progressPercent(const irt::features::ImageSearchBuildProgress &progress, size_t gallery_count);

FEATURE_API QString formatBuildProgressMessage(const irt::features::ImageSearchBuildProgress &progress, size_t gallery_count);

FEATURE_API bool resolveProgressCount(const irt::features::DinoBuildProgress &progress, size_t gallery_count,
                                      size_t &processed, size_t &total);

FEATURE_API int progressPercent(const irt::features::DinoBuildProgress &progress, size_t gallery_count);

FEATURE_API QString formatBuildProgressMessage(const irt::features::DinoBuildProgress &progress, size_t gallery_count);

FEATURE_API QString formatSearchProgressMessage(const irt::features::DinoSearchProgress &progress);

FEATURE_API void addProgressMessage(int level, const QString &message, const QString &task_id = QString());

FEATURE_API irt::features::RoiFeatureMode parseRoiFeatureMode(const QString &mode_str);

FEATURE_API bool roiFromLabelData(const QVariantMap &data, irt::features::RoiSearchBox &box);

FEATURE_API bool roiItemFromLabelData(int64_t roi_id, const std::filesystem::path &image_path,
                                      const QVariantMap &data, irt::features::RoiFeatureItem &item);

FEATURE_API std::vector<int64_t> sortedSearchResultIds(const std::map<int64_t, float> &result_scores);

} // namespace dltool::feature
