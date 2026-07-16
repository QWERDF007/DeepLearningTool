#pragma once

#include "dltool/model/Export.h"

#include <QObject>
#include <QString>
#include <QtQml>

namespace dltool::model {

/**
 * @brief 模型任务类型枚举包装类，供 QML 使用
 */
class MODEL_API ModelTaskTypes : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ModelTaskTypes)
    QML_UNCREATABLE("ModelTaskTypes only provides enum values.")

public:
    enum class Type
    {
        Unknown = 0, ///< 未知
        Train,       ///< 训练
        Test,        ///< 测试
        BoxToMask,   ///< BoxToMask
    };
    Q_ENUM(Type)
};

using ModelTaskType = ModelTaskTypes::Type;

/**
 * @brief 模型任务描述符，描述一个任务类型的元信息
 */
struct MODEL_API ModelTaskDescriptor
{
    ModelTaskType type{ModelTaskType::Unknown};   ///< 任务类型
    QString       key;                            ///< 任务键名
    QString       display_name;                   ///< 显示名称
    QString       config_file_name;               ///< 配置文件名
    QString       log_stem;                       ///< 日志文件名主干
    bool          requires_dataset_export{false}; ///< 是否需要导出数据集

    /**
     * @brief 检查是否为外部配置任务
     * @return 有配置文件名返回 true
     */
    bool isExternalConfigTask() const
    {
        return !config_file_name.isEmpty();
    }
};

/**
 * @brief 获取任务类型描述
 * @param task_type 任务类型
 * @return 任务描述符
 */
MODEL_API ModelTaskDescriptor describeModelTask(ModelTaskType task_type);

/**
 * @brief 检查是否为已知任务类型
 * @param task_type 任务类型
 * @return 已知返回 true
 */
MODEL_API bool isKnownModelTask(ModelTaskType task_type);

/**
 * @brief 检查是否为训练任务
 * @param task_type 任务类型
 * @return 是训练任务返回 true
 */
MODEL_API bool isTrainModelTask(ModelTaskType task_type);

/**
 * @brief 检查是否为测试任务
 * @param task_type 任务类型
 * @return 是测试任务返回 true
 */
MODEL_API bool isTestModelTask(ModelTaskType task_type);

/**
 * @brief 获取任务类型键名
 * @param task_type 任务类型
 * @return 键名
 */
MODEL_API QString modelTaskKey(ModelTaskType task_type);

/**
 * @brief 获取任务类型显示名称
 * @param task_type 任务类型
 * @return 显示名称
 */
MODEL_API QString modelTaskDisplayName(ModelTaskType task_type);

/**
 * @brief 获取任务日志文件名主干
 * @param task_type 任务类型
 * @return 日志文件名主干
 */
MODEL_API QString modelTaskLogStem(ModelTaskType task_type);

/**
 * @brief 获取任务配置文件名
 * @param task_type 任务类型
 * @return 配置文件名
 */
MODEL_API QString modelTaskConfigFileName(ModelTaskType task_type);

} // namespace dltool::model

Q_DECLARE_METATYPE(dltool::model::ModelTaskTypes::Type)
