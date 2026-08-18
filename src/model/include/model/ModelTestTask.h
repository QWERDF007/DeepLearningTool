#pragma once

#include "dltool/model/Export.h"
#include "model/ModelDatasetSelection.h"

#include <QString>
#include <QVariantMap>

namespace dltool::model {

/**
 * @brief 持久化的测试任务定义结构体。
 *
 * 纯值类型，不持有 QML 模型或运行时任务状态，可在仓库、业务逻辑和后台准备流程之间安全传递。
 */
struct MODEL_API ModelTestTaskDefinition
{
    QString               uuid;              ///< 测试任务全局唯一标识符（UUID）。
    QString               model_uuid;        ///< 所属模型的 UUID。
    QString               name;              ///< 测试任务名称。
    QString               directory_name;    ///< 任务工作子目录名。
    QVariantMap           test_params;       ///< 测试运行时超参数字典。
    ModelDatasetSelection dataset_selection; ///< 选中的测试数据集配置。
    qint64                created_at{0};     ///< 创建时间戳（毫秒）。
    qint64                modified_at{0};    ///< 最后修改时间戳（毫秒）。

    /**
     * @brief 验证任务定义是否合法有效。
     * @return 关键字段非空时返回 true。
     */
    bool isValid() const
    {
        return !uuid.trimmed().isEmpty() && !name.trimmed().isEmpty() && !directory_name.trimmed().isEmpty();
    }
};

} // namespace dltool::model

Q_DECLARE_METATYPE(dltool::model::ModelTestTaskDefinition)
