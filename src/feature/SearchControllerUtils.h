#pragma once

#include "settings/GlobalSettings.h"
#include "settings/SettingsKeys.h"

#include <inferrt/features/ImageCluster.hpp>
#include <inferrt/features/ImageSearch.hpp>
#include <inferrt/features/RoiSearch.hpp>

#include <QString>
#include <QVariantMap>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <vector>

namespace dltool::feature {

/// 图像搜索基础设置
struct ImageSearchBaseSettings
{
    QString weights_file;       ///< 模型权重文件路径
    QString model_name;         ///< 模型名称
    QString feature_name;       ///< 特征层名称
    QString index_directory;    ///< 索引存储目录

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

/**
 * @brief 将 QString 转换为 std::filesystem::path
 * @param path 输入路径
 * @return 文件系统路径
 */
std::filesystem::path toFsPath(const QString &path);


/**
 * @brief 从全局设置中读取搜索基础配置
 * @param settings 全局设置实例
 * @param accessor 设置访问键
 * @return 搜索基础配置
 */
ImageSearchBaseSettings readImageSearchBaseSettings(
    const dltool::settings::GlobalSettings *settings,
    dltool::settings::generated::AccessorKey accessor);

/**
 * @brief 从全局设置中读取图像聚类配置
 * @param settings 全局设置实例
 * @return 图像聚类配置
 */
ImageClusterSettings readImageClusterSettings(const dltool::settings::GlobalSettings *settings);

/**
 * @brief 将基础设置应用到 ImageSearchConfig
 * @param config 目标配置
 * @param settings 基础设置
 */
void applyImageSearchBaseConfig(irt::features::ImageSearchConfig &config, const ImageSearchBaseSettings &settings);

/**
 * @brief 将基础设置应用到 RoiSearchConfig
 * @param config 目标配置
 * @param settings 基础设置
 */
void applyImageSearchBaseConfig(irt::features::RoiSearchConfig &config, const ImageSearchBaseSettings &settings);

/**
 * @brief 将图像聚类设置应用到 ImageClusterConfig
 */
void applyImageClusterConfig(irt::features::ImageClusterConfig &config,
                             const ImageClusterSettings &settings);

/**
 * @brief 检查搜索功能是否在设置中启用
 * @param settings 全局设置实例
 * @param accessor 设置访问键
 * @return 已启用返回 true
 */
bool searchSettingsEnabled(const dltool::settings::GlobalSettings *settings,
                           dltool::settings::generated::AccessorKey accessor);

/**
 * @brief 确定项目的索引存储目录
 * @param database_path 数据库路径
 * @param custom_directory 自定义目录
 * @param default_subdirectory 默认子目录名
 * @return 索引目录路径
 */
QString indexDirectoryForProject(const QString &database_path, const QString &custom_directory,
                                 const QString &default_subdirectory);

/**
 * @brief 生成索引文件完整路径
 * @param index_dir_path 索引目录路径
 * @param model_name 模型名称
 * @param feature_name 特征层名称
 * @param suffix 文件后缀
 * @return 索引文件完整路径
 */
QString indexPathForRequest(const QString &index_dir_path, const QString &model_name, const QString &feature_name,
                            const QString &suffix);

/**
 * @brief 格式化毫秒耗时为 HH:MM:SS.mmm 格式
 * @param elapsed_ms 耗时毫秒数
 * @return 格式化后的时间字符串
 */
QString formatElapsed(qint64 elapsed_ms);

/**
 * @brief 从构建进度中解析已处理和总数量
 * @param progress 构建进度
 * @param gallery_count 搜索库项数量
 * @param processed 已处理数量（输出）
 * @param total 总数量（输出）
 * @return 解析成功返回 true
 */
bool resolveProgressCount(const irt::features::ImageSearchBuildProgress &progress, size_t gallery_count,
                          size_t &processed, size_t &total);

/**
 * @brief 计算构建进度百分比
 * @param progress 构建进度
 * @param gallery_count 搜索库项数量
 * @return 进度百分比（0-100），无法计算时返回 -1
 */
int progressPercent(const irt::features::ImageSearchBuildProgress &progress, size_t gallery_count);

/**
 * @brief 格式化构建进度消息
 * @param progress 构建进度
 * @param gallery_count 搜索库项数量
 * @return 格式化的进度消息
 */
QString formatBuildProgressMessage(const irt::features::ImageSearchBuildProgress &progress, size_t gallery_count);

/**
 * @brief 向进度管理器添加消息
 * @param level 日志级别
 * @param message 消息内容
 */
void addProgressMessage(int level, const QString &message);

/**
 * @brief 从标注数据中解析 ROI 搜索框
 * @param data 标注数据
 * @param box ROI 搜索框（输出）
 * @return 解析成功返回 true
 */
bool roiFromLabelData(const QVariantMap &data, irt::features::RoiSearchBox &box);

/**
 * @brief 将搜索评分结果按分数降序排序并返回 ID 列表
 * @param result_scores ID 到分数的映射
 * @return 按分数降序排列的 ID 列表
 */
std::vector<int64_t> sortedSearchResultIds(const std::map<int64_t, float> &result_scores);

} // namespace dltool::feature
