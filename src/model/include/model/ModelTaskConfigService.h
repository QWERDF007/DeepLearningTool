#pragma once

#include "dltool/model/Export.h"
#include "model/ModelStorageService.h"
#include "model/ModelTaskTypes.h"

#include <QString>
#include <QVariantMap>

namespace dltool::model {

class IModel;

enum class ModelTaskConfigFile
{
    Train,
    Test,
};

enum class ModelTaskConfigField
{
    ModelUuid,
    ModelName,
    TaskType,
    Framework,
    ModelArchitecture,
    ModelDir,
    ResultDir,
    LogDir,
    WeightDir,
    Datasets,
    TrainParams,
    TestParams,
    Trainer,
    Inference,
    OutputDir,
};

struct MODEL_API LoadedModelTaskConfigs
{
    QString     model_uuid;
    QVariantMap train_params;
    QVariantMap test_params;
};

MODEL_API QString                        modelTaskConfigFileName(ModelTaskConfigFile file);
MODEL_API QString                        modelTaskConfigFieldName(ModelTaskConfigField field);

class MODEL_API ModelTaskConfigService
{
public:
    explicit ModelTaskConfigService(QString project_dir = {});

    void setProjectDirectory(const QString &project_dir);

    QString                configPath(const QString &model_name, ModelTaskConfigFile file) const;
    LoadedModelTaskConfigs load(const QString &model_uuid, const QString &model_name) const;
    QVariantMap            build(IModel *model, const QString &model_name, ModelTaskType task_type,
                                 const QVariantMap &datasets) const;
    QString                write(const QString &model_name, ModelTaskType task_type, const QVariantMap &config,
                                 QString *err_msg = nullptr) const;

private:
    QVariantMap readParams(const QString &path, ModelTaskConfigField field) const;

    ModelStorageService storage_;
};

} // namespace dltool::model
