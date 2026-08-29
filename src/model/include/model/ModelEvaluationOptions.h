#pragma once

#include "dltool/model/Export.h"
#include "model/ModelEvaluationProtocol.h"

#include <QString>
#include <QVariantMap>
#include <atomic>
#include <functional>
#include <memory>

namespace dltool::model {

/**
 * @brief C++ 评估引擎输入选项结构体。
 *
 * 评估引擎仅接收文件路径与基础数值类型，可在后台工作线程中完全脱离 GUI 线程运行，
 * 不依赖 QML 视图模型或 DataManager 对象。
 */
struct MODEL_API ModelEvaluationOptions
{
    QString                      model_uuid;                          ///< 模型唯一 UUID。
    QString                      test_task_uuid;                      ///< 测试任务唯一 UUID。
    QString                      model_name;                          ///< 模型名称。
    QString                      task_directory;                      ///< 测试任务工作目录。
    evaluation::Method           method{evaluation::Method::Unknown}; ///< 视觉任务方法类型。
    QString                      project_database_path;               ///< 项目数据库路径。
    QString                      dataset_file_list_path; ///< 数据集图像列表文件路径（test.txt / file_list.txt）。
    QString                      task_database_path;     ///< 测试任务数据库路径（task.db）。
    QString                      prediction_dir;         ///< 预测产物输出目录。
    QString                      prediction_snapshot;    ///< 当前数据与预测产物的进程内快照。
    /**
     * @brief 模型推理阶段使用的空间预处理参数。
     *
     * 仅用于把原始异常分数图的模型坐标转换为原图坐标，以及生成热力图
     * 的模型坐标底图；评估指标计算不读取这些参数。
     */
    QVariantMap                  preprocessing_config;
    /**
     * @brief 规范化评估配置映射，用于内存缓存键比较。
     */
    QVariantMap                  evaluation_config;
    double                       confidence_threshold{evaluation::kDefaultConfidenceThreshold}; ///< 置信度过滤阈值。
    double                       iou_threshold{evaluation::kDefaultIouThreshold};               ///< IoU 判定阈值。
    evaluation::MatchingStrategy matching_strategy{evaluation::MatchingStrategy::GreedyIoU};    ///< 实例匹配策略。
    /**
     * @brief 是否在本次评估结果中采用搜索得到的最佳阈值。
     *
     * 该字段是任务生命周期的运行时控制项，不属于用户保存的 evaluation
     * 参数；首次评估由任务管理器设置，后续评估保持 false。
     */
    bool                         apply_best_threshold{false};
    /**
     * @brief 由任务控制器持有的可选协作取消令牌。
     *
     * 评估引擎不访问 QObject，可在后台线程安全轮询该令牌以响应用户取消操作。
     */
    std::shared_ptr<std::atomic_bool>                             cancel_token;
    /**
     * @brief 可选的图像尺寸提供回调函数。
     *
     * 复用 DataManager 后台预取的尺寸缓存，避免评估线程为每张图重复读取原图头信息。
     */
    std::function<bool(qint64 image_id, int *width, int *height)> image_dimensions_provider;
};

} // namespace dltool::model
