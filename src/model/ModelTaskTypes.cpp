#include "model/ModelTaskTypes.h"

namespace dltool::model {

ModelTaskDescriptor describeModelTask(ModelTaskType task_type)
{
    ModelTaskDescriptor descriptor;
    descriptor.type = task_type;

    switch (task_type)
    {
    case ModelTaskType::Train:
        descriptor.key                     = QStringLiteral("train");
        descriptor.display_name            = QStringLiteral("训练");
        descriptor.config_file_name        = QStringLiteral("train.yaml");
        descriptor.log_stem                = QStringLiteral("train");
        descriptor.requires_dataset_export = true;
        break;
    case ModelTaskType::Test:
        descriptor.key                     = QStringLiteral("test");
        descriptor.display_name            = QStringLiteral("测试");
        descriptor.config_file_name        = QStringLiteral("test.yaml");
        descriptor.log_stem                = QStringLiteral("test");
        descriptor.requires_dataset_export = true;
        break;
    case ModelTaskType::BoxToMask:
        descriptor.key                     = QStringLiteral("box_to_mask");
        descriptor.display_name            = QStringLiteral("BoxToMask");
        descriptor.config_file_name        = QStringLiteral("box_to_mask.yaml");
        descriptor.log_stem                = QStringLiteral("box_to_mask");
        descriptor.requires_dataset_export = true;
        break;
    case ModelTaskType::Unknown:
    default:
        descriptor.key          = QStringLiteral("unknown");
        descriptor.display_name = QStringLiteral("未知");
        descriptor.log_stem     = QStringLiteral("task");
        break;
    }

    return descriptor;
}

bool isKnownModelTask(ModelTaskType task_type)
{
    return task_type != ModelTaskType::Unknown;
}

bool isTrainModelTask(ModelTaskType task_type)
{
    return task_type == ModelTaskType::Train;
}

bool isTestModelTask(ModelTaskType task_type)
{
    return task_type == ModelTaskType::Test;
}

QString modelTaskKey(ModelTaskType task_type)
{
    return describeModelTask(task_type).key;
}

QString modelTaskDisplayName(ModelTaskType task_type)
{
    return describeModelTask(task_type).display_name;
}

QString modelTaskLogStem(ModelTaskType task_type)
{
    return describeModelTask(task_type).log_stem;
}

QString modelTaskConfigFileName(ModelTaskType task_type)
{
    return describeModelTask(task_type).config_file_name;
}

} // namespace dltool::model
