#include "model/ModelManager.h"

#include "common/Utils.h"
#include "data/DataSelectionTreeModel.h"
#include "data/DatasetViewModelFactory.h"
#include "database/DataBase.h"
#include "model/IModelConfig.h"
#include "model/IParams.h"
#include "model/ModelDatasetOrganizer.h"
#include "model/TaskManager.h"
#include "common/YamlUtils.h"
#include "settings/GlobalSettings.h"
#include "settings/SettingsKeys.h"
#include "settings/SettingsValue.h"

#include <yaml-cpp/yaml.h>

#include <spdlog/spdlog.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <QQmlEngine>
#include <QPointer>
#include <algorithm>
#include <map>
#include <utility>

namespace dltool::model {

namespace {

struct RegisteredModel
{
    int                        method{-1};
    QString                    framework_name;
    QString                    model_architecture;
    ModelManager::ModelFactory factory;
};

struct RegisteredFramework
{
    ModelManager::FrameworkDefinition definition;
};

struct LoadedModelTaskConfigs
{
    QString     model_uuid;
    QVariantMap train_params;
    QVariantMap test_params;
    QVariantMap dataset_selections;
};

enum class ModelStoragePath
{
    ModelsRoot,
    ModelRoot,
    Results,
    Logs,
    Weights,
    Datasets,
    Configs,
};

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
    DatasetSelections,
    TrainParams,
    TestParams,
    Trainer,
    Inference,
    OutputDir,
};

enum class ModelDatasetSplit
{
    Train,
    Validation,
    Test,
};

enum class ModelDatasetSelectionField
{
    DatasetIds,
    LabelClasses,
    DatasetId,
    LabelClassId,
};

std::vector<RegisteredModel> &modelRegistry()
{
    static std::vector<RegisteredModel> registry;
    return registry;
}

std::vector<RegisteredFramework> &frameworkRegistry()
{
    static std::vector<RegisteredFramework> registry;
    return registry;
}

const std::map<ModelStoragePath, QString> &modelStoragePathNames()
{
    static const std::map<ModelStoragePath, QString> names = {
        {ModelStoragePath::ModelsRoot, QStringLiteral("models")},
        { ModelStoragePath::ModelRoot,                 {}},
        {   ModelStoragePath::Results, QStringLiteral("results")},
        {      ModelStoragePath::Logs, QStringLiteral("logs")},
        {   ModelStoragePath::Weights, QStringLiteral("weights")},
        {  ModelStoragePath::Datasets, QStringLiteral("datasets")},
        {   ModelStoragePath::Configs, QStringLiteral("configs")},
    };
    return names;
}

const std::map<ModelTaskConfigFile, QString> &modelTaskConfigFileNames()
{
    static const std::map<ModelTaskConfigFile, QString> names = {
        {ModelTaskConfigFile::Train, QStringLiteral("train.yaml")},
        { ModelTaskConfigFile::Test,  QStringLiteral("test.yaml")},
    };
    return names;
}

const std::map<ModelTaskConfigField, QString> &modelTaskConfigFieldNames()
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

const std::map<ModelDatasetSplit, QString> &modelDatasetSplitNames()
{
    static const std::map<ModelDatasetSplit, QString> names = {
        {     ModelDatasetSplit::Train,      QStringLiteral("train")},
        {ModelDatasetSplit::Validation, QStringLiteral("validation")},
        {      ModelDatasetSplit::Test,       QStringLiteral("test")},
    };
    return names;
}

const std::map<ModelDatasetSelectionField, QString> &modelDatasetSelectionFieldNames()
{
    static const std::map<ModelDatasetSelectionField, QString> names = {
        {    ModelDatasetSelectionField::DatasetIds,     QStringLiteral("dataset_ids")},
        {  ModelDatasetSelectionField::LabelClasses,   QStringLiteral("label_classes")},
        {     ModelDatasetSelectionField::DatasetId,      QStringLiteral("dataset_id")},
        {ModelDatasetSelectionField::LabelClassId, QStringLiteral("label_class_id")},
    };
    return names;
}

QString modelStoragePathName(ModelStoragePath path)
{
    const auto &names = modelStoragePathNames();
    const auto  found = names.find(path);
    return found != names.end() ? found->second : QString();
}

QString modelTaskConfigFileName(ModelTaskConfigFile file)
{
    const auto &names = modelTaskConfigFileNames();
    const auto  found = names.find(file);
    return found != names.end() ? found->second : QString();
}

QString modelTaskConfigFieldName(ModelTaskConfigField field)
{
    const auto &names = modelTaskConfigFieldNames();
    const auto  found = names.find(field);
    return found != names.end() ? found->second : QString();
}

QString modelDatasetSplitName(ModelDatasetSplit split)
{
    const auto &names = modelDatasetSplitNames();
    const auto  found = names.find(split);
    return found != names.end() ? found->second : QString();
}

QString modelDatasetSelectionFieldName(ModelDatasetSelectionField field)
{
    const auto &names = modelDatasetSelectionFieldNames();
    const auto  found = names.find(field);
    return found != names.end() ? found->second : QString();
}

const std::vector<ModelStoragePath> &modelStorageChildPaths()
{
    static const std::vector<ModelStoragePath> paths = {
        ModelStoragePath::Results,
        ModelStoragePath::Logs,
        ModelStoragePath::Weights,
        ModelStoragePath::Datasets,
        ModelStoragePath::Configs,
    };
    return paths;
}

const std::vector<ModelDatasetSplit> &modelDatasetSplits()
{
    static const std::vector<ModelDatasetSplit> splits = {
        ModelDatasetSplit::Train,
        ModelDatasetSplit::Validation,
        ModelDatasetSplit::Test,
    };
    return splits;
}

QString cleanPath(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return {};
    return QDir::cleanPath(QDir::fromNativeSeparators(trimmed));
}

QString runtimePath(const QString &path)
{
    const QString cleaned = cleanPath(path);
    if (cleaned.isEmpty() || QFileInfo(cleaned).isAbsolute())
        return cleaned;
    return cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath(cleaned));
}

QString resolvePath(const QString &base_dir, const QString &path)
{
    const QString cleaned = cleanPath(path);
    if (cleaned.isEmpty() || QFileInfo(cleaned).isAbsolute())
        return cleaned;
    return cleanPath(QDir(base_dir).filePath(cleaned));
}

QString pythonExecutableFromEnvPath(const QString &env_path)
{
    const QFileInfo info(cleanPath(env_path));
    if (info.isFile())
        return info.absoluteFilePath();

    const QDir dir(info.absoluteFilePath());
    for (const QString &candidate :
         {QStringLiteral("python.exe"), QStringLiteral("Scripts/python.exe"), QStringLiteral("bin/python"),
          QStringLiteral("python")})
    {
        const QString path = dir.filePath(candidate);
        if (QFileInfo::exists(path))
            return cleanPath(path);
    }
    return {};
}

bool isTrainTask(const QString &task_type)
{
    const QString value = task_type.trimmed().toLower();
    return value.contains(QStringLiteral("train")) || value.contains(QStringLiteral("训练"));
}

bool isPredictTask(const QString &task_type)
{
    const QString value = task_type.trimmed().toLower();
    return value.contains(QStringLiteral("test")) || value.contains(QStringLiteral("predict"))
        || value.contains(QStringLiteral("测试")) || value.contains(QStringLiteral("推理"));
}

QString taskLogStem(const QString &task_type, int task_id)
{
    Q_UNUSED(task_id)
    QString kind = QStringLiteral("task");
    if (isTrainTask(task_type))
        kind = QStringLiteral("train");
    else if (isPredictTask(task_type))
        kind = QStringLiteral("test");
    return kind;
}

QString taskConfigFileName(const QString &task_type)
{
    if (isTrainTask(task_type))
        return modelTaskConfigFileName(ModelTaskConfigFile::Train);
    if (isPredictTask(task_type))
        return modelTaskConfigFileName(ModelTaskConfigFile::Test);
    return {};
}

QString projectDirectory(dltool::database::ProjectDataBase *database)
{
    if (database == nullptr)
        return {};
    const QFileInfo project_file(database->path());
    return project_file.absoluteDir().absolutePath();
}

QString modelStoragePath(dltool::database::ProjectDataBase *database, const QString &uuid, ModelStoragePath path)
{
    const QString project_dir = projectDirectory(database);
    if (project_dir.isEmpty())
        return {};

    const QString root = cleanPath(QDir(project_dir).filePath(modelStoragePathName(ModelStoragePath::ModelsRoot)));
    if (path == ModelStoragePath::ModelsRoot)
        return root;

    const QString trimmed_uuid = uuid.trimmed();
    if (trimmed_uuid.isEmpty())
        return {};

    const QString model_dir = cleanPath(QDir(root).filePath(trimmed_uuid));
    if (path == ModelStoragePath::ModelRoot)
        return model_dir;

    const QString child_name = modelStoragePathName(path);
    if (child_name.isEmpty())
        return {};
    return cleanPath(QDir(model_dir).filePath(child_name));
}

QString modelTaskConfigPath(dltool::database::ProjectDataBase *database, const QString &uuid,
                            ModelTaskConfigFile file)
{
    const QString config_dir = modelStoragePath(database, uuid, ModelStoragePath::Configs);
    const QString file_name  = modelTaskConfigFileName(file);
    if (config_dir.isEmpty() || file_name.isEmpty())
        return {};
    return cleanPath(QDir(config_dir).filePath(file_name));
}

bool ensureDirectory(const QString &path, QString *err_msg)
{
    const QString cleaned = cleanPath(path);
    if (cleaned.isEmpty())
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("目录路径为空");
        return false;
    }

    QDir dir(cleaned);
    if (dir.exists())
        return true;
    if (!dir.mkpath(QStringLiteral(".")))
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("创建目录失败: %1").arg(cleaned);
        return false;
    }
    return true;
}

bool ensureModelStorage(dltool::database::ProjectDataBase *database, const QString &uuid, QString *err_msg = nullptr)
{
    const QString model_dir = modelStoragePath(database, uuid, ModelStoragePath::ModelRoot);
    if (!ensureDirectory(model_dir, err_msg))
        return false;

    for (const ModelStoragePath child_path : modelStorageChildPaths())
    {
        if (!ensureDirectory(modelStoragePath(database, uuid, child_path), err_msg))
            return false;
    }
    return true;
}

bool removeModelStorage(dltool::database::ProjectDataBase *database, const QString &uuid, QString *err_msg = nullptr)
{
    const QString root = cleanPath(QFileInfo(modelStoragePath(database, {}, ModelStoragePath::ModelsRoot)).absoluteFilePath());
    const QString target
        = cleanPath(QFileInfo(modelStoragePath(database, uuid, ModelStoragePath::ModelRoot)).absoluteFilePath());
    if (root.isEmpty() || target.isEmpty() || target == root
        || !target.startsWith(root + QStringLiteral("/"), Qt::CaseInsensitive))
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("拒绝删除非法模型目录: %1").arg(target);
        return false;
    }

    QDir dir(target);
    if (!dir.exists())
        return true;
    if (!dir.removeRecursively())
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("删除模型目录失败: %1").arg(target);
        return false;
    }
    return true;
}

QString resolveModelOutputPath(const QString &model_dir, const QString &path, const QString &fallback)
{
    QString value = cleanPath(path);
    if (value.isEmpty())
        value = fallback;
    if (QFileInfo(value).isAbsolute())
        return value;
    return cleanPath(QDir(model_dir).filePath(value));
}

void normalizeOutputDir(QVariantMap &groups, const QString &group_name, const QString &model_dir, const QString &fallback)
{
    QVariantMap group = groups.value(group_name).toMap();
    if (group.isEmpty())
        return;
    const QString output_dir = modelTaskConfigFieldName(ModelTaskConfigField::OutputDir);
    group.insert(output_dir, resolveModelOutputPath(model_dir, group.value(output_dir).toString(), fallback));
    groups.insert(group_name, group);
}

YAML::Node variantToYaml(const QVariant &value)
{
    if (!value.isValid() || value.isNull())
        return {};

    const int type_id = value.typeId();
    if (type_id == QMetaType::QVariantMap)
    {
        YAML::Node node(YAML::NodeType::Map);
        const QVariantMap map = value.toMap();
        for (auto it = map.constBegin(); it != map.constEnd(); ++it)
            node[it.key().toStdString()] = variantToYaml(it.value());
        return node;
    }

    if (type_id == QMetaType::QVariantHash)
    {
        YAML::Node node(YAML::NodeType::Map);
        const QVariantHash hash = value.toHash();
        for (auto it = hash.constBegin(); it != hash.constEnd(); ++it)
            node[it.key().toStdString()] = variantToYaml(it.value());
        return node;
    }

    if (type_id == QMetaType::QVariantList)
    {
        YAML::Node node(YAML::NodeType::Sequence);
        const QVariantList list = value.toList();
        for (const QVariant &entry : list)
            node.push_back(variantToYaml(entry));
        return node;
    }

    if (type_id == QMetaType::QStringList)
    {
        YAML::Node node(YAML::NodeType::Sequence);
        const QStringList list = value.toStringList();
        for (const QString &entry : list)
            node.push_back(entry.toStdString());
        return node;
    }

    switch (type_id)
    {
    case QMetaType::Bool:
        return YAML::Node(value.toBool());
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return YAML::Node(value.toLongLong());
    case QMetaType::Double:
    case QMetaType::Float:
        return YAML::Node(value.toDouble());
    default:
        return YAML::Node(value.toString().toStdString());
    }
}

QVariantMap taskParamsFromConfigFile(const QString &path, ModelTaskConfigField field)
{
    const QFileInfo file_info(cleanPath(path));
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

QVariantMap taskDatasetSelectionsFromConfigFile(const QString &path)
{
    const QFileInfo file_info(cleanPath(path));
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

LoadedModelTaskConfigs loadModelTaskConfigs(const QString &model_uuid, const QString &train_config_path,
                                            const QString &test_config_path)
{
    LoadedModelTaskConfigs configs;
    configs.model_uuid = model_uuid;
    configs.train_params = taskParamsFromConfigFile(train_config_path, ModelTaskConfigField::TrainParams);
    configs.test_params  = taskParamsFromConfigFile(test_config_path, ModelTaskConfigField::TestParams);

    if (configs.train_params.isEmpty())
        configs.train_params = taskParamsFromConfigFile(test_config_path, ModelTaskConfigField::TrainParams);
    if (configs.test_params.isEmpty())
        configs.test_params = taskParamsFromConfigFile(train_config_path, ModelTaskConfigField::TestParams);

    const QVariantMap train_dataset_selections = taskDatasetSelectionsFromConfigFile(train_config_path);
    const QVariantMap test_dataset_selections  = taskDatasetSelectionsFromConfigFile(test_config_path);
    if (!train_dataset_selections.isEmpty() && !test_dataset_selections.isEmpty())
    {
        const QFileInfo train_info(cleanPath(train_config_path));
        const QFileInfo test_info(cleanPath(test_config_path));
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
            {  modelDatasetSelectionFieldName(ModelDatasetSelectionField::DatasetIds),   dataset_ids},
            {modelDatasetSelectionFieldName(ModelDatasetSelectionField::LabelClasses), label_classes},
        };
    }

    for (int dataset_row = 0; dataset_row < selection_model->rowCount(); ++dataset_row)
    {
        const QModelIndex dataset_index = selection_model->index(dataset_row, 0);
        bool              dataset_ok    = false;
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
            bool              class_ok    = false;
            const qint64      label_class_id
                = selection_model->data(class_index, dltool::data::DataSelectionTreeModel::LabelClassIdRole)
                      .toLongLong(&class_ok);
            if (!class_ok || label_class_id < 0 || !selection_model->isNodeSelected(dataset_id, label_class_id))
                continue;

            label_classes.append(QVariantMap{
                {    modelDatasetSelectionFieldName(ModelDatasetSelectionField::DatasetId),      dataset_id},
                {modelDatasetSelectionFieldName(ModelDatasetSelectionField::LabelClassId), label_class_id},
            });
        }
    }

    return {
        {  modelDatasetSelectionFieldName(ModelDatasetSelectionField::DatasetIds),   dataset_ids},
        {modelDatasetSelectionFieldName(ModelDatasetSelectionField::LabelClasses), label_classes},
    };
}

QVariantMap modelDatasetSelections(IModel *model)
{
    QVariantMap selections;
    for (const ModelDatasetSplit split : modelDatasetSplits())
    {
        QObject *selection_object = datasetSelectionObject(model, split);
        if (selection_object != nullptr)
            selections.insert(modelDatasetSplitName(split), datasetSelectionMap(selection_object));
    }
    return selections;
}

void applyDatasetSelectionMap(QObject *selection_object, const QVariantMap &selection)
{
    auto *selection_model = qobject_cast<dltool::data::DataSelectionTreeModel *>(selection_object);
    if (selection_model == nullptr)
        return;

    selection_model->clearSelection();
    const QVariantList dataset_ids
        = selection.value(modelDatasetSelectionFieldName(ModelDatasetSelectionField::DatasetIds)).toList();
    for (const QVariant &value : dataset_ids)
    {
        bool         ok = false;
        const qint64 dataset_id = value.toLongLong(&ok);
        if (ok && dataset_id >= 0)
            selection_model->setNodeSelected(dataset_id, -1, true);
    }

    const QVariantList label_classes
        = selection.value(modelDatasetSelectionFieldName(ModelDatasetSelectionField::LabelClasses)).toList();
    for (const QVariant &entry : label_classes)
    {
        const QVariantMap label_class = entry.toMap();
        bool              dataset_ok = false;
        bool              class_ok   = false;
        const qint64      dataset_id
            = label_class.value(modelDatasetSelectionFieldName(ModelDatasetSelectionField::DatasetId)).toLongLong(
                &dataset_ok);
        const qint64 label_class_id
            = label_class.value(modelDatasetSelectionFieldName(ModelDatasetSelectionField::LabelClassId))
                  .toLongLong(&class_ok);
        if (dataset_ok && class_ok && dataset_id >= 0 && label_class_id >= 0)
            selection_model->setNodeSelected(dataset_id, label_class_id, true);
    }
}

QFile *openProcessLogFile(const QString &path, QObject *parent, QString *err_msg)
{
    const QString cleaned = cleanPath(path);
    if (cleaned.isEmpty())
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("日志路径为空");
        return nullptr;
    }

    if (!ensureDirectory(QFileInfo(cleaned).absolutePath(), err_msg))
        return nullptr;

    auto *file = new QFile(cleaned, parent);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("打开日志文件失败: %1, %2").arg(cleaned, file->errorString());
        delete file;
        return nullptr;
    }
    return file;
}

ModelManager::FrameworkDefinition resolvedFrameworkDefinition(
    const ModelManager::FrameworkDefinition &definition)
{
    ModelManager::FrameworkDefinition resolved = definition;
    resolved.root = runtimePath(definition.root);
    resolved.train_script = resolvePath(resolved.root, definition.train_script);
    resolved.predict_script = resolvePath(resolved.root, definition.predict_script);

    resolved.scripts.clear();
    for (auto it = definition.scripts.constBegin(); it != definition.scripts.constEnd(); ++it)
        resolved.scripts.insert(it.key(), resolvePath(resolved.root, it.value()));

    resolved.python_paths.clear();
    for (const QString &path : definition.python_paths)
        resolved.python_paths.append(resolvePath(resolved.root, path));
    return resolved;
}

QVariantMap modelTaskConfig(IModel *model, const QString &model_name, const QString &task_type,
                            dltool::database::ProjectDataBase *database, const QVariantMap &datasets)
{
    QVariantMap config;
    if (model == nullptr)
        return config;

    const QString model_dir = modelStoragePath(database, model->uuid(), ModelStoragePath::ModelRoot);
    const QString result_dir = modelStoragePath(database, model->uuid(), ModelStoragePath::Results);
    const QString log_dir = modelStoragePath(database, model->uuid(), ModelStoragePath::Logs);
    const QString weight_dir = modelStoragePath(database, model->uuid(), ModelStoragePath::Weights);

    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::ModelUuid), model->uuid());
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::ModelName), model_name);
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::TaskType), task_type);
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
                               modelStoragePathName(ModelStoragePath::Results));
            config.insert(modelTaskConfigFieldName(ModelTaskConfigField::TrainParams), train_values);
        }
        if (ITestParams *test_params = model_config->testParams(); test_params != nullptr)
        {
            QVariantMap test_values = test_params->valuesMap();
            normalizeOutputDir(test_values, modelTaskConfigFieldName(ModelTaskConfigField::Inference), model_dir,
                               modelStoragePathName(ModelStoragePath::Results));
            config.insert(modelTaskConfigFieldName(ModelTaskConfigField::TestParams), test_values);
        }
    }
    return config;
}

QString writeTaskConfigFile(const QString &config_dir, const QString &task_type, const QVariantMap &config,
                            QString *err_msg)
{
    const QString cleaned_dir = cleanPath(config_dir);
    if (cleaned_dir.isEmpty())
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("任务配置目录为空");
        return {};
    }

    const QString file_name = taskConfigFileName(task_type);
    if (file_name.isEmpty())
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("未知任务配置类型: %1").arg(task_type);
        return {};
    }

    QDir dir(cleaned_dir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("创建任务配置目录失败: %1").arg(cleaned_dir);
        return {};
    }

    const QString path = dir.filePath(file_name);
    QFile         file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("写入任务配置失败: %1").arg(file.errorString());
        return {};
    }

    YAML::Emitter out;
    out << variantToYaml(config);
    if (!out.good())
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("生成任务 YAML 配置失败: %1").arg(QString::fromUtf8(out.GetLastError().c_str()));
        return {};
    }

    file.write(out.c_str());
    file.write("\n");
    return path;
}

} // namespace

ModelManager::ModelManager(const int method,
                           dltool::database::ProjectDataBase *database,
                           dltool::data::DataManager *data_manager,
                           QObject *parent)
    : QAbstractListModel(parent)
    , database_(database)
    , data_manager_(data_manager)
    , method_(method)
{
    init();
}

ModelManager::~ModelManager() {}

void ModelManager::init()
{
    beginResetModel();
    models_.clear();
    model_instances_.clear();
    config_load_started_.clear();

    if (database_ == nullptr)
    {
        endResetModel();
        spdlog::error("初始化模型管理器失败: 数据库对象为空");
        return;
    }

    std::vector<int64_t> model_ids;
    std::vector<QString> uuids;
    std::vector<QString> names;
    std::vector<QString> framework_names;
    std::vector<QString> model_architectures;
    std::vector<QString> training_results;
    std::vector<QString> test_results;
    std::vector<qint64>  ctimes;
    std::vector<qint64>  mtimes;
    QString              err_msg;

    const bool ok = database_->getAllModels(model_ids, uuids, names, framework_names, model_architectures,
                                            training_results, test_results, ctimes, mtimes, err_msg);
    if (!ok)
    {
        endResetModel();
        spdlog::error("查询所有模型失败, 错误: {}", err_msg.toUtf8().constData());
        return;
    }

    models_.reserve(model_ids.size());
    for (size_t i = 0; i < model_ids.size(); ++i)
    {
        models_.push_back(ModelRecord{
            model_ids[i],
            uuids[i],
            names[i],
            framework_names[i],
            model_architectures[i],
            training_results[i],
            test_results[i],
            ctimes[i],
            mtimes[i],
        });
    }

    endResetModel();
}

int ModelManager::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(models_.size());
}

QVariant ModelManager::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= rowCount())
        return QVariant();

    switch (role)
    {
    case ModelIdRole:
        return getModelId(index);
    case UuidRole:
        return getUuid(index);
    case NameRole:
        return getName(index);
    case FrameworkNameRole:
        return getFrameworkName(index);
    case ModelArchitectureRole:
        return getModelArchitecture(index);
    case TrainingResultRole:
        return getTrainingResult(index);
    case TestResultRole:
        return getTestResult(index);
    case CtimeRole:
        return getCtime(index);
    case MtimeRole:
        return getMtime(index);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> ModelManager::roleNames() const
{
    return {
        {       ModelIdRole,            "model_id"},
        {          UuidRole,                "uuid"},
        {          NameRole,                "name"},
        { FrameworkNameRole,      "framework_name"},
        {ModelArchitectureRole, "model_architecture"},
        {TrainingResultRole,     "training_result"},
        {    TestResultRole,         "test_result"},
        {         CtimeRole,               "ctime"},
        {         MtimeRole,               "mtime"},
    };
}

bool ModelManager::addModel(const QString &name, const QString &framework_name, const QString &model_architecture)
{
    const QString trimmed_name               = name.trimmed();
    const QString trimmed_framework_name     = framework_name.trimmed();
    const QString trimmed_model_architecture = model_architecture.trimmed();
    if (trimmed_name.isEmpty() || trimmed_framework_name.isEmpty() || trimmed_model_architecture.isEmpty())
    {
        spdlog::warn("添加模型失败: 模型名称、框架或模型架构为空");
        return false;
    }

    if (!registeredModelArchitectures(method_, trimmed_framework_name).contains(trimmed_model_architecture))
    {
        spdlog::warn("添加模型失败: 模型未注册, 方法: {}, 框架: {}, 模型架构: {}", method_,
                     trimmed_framework_name.toUtf8().constData(), trimmed_model_architecture.toUtf8().constData());
        return false;
    }

    if (database_ == nullptr)
    {
        spdlog::error("添加模型失败: 数据库对象为空");
        return false;
    }

    QString       err_msg;
    int64_t       model_id{-1};
    const qint64  now  = QDateTime::currentSecsSinceEpoch();
    const QString uuid = dltool::common::uuid();
    if (!ensureModelStorage(database_, uuid, &err_msg))
    {
        spdlog::error("添加模型失败, 创建模型目录失败: {}", err_msg.toUtf8().constData());
        return false;
    }

    const bool ok = database_->addModel(uuid, trimmed_name, trimmed_framework_name, trimmed_model_architecture,
                                        QString(), QString(), now, now, model_id, err_msg);
    if (!ok)
    {
        QString remove_err;
        removeModelStorage(database_, uuid, &remove_err);
        spdlog::error("添加模型失败, 名称: {}, 框架: {}, 模型架构: {}, 错误: {}", trimmed_name.toUtf8().constData(),
                      trimmed_framework_name.toUtf8().constData(), trimmed_model_architecture.toUtf8().constData(),
                      err_msg.toUtf8().constData());
        return false;
    }

    const int row = rowCount();
    beginInsertRows(QModelIndex(), row, row);
    models_.push_back(ModelRecord{
        model_id,
        uuid,
        trimmed_name,
        trimmed_framework_name,
        trimmed_model_architecture,
        QString(),
        QString(),
        now,
        now,
    });
    endInsertRows();

    spdlog::info("模型添加成功, id: {}, 模型名称: {}, 框架: {}, 模型架构: {}", model_id,
                 trimmed_name.toUtf8().constData(), trimmed_framework_name.toUtf8().constData(),
                 trimmed_model_architecture.toUtf8().constData());
    return true;
}

bool ModelManager::renameModel(const qint64 model_id, const QString &name)
{
    const QString trimmed_name = name.trimmed();
    if (trimmed_name.isEmpty())
    {
        spdlog::warn("模型重命名失败: 模型名称为空");
        return false;
    }

    const int row = indexOfModel(model_id);
    if (row < 0)
    {
        spdlog::warn("模型重命名失败: 模型 {} 不存在", model_id);
        return false;
    }

    QString      err_msg;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const bool   ok  = database_ != nullptr && database_->updateModelName(model_id, trimmed_name, now, err_msg);
    if (!ok)
    {
        spdlog::error("重命名模型失败, id: {}, 错误: {}", model_id, err_msg.toUtf8().constData());
        return false;
    }

    models_[row].name  = trimmed_name;
    models_[row].mtime = now;
    emit dataChanged(index(row), index(row), {NameRole, MtimeRole});
    return true;
}

bool ModelManager::deleteModel(const qint64 model_id)
{
    const int row = indexOfModel(model_id);
    if (row < 0)
    {
        spdlog::warn("删除模型失败: 模型 {} 不存在", model_id);
        return false;
    }

    QString    err_msg;
    const QString uuid = models_[static_cast<size_t>(row)].uuid;
    const bool ok = database_ != nullptr && database_->deleteModel(model_id, err_msg);
    if (!ok)
    {
        spdlog::error("删除模型失败, id: {}, 错误: {}", model_id, err_msg.toUtf8().constData());
        return false;
    }

    beginRemoveRows(QModelIndex(), row, row);
    models_.erase(models_.begin() + row);
    endRemoveRows();
    model_instances_.erase(instanceKey(uuid));
    config_load_started_.erase(instanceKey(uuid));
    if (!removeModelStorage(database_, uuid, &err_msg))
    {
        spdlog::error("删除模型目录失败, uuid: {}, 错误: {}", uuid.toUtf8().constData(), err_msg.toUtf8().constData());
    }
    return true;
}

bool ModelManager::copyModel(const qint64 model_id)
{
    const int row = indexOfModel(model_id);
    if (row < 0)
    {
        spdlog::warn("复制模型失败: 模型 {} 不存在", model_id);
        return false;
    }

    const ModelRecord &source = models_[row];
    QString            err_msg;
    int64_t            new_model_id{-1};
    const qint64       now         = QDateTime::currentSecsSinceEpoch();
    const QString      copied_name = uniqueCopyName(source.name);
    const QString      new_uuid    = dltool::common::uuid();
    if (!ensureModelStorage(database_, new_uuid, &err_msg))
    {
        spdlog::error("复制模型失败, 创建模型目录失败: {}", err_msg.toUtf8().constData());
        return false;
    }

    const bool         ok          = database_ != nullptr
                 && database_->addModel(new_uuid, copied_name, source.framework_name, source.model_architecture,
                                        source.training_result, source.test_result, now, now, new_model_id, err_msg);
    if (!ok)
    {
        QString remove_err;
        removeModelStorage(database_, new_uuid, &remove_err);
        spdlog::error("复制模型失败, id: {}, 错误: {}", model_id, err_msg.toUtf8().constData());
        return false;
    }

    const int insert_row = rowCount();
    beginInsertRows(QModelIndex(), insert_row, insert_row);
    models_.push_back(ModelRecord{
        new_model_id,
        new_uuid,
        copied_name,
        source.framework_name,
        source.model_architecture,
        source.training_result,
        source.test_result,
        now,
        now,
    });
    endInsertRows();

    const auto source_found = model_instances_.find(instanceKey(source.uuid));
    if (source_found != model_instances_.end() && source_found->second)
    {
        auto copied_model = createRegisteredModelInstance(source.framework_name, source.model_architecture);
        if (copied_model && copied_model->config() && source_found->second->config())
        {
            ITrainParams       *target_train_params = copied_model->config()->trainParams();
            const ITrainParams *source_train_params = source_found->second->config()->trainParams();
            if (target_train_params != nullptr && source_train_params != nullptr)
            {
                target_train_params->copyValuesFrom(*source_train_params);
            }

            ITestParams       *target_test_params = copied_model->config()->testParams();
            const ITestParams *source_test_params = source_found->second->config()->testParams();
            if (target_test_params != nullptr && source_test_params != nullptr)
            {
                target_test_params->copyValuesFrom(*source_test_params);
            }

            copied_model->setParent(const_cast<ModelManager *>(this));
            copied_model->setUuid(new_uuid);
            QQmlEngine::setObjectOwnership(copied_model.get(), QQmlEngine::CppOwnership);
            model_instances_[instanceKey(new_uuid)] = std::move(copied_model);
            config_load_started_.insert(instanceKey(new_uuid));
        }
    }

    return true;
}

QStringList ModelManager::supportedFrameworks() const
{
    return registeredFrameworkNames(method_);
}

QStringList ModelManager::supportedModelArchitectures(const QString &framework_name) const
{
    return registeredModelArchitectures(method_, framework_name);
}

QStringList ModelManager::availableModelNames() const
{
    return registeredModelNames(method_);
}

QVariantMap ModelManager::modelAt(const int row) const
{
    if (row < 0 || row >= rowCount())
    {
        return {};
    }

    const ModelRecord &model = models_.at(static_cast<size_t>(row));
    return {
        {         QStringLiteral("model_id"), static_cast<qint64>(model.model_id)},
        {             QStringLiteral("uuid"),                          model.uuid},
        {             QStringLiteral("name"),                          model.name},
        {   QStringLiteral("framework_name"),                model.framework_name},
        {QStringLiteral("model_architecture"),          model.model_architecture},
        {  QStringLiteral("training_result"),               model.training_result},
        {      QStringLiteral("test_result"),                   model.test_result},
        {            QStringLiteral("ctime"),                         model.ctime},
        {            QStringLiteral("mtime"),                         model.mtime},
    };
}

IModel *ModelManager::modelForUuid(const QString &uuid) const
{
    const int row = indexOfUuid(uuid);
    if (row < 0)
        return nullptr;

    IModel *model = cachedModelForRecord(models_[static_cast<size_t>(row)]);
    if (model != nullptr)
        requestModelTaskConfigLoad(models_[static_cast<size_t>(row)].uuid);
    return model;
}

int ModelManager::addModelTask(const QString &model_uuid, const QString &model_name, const QString &task_type)
{
    return TaskManager::getInstance()->addModelTask(model_uuid, model_name, task_type);
}

int ModelManager::startModelTask(const QString &model_uuid, const QString &model_name, const QString &task_type)
{
    return TaskManager::getInstance()->startModelTask(model_uuid, model_name, task_type);
}

bool ModelManager::stopModelTask(const QString &model_uuid, const QString &task_type)
{
    return TaskManager::getInstance()->stopModelTask(model_uuid, task_type);
}

bool ModelManager::deleteModelTask(const QString &model_uuid, const QString &task_type)
{
    return TaskManager::getInstance()->deleteModelTask(model_uuid, task_type);
}

void ModelManager::requestModelTaskConfigLoad(const QString &model_uuid) const
{
    const QString trimmed_uuid = model_uuid.trimmed();
    if (trimmed_uuid.isEmpty())
        return;

    const std::string key = instanceKey(trimmed_uuid);
    if (config_load_started_.find(key) != config_load_started_.end())
        return;
    config_load_started_.insert(key);

    const LoadedModelTaskConfigs configs = loadModelTaskConfigs(
        trimmed_uuid, modelTaskConfigPath(database_, trimmed_uuid, ModelTaskConfigFile::Train),
        modelTaskConfigPath(database_, trimmed_uuid, ModelTaskConfigFile::Test));
    const_cast<ModelManager *>(this)->applyLoadedModelTaskConfigs(configs.model_uuid, configs.train_params,
                                                                  configs.test_params,
                                                                  configs.dataset_selections);
}

void ModelManager::applyLoadedModelTaskConfigs(const QString &model_uuid, const QVariantMap &train_params,
                                               const QVariantMap &test_params,
                                               const QVariantMap &dataset_selections)
{
    const auto found = model_instances_.find(instanceKey(model_uuid));
    if (found == model_instances_.end() || !found->second || found->second->config() == nullptr)
        return;

    IModel *model = found->second.get();
    IModelConfig *model_config = found->second->config();
    if (!train_params.isEmpty())
    {
        if (ITrainParams *params = model_config->trainParams(); params != nullptr)
            params->setValuesMap(train_params);
    }
    if (!test_params.isEmpty())
    {
        if (ITestParams *params = model_config->testParams(); params != nullptr)
            params->setValuesMap(test_params);
    }
    for (const ModelDatasetSplit split : modelDatasetSplits())
    {
        const QString split_name = modelDatasetSplitName(split);
        if (dataset_selections.contains(split_name))
            applyDatasetSelectionMap(datasetSelectionObject(model, split),
                                     dataset_selections.value(split_name).toMap());
    }
}

bool ModelManager::hasTaskHandler(int task_id) const
{
    auto *task_manager = TaskManager::getInstance();
    if (task_manager == nullptr || task_manager->tasks() == nullptr)
        return false;

    const QVariantMap task = task_manager->tasks()->taskForId(task_id);
    const QString model_uuid = task.value(QStringLiteral("model_uuid")).toString().trimmed();
    return !model_uuid.isEmpty() && modelForUuid(model_uuid) != nullptr;
}

bool ModelManager::modelTaskSupportsPause(const QString &model_uuid, const QString &task_type) const
{
    IModel *model = modelForUuid(model_uuid);
    if (model == nullptr)
        return true;

    const FrameworkDefinition framework = registeredFramework(method_, model->frameworkName());
    return !frameworkHasScript(framework, task_type.trimmed());
}

bool ModelManager::startTask(int task_id)
{
    auto *task_manager = TaskManager::getInstance();
    if (task_manager == nullptr || task_manager->tasks() == nullptr)
        return false;

    const QVariantMap task = task_manager->tasks()->taskForId(task_id);
    const int status_value = task.value(QStringLiteral("status_value"), TaskTableModel::Pending).toInt();
    if (status_value == TaskTableModel::Running)
        return true;

    const QString model_uuid = task.value(QStringLiteral("model_uuid")).toString().trimmed();
    const QString model_name = task.value(QStringLiteral("model_name")).toString().trimmed();
    const QString task_type = task.value(QStringLiteral("task_type")).toString().trimmed();
    IModel *model = modelForUuid(model_uuid);
    if (model == nullptr)
        return false;

    const FrameworkDefinition framework = registeredFramework(method_, model->frameworkName());
    if (frameworkHasScript(framework, task_type))
        return startExternalModelTask(model_uuid, model_name, task_type, task_id) >= 0;

    return true;
}

bool ModelManager::stopTask(int task_id)
{
    auto *task_manager = TaskManager::getInstance();
    if (task_manager == nullptr || task_manager->tasks() == nullptr || !hasTaskHandler(task_id))
        return false;

    const auto found = external_processes_.find(task_id);
    if (found != external_processes_.end() && found->second)
    {
        stop_requested_tasks_.insert(task_id);
        QProcess *process = found->second;
        if (process->state() != QProcess::NotRunning)
        {
            process->terminate();
            QTimer::singleShot(5000, process,
                               [process]()
                               {
                                   if (process->state() != QProcess::NotRunning)
                                       process->kill();
                               });
        }
    }

    return true;
}

bool ModelManager::deleteTask(int task_id)
{
    auto *task_manager = TaskManager::getInstance();
    if (task_manager == nullptr || task_manager->tasks() == nullptr || !hasTaskHandler(task_id))
        return false;

    stopTask(task_id);
    external_processes_.erase(task_id);
    stop_requested_tasks_.erase(task_id);
    return true;
}

bool ModelManager::registerFramework(const int method, const FrameworkDefinition &definition)
{
    FrameworkDefinition normalized = definition;
    normalized.method = method;
    normalized.name = definition.name.trimmed();
    const QString trimmed_framework_name = normalized.name;
    if (trimmed_framework_name.isEmpty())
    {
        return false;
    }

    auto      &registry = frameworkRegistry();
    const auto found
        = std::find_if(registry.begin(), registry.end(),
                       [method, &trimmed_framework_name](const RegisteredFramework &framework)
                       {
                           return framework.definition.method == method
                               && framework.definition.name == trimmed_framework_name;
                       });
    if (found != registry.end())
    {
        return false;
    }

    registry.push_back(RegisteredFramework{normalized});
    return true;
}

bool ModelManager::registerModel(const int method, const QString &framework_name, const QString &model_architecture,
                                 ModelFactory factory)
{
    const QString trimmed_framework_name     = framework_name.trimmed();
    const QString trimmed_model_architecture = model_architecture.trimmed();
    if (trimmed_framework_name.isEmpty() || trimmed_model_architecture.isEmpty() || !factory)
    {
        return false;
    }

    auto      &registry = modelRegistry();
    const auto found
        = std::find_if(registry.begin(), registry.end(),
                       [method, &trimmed_framework_name, &trimmed_model_architecture](const RegisteredModel &model)
                       {
                           return model.method == method && model.framework_name == trimmed_framework_name
                               && model.model_architecture == trimmed_model_architecture;
                       });
    if (found != registry.end())
    {
        return false;
    }

    registry.push_back(
        RegisteredModel{method, trimmed_framework_name, trimmed_model_architecture, std::move(factory)});
    return true;
}

ModelManager::FrameworkDefinition ModelManager::registeredFramework(const int method, const QString &framework_name)
{
    const QString trimmed_framework_name = framework_name.trimmed();
    if (trimmed_framework_name.isEmpty())
        return {};

    const auto &registry = frameworkRegistry();
    const auto  found
        = std::find_if(registry.begin(), registry.end(),
                       [method, &trimmed_framework_name](const RegisteredFramework &framework)
                       {
                           return (method < 0 || framework.definition.method == method)
                               && framework.definition.name == trimmed_framework_name;
                       });
    if (found == registry.end())
        return {};
    return resolvedFrameworkDefinition(found->definition);
}

QStringList ModelManager::registeredFrameworkNames(const int method)
{
    QStringList names;
    const auto &registry = frameworkRegistry();
    names.reserve(static_cast<int>(registry.size()));
    for (const RegisteredFramework &framework : registry)
    {
        if ((method < 0 || framework.definition.method == method) && framework.definition.visible_for_model_creation
            && !names.contains(framework.definition.name))
        {
            names.append(framework.definition.name);
        }
    }
    return names;
}

QStringList ModelManager::registeredModelArchitectures(const int method, const QString &framework_name)
{
    const QString trimmed_framework_name = framework_name.trimmed();
    QStringList   names;
    const auto   &registry = modelRegistry();
    names.reserve(static_cast<int>(registry.size()));
    for (const RegisteredModel &model : registry)
    {
        if ((method < 0 || model.method == method) && model.framework_name == trimmed_framework_name
            && !names.contains(model.model_architecture))
        {
            names.append(model.model_architecture);
        }
    }
    return names;
}

QStringList ModelManager::registeredModelNames(const int method)
{
    QStringList names;
    const auto &registry = modelRegistry();
    names.reserve(static_cast<int>(registry.size()));
    for (const RegisteredModel &model : registry)
    {
        if ((method < 0 || model.method == method) && !names.contains(model.model_architecture))
        {
            names.append(model.model_architecture);
        }
    }
    return names;
}

std::unique_ptr<IModel> ModelManager::createRegisteredModel(const int method, const QString &framework_name,
                                                            const QString &model_architecture)
{
    const QString trimmed_framework_name     = framework_name.trimmed();
    const QString trimmed_model_architecture = model_architecture.trimmed();
    if (trimmed_framework_name.isEmpty() || trimmed_model_architecture.isEmpty())
        return nullptr;

    const auto &registry = modelRegistry();
    const auto  found
        = std::find_if(registry.begin(), registry.end(),
                       [method, &trimmed_framework_name, &trimmed_model_architecture](const RegisteredModel &model)
                       {
                           return (method < 0 || model.method == method) && model.framework_name == trimmed_framework_name
                               && model.model_architecture == trimmed_model_architecture;
                       });
    if (found == registry.end() || !found->factory)
        return nullptr;

    return found->factory();
}

std::vector<std::unique_ptr<IModel>> ModelManager::registeredModels(const int method)
{
    std::vector<std::unique_ptr<IModel>> models;
    const auto                          &registry = modelRegistry();
    models.reserve(registry.size());
    for (const RegisteredModel &model : registry)
    {
        if ((method < 0 || model.method == method) && model.factory)
        {
            models.emplace_back(model.factory());
        }
    }
    return models;
}

std::unique_ptr<IModel> ModelManager::createRegisteredModelInstance(const QString &framework_name,
                                                                    const QString &model_architecture) const
{
    auto model = createRegisteredModel(method_, framework_name, model_architecture);
    initializeDatasetViewModels(model.get());
    return model;
}

std::vector<std::unique_ptr<IModel>> ModelManager::registeredModelInstances() const
{
    return registeredModels(method_);
}

int ModelManager::startExternalModelTask(const QString &model_uuid, const QString &model_name,
                                         const QString &task_type, int task_id)
{
    auto *task_manager = TaskManager::getInstance();
    if (task_manager == nullptr || task_manager->tasks() == nullptr)
        return -1;

    IModel *model = modelForUuid(model_uuid);
    if (model == nullptr)
        return -1;

    QString storage_err;
    if (!ensureModelStorage(database_, model_uuid, &storage_err))
    {
        spdlog::error("启动模型任务失败: 创建模型目录失败: {}", storage_err.toUtf8().constData());
        task_manager->tasks()->failTask(task_id);
        return task_id;
    }

    const FrameworkDefinition framework = registeredFramework(method_, model->frameworkName());
    const QString script_path = scriptForTask(framework, task_type);
    const auto running_process = external_processes_.find(task_id);
    if (running_process != external_processes_.end() && running_process->second
        && running_process->second->state() != QProcess::NotRunning)
    {
        return task_id;
    }

    if (script_path.isEmpty())
    {
        spdlog::warn("启动模型任务失败: 框架未定义脚本, 框架: {}, 任务: {}",
                     model->frameworkName().toUtf8().constData(), task_type.toUtf8().constData());
        return -1;
    }
    if (!QFileInfo::exists(script_path))
    {
        spdlog::error("启动模型任务失败: 脚本不存在 {}", script_path.toUtf8().constData());
        task_manager->tasks()->failTask(task_id);
        return task_id;
    }

    QString server_err;
    if (!task_manager->ensureTaskServer(&server_err))
    {
        spdlog::error("启动模型任务失败: 任务通信服务启动失败: {}", server_err.toUtf8().constData());
        task_manager->tasks()->failTask(task_id);
        return task_id;
    }

    namespace generated_field = dltool::settings::generated::field;
    const QString python_executable = pythonExecutableFromEnvPath(dltool::settings::settingString(
        dltool::settings::GlobalSettings::getInstance(), generated_field::Software::PythonEnvPath));
    if (python_executable.isEmpty())
    {
        spdlog::error("启动模型任务失败: 未配置 Python 环境目录");
        task_manager->tasks()->failTask(task_id);
        return task_id;
    }

    QString dataset_err;
    ModelDatasetExportContext dataset_context;
    dataset_context.method             = method_;
    dataset_context.framework_name     = model->frameworkName();
    dataset_context.model_architecture = model->modelArchitecture();
    dataset_context.model_uuid         = model_uuid;
    dataset_context.task_type          = task_type;
    dataset_context.dataset_dir        = modelStoragePath(database_, model_uuid, ModelStoragePath::Datasets);
    dataset_context.model              = model;
    dataset_context.data_manager       = data_manager_;
    const QVariantMap datasets = ModelDatasetOrganizer::organize(dataset_context, &dataset_err);
    if (datasets.isEmpty() && (isTrainTask(task_type) || isPredictTask(task_type)))
    {
        spdlog::error("启动模型任务失败: 数据集组织失败: {}", dataset_err.toUtf8().constData());
        task_manager->tasks()->failTask(task_id);
        return task_id;
    }

    QString config_err;
    const QString config_path = writeTaskConfigFile(
        modelStoragePath(database_, model_uuid, ModelStoragePath::Configs), task_type,
        modelTaskConfig(model, model_name, task_type, database_, datasets), &config_err);
    if (config_path.isEmpty())
    {
        spdlog::error("启动模型任务失败: {}", config_err.toUtf8().constData());
        task_manager->tasks()->failTask(task_id);
        return task_id;
    }

    auto *process = new QProcess(this);
    const QString log_dir = modelStoragePath(database_, model_uuid, ModelStoragePath::Logs);
    const QString log_stem = taskLogStem(task_type, task_id);
    QString       log_err;
    auto         *process_log = openProcessLogFile(QDir(log_dir).filePath(log_stem + QStringLiteral(".log")),
                                                   process, &log_err);
    if (process_log == nullptr)
    {
        spdlog::error("启动模型任务失败: {}", log_err.toUtf8().constData());
        task_manager->tasks()->failTask(task_id);
        process->deleteLater();
        return task_id;
    }

    process->setProgram(python_executable);
    process->setArguments({
        script_path,
        QStringLiteral("--config"),
        config_path,
        QStringLiteral("--dltool_task_host"),
        task_manager->taskServerHost(),
        QStringLiteral("--dltool_task_port"),
        QString::number(task_manager->taskServerPort()),
        QStringLiteral("--dltool_task_id"),
        QString::number(task_id),
    });
    process->setWorkingDirectory(framework.root);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString old_python_path = env.value(QStringLiteral("PYTHONPATH"));
    QStringList python_path_parts = framework.python_paths;
    if (!old_python_path.isEmpty())
        python_path_parts.append(old_python_path);
    if (!python_path_parts.isEmpty())
        env.insert(QStringLiteral("PYTHONPATH"), python_path_parts.join(QDir::listSeparator()));
    env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    process->setProcessEnvironment(env);
    process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(process, &QProcess::readyReadStandardOutput, this,
            [process, log_file = QPointer<QFile>(process_log)]()
            {
                const QByteArray output = process->readAllStandardOutput();
                if (log_file != nullptr && !output.isEmpty())
                {
                    log_file->write(output);
                    log_file->flush();
                }
            });
    connect(process, &QProcess::readyReadStandardError, this,
            [process, log_file = QPointer<QFile>(process_log)]()
            {
                const QByteArray output = process->readAllStandardError();
                if (log_file != nullptr && !output.isEmpty())
                {
                    log_file->write(output);
                    log_file->flush();
                }
            });
    connect(process, &QProcess::finished, this,
            [this, process, task_id, log_file = QPointer<QFile>(process_log)](int exit_code,
                                                                              QProcess::ExitStatus exit_status)
            {
                external_processes_.erase(task_id);
                const bool stop_requested = stop_requested_tasks_.erase(task_id) > 0;
                auto *task_manager = TaskManager::getInstance();
                if (task_manager != nullptr && task_manager->tasks() != nullptr)
                {
                    if (stop_requested)
                        task_manager->tasks()->setTaskStatus(task_id, TaskTableModel::Stopped);
                    else if (exit_status == QProcess::NormalExit && exit_code == 0)
                        task_manager->tasks()->setTaskStatus(task_id, TaskTableModel::Finished);
                    else if (exit_status == QProcess::NormalExit && exit_code == 2)
                        task_manager->tasks()->setTaskStatus(task_id, TaskTableModel::Stopped);
                    else
                        task_manager->tasks()->setTaskStatus(task_id, TaskTableModel::Failed);
                }
                if (log_file != nullptr)
                    log_file->close();
                process->deleteLater();
            });

    external_processes_[task_id] = process;
    task_manager->tasks()->setTaskStatus(task_id, TaskTableModel::Running);
    process->start();
    if (!process->waitForStarted(5000))
    {
        const QString error = process->errorString();
        if (process_log != nullptr)
        {
            process_log->write(error.toUtf8());
            process_log->write("\n");
            process_log->close();
        }
        spdlog::error("启动模型任务失败: {}", error.toUtf8().constData());
        external_processes_.erase(task_id);
        task_manager->tasks()->setTaskStatus(task_id, TaskTableModel::Failed);
        process->deleteLater();
    }
    return task_id;
}

bool ModelManager::frameworkHasScript(const FrameworkDefinition &framework, const QString &task_type) const
{
    return !scriptForTask(framework, task_type).isEmpty();
}

QString ModelManager::scriptForTask(const FrameworkDefinition &framework, const QString &task_type) const
{
    if (framework.name.isEmpty())
        return {};

    if (isTrainTask(task_type))
        return framework.train_script;
    if (isPredictTask(task_type))
        return framework.predict_script;
    const QString key = task_type.trimmed();
    return framework.scripts.value(key);
}

int ModelManager::indexOfModel(const int64_t model_id) const
{
    for (int i = 0; i < static_cast<int>(models_.size()); ++i)
    {
        if (models_[i].model_id == model_id)
            return i;
    }
    return -1;
}

int ModelManager::indexOfUuid(const QString &uuid) const
{
    const QString trimmed_uuid = uuid.trimmed();
    if (trimmed_uuid.isEmpty())
        return -1;
    for (int i = 0; i < static_cast<int>(models_.size()); ++i)
    {
        if (models_[static_cast<size_t>(i)].uuid == trimmed_uuid)
            return i;
    }
    return -1;
}

QString ModelManager::uniqueCopyName(const QString &name) const
{
    const QString base      = QString("%1 Copy").arg(name);
    QString       candidate = base;
    int           suffix    = 2;
    auto          exists    = [this](const QString &candidate_name)
    {
        return std::any_of(models_.begin(), models_.end(),
                           [&candidate_name](const ModelRecord &model) { return model.name == candidate_name; });
    };

    while (exists(candidate))
    {
        candidate = QString("%1 %2").arg(base).arg(suffix++);
    }
    return candidate;
}

IModel *ModelManager::cachedModelForRecord(const ModelRecord &record) const
{
    const QString trimmed_uuid               = record.uuid.trimmed();
    const QString trimmed_framework_name     = record.framework_name.trimmed();
    const QString trimmed_model_architecture = record.model_architecture.trimmed();
    if (trimmed_framework_name.isEmpty() || trimmed_model_architecture.isEmpty())
    {
        return nullptr;
    }

    if (trimmed_uuid.isEmpty() || indexOfUuid(trimmed_uuid) < 0)
    {
        return nullptr;
    }

    auto &model = model_instances_[instanceKey(trimmed_uuid)];
    if (!model || model->frameworkName() != trimmed_framework_name
        || model->modelArchitecture() != trimmed_model_architecture)
    {
        model = createRegisteredModelInstance(trimmed_framework_name, trimmed_model_architecture);
        if (model)
        {
            model->setParent(const_cast<ModelManager *>(this));
            model->setUuid(trimmed_uuid);
            QQmlEngine::setObjectOwnership(model.get(), QQmlEngine::CppOwnership);
        }
    }
    return model.get();
}

void ModelManager::initializeDatasetViewModels(IModel *model) const
{
    if (model == nullptr || data_manager_ == nullptr)
        return;

    if (model->trainDatasetViewModel() == nullptr)
    {
        model->setTrainDatasetViewModel(
            dltool::data::DatasetViewModelFactory::createDatasetSelectionModel(data_manager_, model));
    }
    if (model->validationDatasetViewModel() == nullptr)
    {
        model->setValidationDatasetViewModel(
            dltool::data::DatasetViewModelFactory::createDatasetSelectionModel(data_manager_, model));
    }
    if (model->testDatasetViewModel() == nullptr)
    {
        model->setTestDatasetViewModel(
            dltool::data::DatasetViewModelFactory::createDatasetSelectionModel(data_manager_, model));
    }
}

std::string ModelManager::instanceKey(const QString &uuid)
{
    return uuid.toStdString();
}

QVariant ModelManager::getModelId(const QModelIndex &index) const
{
    return static_cast<qint64>(models_.at(index.row()).model_id);
}

QVariant ModelManager::getUuid(const QModelIndex &index) const
{
    return models_.at(index.row()).uuid;
}

QVariant ModelManager::getName(const QModelIndex &index) const
{
    return models_.at(index.row()).name;
}

QVariant ModelManager::getFrameworkName(const QModelIndex &index) const
{
    return models_.at(index.row()).framework_name;
}

QVariant ModelManager::getModelArchitecture(const QModelIndex &index) const
{
    return models_.at(index.row()).model_architecture;
}

QVariant ModelManager::getTrainingResult(const QModelIndex &index) const
{
    const QString &value = models_.at(index.row()).training_result;
    return value.isEmpty() ? QString("未训练") : value;
}

QVariant ModelManager::getTestResult(const QModelIndex &index) const
{
    const QString &value = models_.at(index.row()).test_result;
    return value.isEmpty() ? QString("未测试") : value;
}

QVariant ModelManager::getCtime(const QModelIndex &index) const
{
    return QDateTime::fromSecsSinceEpoch(models_.at(index.row()).ctime).toString(QStringLiteral("yyyy/MM/dd hh:mm"));
}

QVariant ModelManager::getMtime(const QModelIndex &index) const
{
    return QDateTime::fromSecsSinceEpoch(models_.at(index.row()).mtime).toString(QStringLiteral("yyyy/MM/dd hh:mm"));
}

} // namespace dltool::model
