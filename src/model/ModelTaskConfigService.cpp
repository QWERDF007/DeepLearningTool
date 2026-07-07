#include "model/ModelTaskConfigService.h"

#include "common/YamlUtils.h"
#include "data/DataSelectionTreeModel.h"
#include "model/IModel.h"
#include "model/IModelConfig.h"
#include "model/IParams.h"
#include "model/ModelTaskTypes.h"

#include <yaml-cpp/yaml.h>

#include <spdlog/spdlog.h>

#include <QDir>
#include <QFileInfo>
#include <QModelIndex>
#include <QVariantList>
#include <array>
#include <map>
#include <utility>

namespace dltool::model {

namespace {

enum class ModelDatasetSelectionField
{
    DatasetIds,
    LabelClasses,
    DatasetId,
    LabelClassId,
};

const std::map<ModelTaskConfigFile, QString> &taskConfigFileNames()
{
    static const std::map<ModelTaskConfigFile, QString> names = {
        {ModelTaskConfigFile::Train, QStringLiteral("train.yaml")},
        { ModelTaskConfigFile::Test,  QStringLiteral("test.yaml")},
    };
    return names;
}

const std::map<ModelTaskConfigField, QString> &taskConfigFieldNames()
{
    static const std::map<ModelTaskConfigField, QString> names = {
        {      ModelTaskConfigField::ModelUuid, QStringLiteral("model_uuid")},
        {      ModelTaskConfigField::ModelName, QStringLiteral("model_name")},
        {       ModelTaskConfigField::TaskType, QStringLiteral("task_type")},
        {      ModelTaskConfigField::Framework, QStringLiteral("framework")},
        {ModelTaskConfigField::ModelArchitecture, QStringLiteral("model_architecture")},
        {       ModelTaskConfigField::ModelDir, QStringLiteral("model_dir")},
        {      ModelTaskConfigField::ResultDir, QStringLiteral("result_dir")},
        {         ModelTaskConfigField::LogDir, QStringLiteral("log_dir")},
        {      ModelTaskConfigField::WeightDir, QStringLiteral("weight_dir")},
        {      ModelTaskConfigField::Datasets, QStringLiteral("datasets")},
        {ModelTaskConfigField::DatasetSelections, QStringLiteral("dataset_selections")},
        {    ModelTaskConfigField::TrainParams, QStringLiteral("train_params")},
        {     ModelTaskConfigField::TestParams, QStringLiteral("test_params")},
        {         ModelTaskConfigField::Trainer, QStringLiteral("trainer")},
        {       ModelTaskConfigField::Inference, QStringLiteral("inference")},
        {       ModelTaskConfigField::OutputDir, QStringLiteral("output_dir")},
    };
    return names;
}

const std::map<ModelDatasetSplit, QString> &datasetSplitNames()
{
    static const std::map<ModelDatasetSplit, QString> names = {
        {     ModelDatasetSplit::Train,      QStringLiteral("train")},
        {ModelDatasetSplit::Validation, QStringLiteral("validation")},
        {      ModelDatasetSplit::Test,       QStringLiteral("test")},
    };
    return names;
}

const std::map<ModelDatasetSelectionField, QString> &datasetSelectionFieldNames()
{
    static const std::map<ModelDatasetSelectionField, QString> names = {
        {    ModelDatasetSelectionField::DatasetIds,     QStringLiteral("dataset_ids")},
        {  ModelDatasetSelectionField::LabelClasses,   QStringLiteral("label_classes")},
        {     ModelDatasetSelectionField::DatasetId,      QStringLiteral("dataset_id")},
        {ModelDatasetSelectionField::LabelClassId, QStringLiteral("label_class_id")},
    };
    return names;
}

QString datasetSelectionFieldName(ModelDatasetSelectionField field)
{
    const auto &names = datasetSelectionFieldNames();
    const auto  found = names.find(field);
    return found != names.end() ? found->second : QString();
}

QObject *datasetSelectionObject(IModel *model, ModelDatasetSplit split)
{
    if (model == nullptr)
        return nullptr;

    switch (split)
    {
    case ModelDatasetSplit::Train:
        return model->trainDatasetViewModel();
    case ModelDatasetSplit::Validation:
        return model->validationDatasetViewModel();
    case ModelDatasetSplit::Test:
        return model->testDatasetViewModel();
    }
    return nullptr;
}

QVariantMap datasetSelectionMap(QObject *selection_object)
{
    auto *selection_model = qobject_cast<dltool::data::DataSelectionTreeModel *>(selection_object);
    QVariantList dataset_ids;
    QVariantList label_classes;
    if (selection_model == nullptr)
    {
        return {
            {  datasetSelectionFieldName(ModelDatasetSelectionField::DatasetIds),   dataset_ids},
            {datasetSelectionFieldName(ModelDatasetSelectionField::LabelClasses), label_classes},
        };
    }

    for (int dataset_row = 0; dataset_row < selection_model->rowCount(); ++dataset_row)
    {
        const QModelIndex dataset_index = selection_model->index(dataset_row, 0);
        bool              dataset_ok = false;
        qint64            dataset_id
            = selection_model->data(dataset_index, dltool::data::DataSelectionTreeModel::DatasetIdRole).toLongLong(
                &dataset_ok);
        if (!dataset_ok || dataset_id < 0)
        {
            dataset_id
                = selection_model->data(dataset_index, dltool::data::DataSelectionTreeModel::ItemIdRole).toLongLong(
                    &dataset_ok);
        }
        if (!dataset_ok || dataset_id < 0)
            continue;

        if (selection_model->isNodeSelected(dataset_id, -1))
        {
            dataset_ids.append(dataset_id);
            continue;
        }

        for (int class_row = 0; class_row < selection_model->rowCount(dataset_index); ++class_row)
        {
            const QModelIndex class_index = selection_model->index(class_row, 0, dataset_index);
            bool              class_ok = false;
            const qint64      label_class_id
                = selection_model->data(class_index, dltool::data::DataSelectionTreeModel::LabelClassIdRole)
                      .toLongLong(&class_ok);
            if (!class_ok || label_class_id < 0 || !selection_model->isNodeSelected(dataset_id, label_class_id))
                continue;

            label_classes.append(QVariantMap{
                {     datasetSelectionFieldName(ModelDatasetSelectionField::DatasetId),      dataset_id},
                {datasetSelectionFieldName(ModelDatasetSelectionField::LabelClassId), label_class_id},
            });
        }
    }

    return {
        {  datasetSelectionFieldName(ModelDatasetSelectionField::DatasetIds),   dataset_ids},
        {datasetSelectionFieldName(ModelDatasetSelectionField::LabelClasses), label_classes},
    };
}

void applyDatasetSelectionMap(QObject *selection_object, const QVariantMap &selection)
{
    auto *selection_model = qobject_cast<dltool::data::DataSelectionTreeModel *>(selection_object);
    if (selection_model == nullptr)
        return;

    selection_model->clearSelection();
    const QVariantList dataset_ids
        = selection.value(datasetSelectionFieldName(ModelDatasetSelectionField::DatasetIds)).toList();
    for (const QVariant &dataset_value : dataset_ids)
    {
        bool         ok = false;
        const qint64 dataset_id = dataset_value.toLongLong(&ok);
        if (ok && dataset_id >= 0)
            selection_model->setNodeSelected(dataset_id, -1, true);
    }

    const QVariantList label_classes
        = selection.value(datasetSelectionFieldName(ModelDatasetSelectionField::LabelClasses)).toList();
    for (const QVariant &entry : label_classes)
    {
        const QVariantMap map = entry.toMap();
        bool              dataset_ok = false;
        bool              class_ok = false;
        const qint64      dataset_id
            = map.value(datasetSelectionFieldName(ModelDatasetSelectionField::DatasetId)).toLongLong(&dataset_ok);
        const qint64 label_class_id
            = map.value(datasetSelectionFieldName(ModelDatasetSelectionField::LabelClassId)).toLongLong(&class_ok);
        if (dataset_ok && class_ok && dataset_id >= 0 && label_class_id >= 0)
            selection_model->setNodeSelected(dataset_id, label_class_id, true);
    }
}

QString resolveModelOutputPath(const QString &model_dir, const QString &path, const QString &fallback)
{
    QString value = cleanModelPath(path);
    if (value.isEmpty())
        value = fallback;
    if (QFileInfo(value).isAbsolute())
        return value;
    return cleanModelPath(QDir(model_dir).filePath(value));
}

void normalizeOutputDir(QVariantMap &groups, const QString &group_name, const QString &model_dir,
                        const QString &fallback)
{
    QVariantMap group = groups.value(group_name).toMap();
    if (group.isEmpty())
        return;

    const QString output_dir = modelTaskConfigFieldName(ModelTaskConfigField::OutputDir);
    group.insert(output_dir, resolveModelOutputPath(model_dir, group.value(output_dir).toString(), fallback));
    groups.insert(group_name, group);
}

} // namespace

QString modelTaskConfigFileName(ModelTaskConfigFile file)
{
    const auto &names = taskConfigFileNames();
    const auto  found = names.find(file);
    return found != names.end() ? found->second : QString();
}

QString modelTaskConfigFieldName(ModelTaskConfigField field)
{
    const auto &names = taskConfigFieldNames();
    const auto  found = names.find(field);
    return found != names.end() ? found->second : QString();
}

QString modelDatasetSplitName(ModelDatasetSplit split)
{
    const auto &names = datasetSplitNames();
    const auto  found = names.find(split);
    return found != names.end() ? found->second : QString();
}

std::vector<ModelDatasetSplit> modelDatasetSplits()
{
    return {
        ModelDatasetSplit::Train,
        ModelDatasetSplit::Validation,
        ModelDatasetSplit::Test,
    };
}

QVariantMap modelDatasetSelections(IModel *model)
{
    QVariantMap selections;
    if (model == nullptr)
        return selections;

    for (const ModelDatasetSplit split : modelDatasetSplits())
    {
        QObject *selection_object = datasetSelectionObject(model, split);
        if (selection_object != nullptr)
            selections.insert(modelDatasetSplitName(split), datasetSelectionMap(selection_object));
    }
    return selections;
}

void applyModelDatasetSelections(IModel *model, const QVariantMap &dataset_selections)
{
    if (model == nullptr || dataset_selections.isEmpty())
        return;

    for (const ModelDatasetSplit split : modelDatasetSplits())
    {
        const QString split_name = modelDatasetSplitName(split);
        if (dataset_selections.contains(split_name))
            applyDatasetSelectionMap(datasetSelectionObject(model, split), dataset_selections.value(split_name).toMap());
    }
}

ModelTaskConfigService::ModelTaskConfigService(QString project_dir)
    : storage_(std::move(project_dir))
{
}

void ModelTaskConfigService::setProjectDirectory(const QString &project_dir)
{
    storage_.setProjectDirectory(project_dir);
}

QString ModelTaskConfigService::configPath(const QString &model_uuid, ModelTaskConfigFile file) const
{
    const QString config_dir = storage_.path(model_uuid, ModelStorageLocation::Configs);
    const QString file_name  = modelTaskConfigFileName(file);
    if (config_dir.isEmpty() || file_name.isEmpty())
        return {};
    return cleanModelPath(QDir(config_dir).filePath(file_name));
}

LoadedModelTaskConfigs ModelTaskConfigService::load(const QString &model_uuid) const
{
    const QString train_config_path = configPath(model_uuid, ModelTaskConfigFile::Train);
    const QString test_config_path  = configPath(model_uuid, ModelTaskConfigFile::Test);

    LoadedModelTaskConfigs configs;
    configs.model_uuid    = model_uuid;
    configs.train_params = readParams(train_config_path, ModelTaskConfigField::TrainParams);
    configs.test_params  = readParams(test_config_path, ModelTaskConfigField::TestParams);

    if (configs.train_params.isEmpty())
        configs.train_params = readParams(test_config_path, ModelTaskConfigField::TrainParams);
    if (configs.test_params.isEmpty())
        configs.test_params = readParams(train_config_path, ModelTaskConfigField::TestParams);

    const QVariantMap train_dataset_selections = readDatasetSelections(train_config_path);
    const QVariantMap test_dataset_selections  = readDatasetSelections(test_config_path);
    if (!train_dataset_selections.isEmpty() && !test_dataset_selections.isEmpty())
    {
        const QFileInfo train_info(cleanModelPath(train_config_path));
        const QFileInfo test_info(cleanModelPath(test_config_path));
        configs.dataset_selections
            = test_info.lastModified() > train_info.lastModified() ? test_dataset_selections : train_dataset_selections;
    }
    else
    {
        configs.dataset_selections
            = !test_dataset_selections.isEmpty() ? test_dataset_selections : train_dataset_selections;
    }
    return configs;
}

QVariantMap ModelTaskConfigService::build(IModel *model, const QString &model_name, ModelTaskType task_type,
                                          const QVariantMap &datasets) const
{
    QVariantMap config;
    if (model == nullptr)
        return config;

    const QString model_dir  = storage_.path(model->uuid(), ModelStorageLocation::ModelRoot);
    const QString result_dir = storage_.path(model->uuid(), ModelStorageLocation::Results);
    const QString log_dir    = storage_.path(model->uuid(), ModelStorageLocation::Logs);
    const QString weight_dir = storage_.path(model->uuid(), ModelStorageLocation::Weights);

    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::ModelUuid), model->uuid());
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::ModelName), model_name);
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::TaskType), modelTaskKey(task_type));
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::Framework), model->frameworkName());
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::ModelArchitecture), model->modelArchitecture());
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::ModelDir), model_dir);
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::ResultDir), result_dir);
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::LogDir), log_dir);
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::WeightDir), weight_dir);
    if (!datasets.isEmpty())
        config.insert(modelTaskConfigFieldName(ModelTaskConfigField::Datasets), datasets);

    const QVariantMap dataset_selections = modelDatasetSelections(model);
    if (!dataset_selections.isEmpty())
        config.insert(modelTaskConfigFieldName(ModelTaskConfigField::DatasetSelections), dataset_selections);

    if (IModelConfig *model_config = model->config(); model_config != nullptr)
    {
        if (ITrainParams *train_params = model_config->trainParams(); train_params != nullptr)
        {
            QVariantMap train_values = train_params->valuesMap();
            normalizeOutputDir(train_values, modelTaskConfigFieldName(ModelTaskConfigField::Trainer), model_dir,
                               modelStorageLocationName(ModelStorageLocation::Results));
            config.insert(modelTaskConfigFieldName(ModelTaskConfigField::TrainParams), train_values);
        }
        if (ITestParams *test_params = model_config->testParams(); test_params != nullptr)
        {
            QVariantMap test_values = test_params->valuesMap();
            normalizeOutputDir(test_values, modelTaskConfigFieldName(ModelTaskConfigField::Inference), model_dir,
                               modelStorageLocationName(ModelStorageLocation::Results));
            config.insert(modelTaskConfigFieldName(ModelTaskConfigField::TestParams), test_values);
        }
    }
    return config;
}

QString ModelTaskConfigService::write(const QString &model_uuid, ModelTaskType task_type, const QVariantMap &config,
                                      QString *err_msg) const
{
    const QString config_dir = storage_.path(model_uuid, ModelStorageLocation::Configs);
    const QString file_name  = dltool::model::modelTaskConfigFileName(task_type);
    if (config_dir.isEmpty())
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("任务配置目录为空");
        return {};
    }
    if (file_name.isEmpty())
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("未知任务配置类型: %1").arg(modelTaskKey(task_type));
        return {};
    }

    QDir dir(config_dir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("创建任务配置目录失败: %1").arg(config_dir);
        return {};
    }

    const QString path = dir.filePath(file_name);
    if (!dltool::common::yaml::writeFile(path, dltool::common::yaml::variantToYaml(config), err_msg,
                                         QStringLiteral("写入任务配置失败"),
                                         QStringLiteral("生成任务 YAML 配置失败")))
        return {};
    return path;
}

QVariantMap ModelTaskConfigService::readParams(const QString &path, ModelTaskConfigField field) const
{
    const QFileInfo file_info(cleanModelPath(path));
    if (!file_info.exists() || !file_info.isFile())
        return {};

    try
    {
        const YAML::Node root = dltool::common::yaml::loadFile(file_info);
        if (!root || !root.IsMap())
            return {};

        return dltool::common::yaml::nodeVariant(root[modelTaskConfigFieldName(field).toStdString()]).toMap();
    }
    catch (const std::exception &e)
    {
        spdlog::error("读取模型任务配置失败 '{}': {}", file_info.absoluteFilePath().toUtf8().constData(), e.what());
        return {};
    }
}

QVariantMap ModelTaskConfigService::readDatasetSelections(const QString &path) const
{
    const QFileInfo file_info(cleanModelPath(path));
    if (!file_info.exists() || !file_info.isFile())
        return {};

    try
    {
        const YAML::Node root = dltool::common::yaml::loadFile(file_info);
        if (!root || !root.IsMap())
            return {};

        return dltool::common::yaml::nodeVariant(
                   root[modelTaskConfigFieldName(ModelTaskConfigField::DatasetSelections).toStdString()])
            .toMap();
    }
    catch (const std::exception &e)
    {
        spdlog::error("读取模型数据集选择配置失败 '{}': {}", file_info.absoluteFilePath().toUtf8().constData(),
                      e.what());
        return {};
    }
}

} // namespace dltool::model
