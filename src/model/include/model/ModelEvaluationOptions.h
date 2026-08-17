#pragma once

#include "dltool/model/Export.h"
#include "model/ModelEvaluationProtocol.h"

#include <QVariantMap>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>

namespace dltool::model {

/**
 * @brief C++ evaluation input.  The engine deliberately accepts paths and
 * plain values only, so it can run off the GUI thread and does not depend on
 * QML models or DataManager objects.
 */
struct MODEL_API ModelEvaluationOptions
{
    QString model_uuid;
    QString test_task_uuid;
    QString model_name;
    QString task_directory;
    evaluation::Method method{evaluation::Method::Unknown};
    QString project_database_path;
    QString dataset_file_list_path;
    QString task_database_path;
    QString prediction_dir;
    /**
     * @brief 保存完整规范化评估配置作为内存缓存键。
     *
     * 下方标量字段是评估器实际消费的值。
     */
    QVariantMap evaluation_config;
    double confidence_threshold{evaluation::kDefaultConfidenceThreshold};
    double iou_threshold{evaluation::kDefaultIouThreshold};
    evaluation::MatchingStrategy matching_strategy{evaluation::MatchingStrategy::GreedyIoU};
    /**
     * @brief 由任务控制器持有的可选协作取消令牌。
     *
     * 评估器不访问 QObject，可在工作线程安全轮询该令牌。
     */
    std::shared_ptr<std::atomic_bool> cancel_token;
    /**
     * @brief 可选图像尺寸提供者。
     *
     * 复用 DataManager 后台预取的尺寸缓存，避免评估线程为每张图重复打开
     * 文件；缓存没有尺寸时直接读取文件。
     */
    std::function<bool(qint64 image_id, int *width, int *height)> image_dimensions_provider;
};

} // namespace dltool::model
