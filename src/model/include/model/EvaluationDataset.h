#pragma once

#include "dltool/model/Export.h"
#include "model/EvaluationData.h"
#include "model/ModelEvaluationProtocol.h"

#include <QMap>
#include <QPair>
#include <QString>
#include <atomic>
#include <functional>
#include <limits>
#include <memory>

namespace dltool::model {

/** @brief 从 TIFF 预测产物读取原始异常分数图；缺失或格式无效时返回 false。 */
MODEL_API bool readEvaluationScoreMap(const QString &path, EvaluationScoreMap &score_map,
                                      QString *err_msg = nullptr);

/** @brief 获取分数图中的有限像素最大值。 */
MODEL_API bool evaluationScoreMapMaximum(const EvaluationScoreMap &score_map, double *maximum);

/** @brief 只读取异常分数图中的有限像素最大值，不物化完整像素数组。 */
MODEL_API bool readEvaluationScoreMapMaximum(const QString &path, double *maximum, QString *err_msg = nullptr);

/**
 * @brief 读取测试任务图像文件列表。
 *
 * 文件为 CSV 格式（image_id,image_path），支持 UTF-8 BOM 与可选表头行；
 * 对文档大小与记录数量设上限，避免异常文件耗尽 GUI 进程内存。
 * @param path 文件列表路径。
 * @param rows 输出行记录（image_id -> 图像路径）。
 * @param cancel_token 协作取消令牌，置位后提前失败；可为空。
 * @param err_msg 失败时输出错误信息，可为 nullptr。
 * @return 读取成功返回 true。
 */
MODEL_API bool readEvaluationImageList(const QString &path, QList<QPair<qint64, QString>> &rows,
                                       const std::shared_ptr<std::atomic_bool> &cancel_token = {},
                                       QString                                 *err_msg      = nullptr);

/**
 * @brief 从项目/任务数据库加载测试图像与真值。
 *
 * 以文件列表为主轴：不在项目数据库中的图像跳过并计数；图像级/标注级
 * 类别选择过滤不满足条件的图像并计数。异常检测方法按图像标签分组
 * （good/unlabeled/anomaly）构造二值真值，分类方法按图像标签类别构造。
 * 异常检测不在此阶段读取原图尺寸，区域生成时按需读取；其他需要几何校验
 * 的方法仍在此阶段准备尺寸。class_catalog 始终返回项目数据库中的完整
 * 类别目录，不受当前选择集影响。
 * @param file_list_path 测试任务文件列表路径。
 * @param project_database_path 项目数据库路径。
 * @param task_database_path 测试任务数据库路径（数据集/类别选择）。
 * @param method 评估方法。
 * @param images 输出图像记录。
 * @param cancel_token 协作取消令牌，置位后提前失败；可为空。
 * @param err_msg 失败时输出错误信息，可为 nullptr。
 * @param missing_database_images 输出：不在项目数据库中的图像数。
 * @param ignored_selection_images 输出：不满足数据集/类别选择的图像数。
 * @param dimensions_provider 可选的图像尺寸提供器。
 * @param class_catalog 输出：项目数据库中的全局类别目录，可为 nullptr。
 * @param class_colors 输出：项目数据库中的全局类别颜色映射，可为 nullptr。
 * @return 加载成功返回 true。
 */
MODEL_API bool loadEvaluationImages(
    const QString &file_list_path, const QString &project_database_path, const QString &task_database_path,
    evaluation::Method method, QMap<qint64, EvaluationImageData> &images,
    const std::shared_ptr<std::atomic_bool> &cancel_token = {}, QString *err_msg = nullptr,
    int *missing_database_images = nullptr, int *ignored_selection_images = nullptr,
    const std::function<bool(qint64 image_id, int *width, int *height)> &dimensions_provider = {},
    QMap<int, QString> *class_catalog = nullptr, QMap<int, QString> *class_colors_out = nullptr);

/**
 * @brief 从测试任务数据库与预测目录加载预测结果。
 *
 * 每条预测记录按协议校验 geometry，越界 bbox 裁剪到图像边界，并规范化
 * 几何记录；异常检测方法从 pred/<image_id>.tiff 读取原始异常分数图，
 * 不使用 task.db 中的 image_score 替代。评估主链路对异常检测只加载每张
 * TIFF 的最大值，并把这个标量随图像记录传递给阈值搜索、指标和图表；需要
 * 生成异常区域时才保留或按需读取对应完整分数图。
 * @param task_database_path 测试任务数据库路径。
 * @param prediction_dir 预测输出目录（mask artifact 根目录）。
 * @param images 已加载的图像记录（按 image_id 追加预测）。
 * @param anomaly_method 是否为异常检测方法。
 * @param count 输出预测总数，可为 nullptr。
 * @param cancel_token 协作取消令牌，置位后提前失败；可为空。
 * @param err_msg 失败时输出错误信息，可为 nullptr。
 * @param ignored_count 输出：不属于当前可用图像的预测数。
 * @param load_anomaly_score_maps 异常检测是否保留完整分数图；关闭时仅加载图像级最大分数，
 *                                多边形生成阶段按需读取分数图。指定阈值时，在同一次 TIFF
 *                                解码中保留最大值达到阈值的分数图，避免后续二次解码。
 * @return 加载成功返回 true。
 */
MODEL_API bool loadEvaluationPredictions(const QString &task_database_path, const QString &prediction_dir,
                                         QMap<qint64, EvaluationImageData> &images, bool anomaly_method,
                                         int                                     *count        = nullptr,
                                         const std::shared_ptr<std::atomic_bool> &cancel_token = {},
                                         QString *err_msg = nullptr, int *ignored_count = nullptr,
                                         bool load_anomaly_score_maps = true,
                                         double retain_anomaly_score_map_threshold
                                         = std::numeric_limits<double>::quiet_NaN());

} // namespace dltool::model
