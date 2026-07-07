#pragma once

#include "dltool/model/Export.h"

#include <QObject>
#include <QString>
#include <QtQml>

namespace dltool::model {

class MODEL_API ModelTaskTypes : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ModelTaskTypes)
    QML_UNCREATABLE("ModelTaskTypes only provides enum values.")

public:
    enum class Type
    {
        Unknown = 0,
        Train,
        Test,
        BoxToMask,
    };
    Q_ENUM(Type)
};

using ModelTaskType = ModelTaskTypes::Type;

struct MODEL_API ModelTaskDescriptor
{
    ModelTaskType type{ModelTaskType::Unknown};
    QString       key;
    QString       display_name;
    QString       config_file_name;
    QString       log_stem;
    bool          requires_dataset_export{false};

    bool isExternalConfigTask() const
    {
        return !config_file_name.isEmpty();
    }
};

MODEL_API ModelTaskDescriptor describeModelTask(ModelTaskType task_type);
MODEL_API bool                isKnownModelTask(ModelTaskType task_type);
MODEL_API bool                isTrainModelTask(ModelTaskType task_type);
MODEL_API bool                isTestModelTask(ModelTaskType task_type);
MODEL_API QString             modelTaskKey(ModelTaskType task_type);
MODEL_API QString             modelTaskDisplayName(ModelTaskType task_type);
MODEL_API QString             modelTaskLogStem(ModelTaskType task_type);
MODEL_API QString             modelTaskConfigFileName(ModelTaskType task_type);

} // namespace dltool::model

Q_DECLARE_METATYPE(dltool::model::ModelTaskTypes::Type)
