#pragma once

#include "dltool/model/Export.h"
#include "model/ModelDatasetSelection.h"

#include <QVariantMap>
#include <QString>

namespace dltool::model {

/**
 * @brief 持久化的测试任务定义。
 *
 * 该对象只包含值类型，不持有 QML 模型或运行时任务状态，因此可以在仓库和
 * 后台准备流程之间安全传递。普通模型测试只包含一个 test 数据集选择。
 */
struct MODEL_API ModelTestTaskDefinition
{
    QString              uuid;
    QString              model_uuid;
    QString              name;
    QString              directory_name;
    QVariantMap          test_params;
    ModelDatasetSelection dataset_selection;
    qint64               created_at{0};
    qint64               modified_at{0};

    bool isValid() const
    {
        return !uuid.trimmed().isEmpty() && !model_uuid.trimmed().isEmpty() && !name.trimmed().isEmpty()
            && !directory_name.trimmed().isEmpty();
    }
};

} // namespace dltool::model

Q_DECLARE_METATYPE(dltool::model::ModelTestTaskDefinition)
