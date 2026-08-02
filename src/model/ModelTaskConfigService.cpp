#include "model/ModelTaskConfigService.h"

#include "common/Utils.h"
#include "common/YamlUtils.h"
#include "model/ModelTaskTypes.h"

#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#include <QDir>
#include <QFileInfo>
#include <map>
#include <utility>

using dltool::common::cleanPath;

namespace dltool::model {

namespace {

QVariantMap selectionToMap(const ModelDatasetSelection &selection)
{
    QVariantList dataset_ids;
    for (const qint64 id : selection.dataset_ids)
        dataset_ids.push_back(id);

    QVariantList label_classes;
    for (const auto &[dataset_id, label_class_id] : selection.label_classes)
    {
        label_classes.push_back(QVariantMap{{QStringLiteral("dataset_id"), dataset_id},
                                            {QStringLiteral("label_class_id"), label_class_id}});
    }
    return {{QStringLiteral("dataset_ids"), dataset_ids}, {QStringLiteral("label_classes"), label_classes}};
}

/**
 * @brief 获取任务配置文件名映射表
 * @return 文件名映射
 */
const std::map<ModelTaskConfigFile, QString> &taskConfigFileNames()
{
    static const std::map<ModelTaskConfigFile, QString> names = {
        {ModelTaskConfigFile::Train, QStringLiteral("config.yaml")},
        { ModelTaskConfigFile::Test,  QStringLiteral("config.yaml")},
    };
    return names;
}

/**
 * @brief 获取任务配置字段名映射表
 * @return 字段名映射
 */
const std::map<ModelTaskConfigField, QString> &taskConfigFieldNames()
{
    static const std::map<ModelTaskConfigField, QString> names = {
        {        ModelTaskConfigField::ModelUuid,         QStringLiteral("model_uuid")},
        {        ModelTaskConfigField::ModelName,         QStringLiteral("model_name")},
        {         ModelTaskConfigField::TaskType,          QStringLiteral("task_type")},
        {        ModelTaskConfigField::Framework,          QStringLiteral("framework")},
        {             ModelTaskConfigField::Method,             QStringLiteral("method")},
        {ModelTaskConfigField::ModelArchitecture, QStringLiteral("model_architecture")},
        {         ModelTaskConfigField::ModelDir,          QStringLiteral("model_dir")},
        {        ModelTaskConfigField::ResultDir,         QStringLiteral("result_dir")},
        {           ModelTaskConfigField::LogDir,            QStringLiteral("log_dir")},
        {        ModelTaskConfigField::WeightDir,         QStringLiteral("weight_dir")},
        {         ModelTaskConfigField::Datasets,           QStringLiteral("datasets")},
        {      ModelTaskConfigField::TrainParams,       QStringLiteral("train_params")},
        {       ModelTaskConfigField::TestParams,        QStringLiteral("test_params")},
        {          ModelTaskConfigField::Trainer,            QStringLiteral("trainer")},
        {        ModelTaskConfigField::Inference,          QStringLiteral("inference")},
        {        ModelTaskConfigField::OutputDir,         QStringLiteral("output_dir")},
        {     ModelTaskConfigField::PredictionDir,      QStringLiteral("prediction_dir")},
        {     ModelTaskConfigField::TestTaskUuid,      QStringLiteral("test_task_uuid")},
    };
    return names;
}

/**
 * @brief 解析输出目录路径（绝对路径直接返回，相对路径基于任务配置根目录拼接）
 * @param output_root 任务配置根目录
 * @param path 配置路径
 * @param fallback 回退路径
 * @return 解析后的绝对路径
 */
QString resolveModelOutputPath(const QString &output_root, const QString &path, const QString &fallback)
{
    QString value = cleanPath(path);
    if (value.isEmpty())
        value = fallback;
    if (QFileInfo(value).isAbsolute())
        return value;
    return cleanPath(QDir(output_root).filePath(value));
}

QString relativeConfigPath(const QString &config_root, const QString &path)
{
    const QString root = cleanPath(QFileInfo(config_root).absoluteFilePath());
    const QString value = cleanPath(path);
    if (root.isEmpty() || value.isEmpty())
        return value;
    if (!QFileInfo(value).isAbsolute())
        return value;
    return QDir::fromNativeSeparators(QDir(root).relativeFilePath(value));
}

void makeDatasetPathsRelative(QVariantMap &datasets, const QString &config_root)
{
    for (const QString &split : {QStringLiteral("train"), QStringLiteral("validation"), QStringLiteral("test")})
    {
        QVariantMap entry = datasets.value(split).toMap();
        if (entry.isEmpty())
            continue;
        for (const QString &field : {QStringLiteral("manifest"), QStringLiteral("file_list"),
                                     QStringLiteral("masks_dir")})
        {
            const QString value = entry.value(field).toString();
            if (!value.isEmpty())
                entry.insert(field, relativeConfigPath(config_root, value));
        }
        datasets.insert(split, entry);
    }
}

/**
 * @brief 规范化参数组中的输出目录路径
 * @param groups 参数组键值对
 * @param group_name 组名称
 * @param output_root 相对输出目录的解析根目录
 * @param fallback 回退路径
 */
void normalizeOutputDir(QVariantMap &groups, const QString &group_name, const QString &output_root,
                        const QString &fallback, const QString &config_root)
{
    QVariantMap group = groups.value(group_name).toMap();
    if (group.isEmpty())
        return;

    const QString output_dir = modelTaskConfigFieldName(ModelTaskConfigField::OutputDir);
    group.insert(output_dir, relativeConfigPath(
                                  config_root,
                                  resolveModelOutputPath(output_root, group.value(output_dir).toString(), fallback)));
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

ModelTaskConfigService::ModelTaskConfigService(QString project_dir)
    : storage_(std::move(project_dir))
{
}

void ModelTaskConfigService::setProjectDirectory(const QString &project_dir)
{
    storage_.setProjectDirectory(project_dir);
}

QString ModelTaskConfigService::configPath(const QString &model_name, ModelTaskConfigFile file) const
{
    if (file == ModelTaskConfigFile::Train)
        return storage_.trainConfigPath(model_name);
    // FS-SAM2 is an internal few-shot pipeline rather than a regular test
    // task.  It still consumes the historical model-level test.yaml while
    // ordinary tests always use test/<task>/config.yaml through the overload
    // below.
    const QString config_dir = storage_.path(model_name, ModelStorageLocation::Configs);
    return config_dir.isEmpty() ? QString() : cleanPath(QDir(config_dir).filePath(QString("test.yaml")));
}

QString ModelTaskConfigService::configPath(const QString &model_name, const ModelTaskType task_type,
                                           const QString &task_directory) const
{
    if (isTrainModelTask(task_type))
        return storage_.trainConfigPath(model_name);
    if (isTestModelTask(task_type))
        return task_directory.trimmed().isEmpty() ? configPath(model_name, ModelTaskConfigFile::Test)
                                                   : storage_.testTaskConfigPath(model_name, task_directory);
    return {};
}

LoadedModelTaskConfigs ModelTaskConfigService::load(const QString &model_uuid, const QString &model_name) const
{
    const QString train_config_path = configPath(model_name, ModelTaskConfigFile::Train);

    LoadedModelTaskConfigs configs;
    configs.model_uuid   = model_uuid;
    configs.train_params = readParams(train_config_path, ModelTaskConfigField::TrainParams);
    // Test parameters belong to a concrete ModelTestTaskDefinition and are
    // loaded by ModelTestTaskManager.  Never leak a train/config.yaml test
    // section into the model-wide template.
    configs.test_params = {};

    return configs;
}

QVariantMap ModelTaskConfigService::build(const ModelTaskConfigInput &model, ModelTaskType task_type,
                                          const QVariantMap &datasets) const
{
    if (model.model_uuid.trimmed().isEmpty() || model.model_name.trimmed().isEmpty())
        return {};

    const QString model_dir = storage_.path(model.model_name, ModelStorageLocation::ModelRoot);
    const bool is_train = isTrainModelTask(task_type);
    const QString task_root = is_train ? storage_.trainRoot(model.model_name)
                                       : storage_.testTaskRoot(model.model_name, model.task_directory);
    const bool legacy_few_shot_test = !is_train && model.task_directory.trimmed().isEmpty();
    const QString result_dir = is_train
        ? task_root
        : (legacy_few_shot_test ? storage_.path(model.model_name, ModelStorageLocation::Results)
                                : storage_.testTaskPredictionPath(model.model_name, model.task_directory));
    const QString log_dir = is_train
        ? storage_.trainLogsPath(model.model_name)
        : (legacy_few_shot_test ? storage_.path(model.model_name, ModelStorageLocation::Logs)
                                : storage_.testLogsPath(model.model_name));
    const QString weight_dir = storage_.trainWeightsPath(model.model_name);

    QVariantMap config;
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::ModelUuid), model.model_uuid);
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::ModelName), model.model_name);
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::TaskType), modelTaskKey(task_type));
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::Framework), model.framework_name);
    if (!model.method.trimmed().isEmpty())
        config.insert(modelTaskConfigFieldName(ModelTaskConfigField::Method), model.method);
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::ModelArchitecture), model.model_architecture);
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::ModelDir), relativeConfigPath(task_root, model_dir));
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::ResultDir), relativeConfigPath(task_root, result_dir));
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::LogDir), relativeConfigPath(task_root, log_dir));
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::WeightDir), relativeConfigPath(task_root, weight_dir));
    if (!model.scope_uuid.trimmed().isEmpty())
        config.insert(modelTaskConfigFieldName(ModelTaskConfigField::TestTaskUuid), model.scope_uuid);
    if (!is_train)
        config.insert(modelTaskConfigFieldName(ModelTaskConfigField::PredictionDir),
                      relativeConfigPath(task_root, result_dir));

    // A regular test task uses test/<task>/config.yaml as both the runner
    // configuration and the durable task definition.  Preserve the complete
    // definition here so a run cannot replace it with a runtime-only map.
    if (!is_train && !legacy_few_shot_test)
    {
        config.insert(QStringLiteral("uuid"), model.scope_uuid);
        config.insert(QStringLiteral("name"), model.scope_name);
        config.insert(QStringLiteral("directory_name"), model.task_directory);
        config.insert(QStringLiteral("created_at"), model.created_at);
        config.insert(QStringLiteral("modified_at"), model.modified_at);
        config.insert(QStringLiteral("dataset_selection"), selectionToMap(model.test_dataset_selection));
    }
    if (!datasets.isEmpty())
    {
        QVariantMap relative_datasets = datasets;
        makeDatasetPathsRelative(relative_datasets, task_root);
        config.insert(modelTaskConfigFieldName(ModelTaskConfigField::Datasets), relative_datasets);
    }

    if (is_train)
    {
        QVariantMap train_values = model.train_params;
        normalizeOutputDir(train_values, modelTaskConfigFieldName(ModelTaskConfigField::Trainer), task_root, weight_dir,
                           task_root);
        if (!train_values.isEmpty())
            config.insert(modelTaskConfigFieldName(ModelTaskConfigField::TrainParams), train_values);
    }
    else
    {
        // A test uses the model trained under train/.  Preserve the model
        // structure parameters in the runner config so Python can recreate
        // the exact backbone/pre-processing before loading the checkpoint.
        QVariantMap train_values = model.train_params;
        QVariantMap model_values = train_values.value(QStringLiteral("model")).toMap();
        if (model_values.isEmpty())
        {
            train_values = readParams(storage_.trainConfigPath(model.model_name), ModelTaskConfigField::TrainParams);
            model_values = train_values.value(QStringLiteral("model")).toMap();
        }
        if (!model_values.isEmpty())
            config.insert(modelTaskConfigFieldName(ModelTaskConfigField::TrainParams),
                          QVariantMap{{QStringLiteral("model"), model_values}});

        QVariantMap test_values = model.test_params;
        // Regular tests resolve relative inference output paths from
        // test/<task>/, while the legacy FS-SAM2 config keeps its historical
        // model-root-relative behavior.
        const QString output_root = legacy_few_shot_test ? model_dir : task_root;
        normalizeOutputDir(test_values, modelTaskConfigFieldName(ModelTaskConfigField::Inference), output_root, result_dir,
                           task_root);
        if (!test_values.isEmpty())
            config.insert(modelTaskConfigFieldName(ModelTaskConfigField::TestParams), test_values);

        if (!legacy_few_shot_test)
        {
            const QVariantMap persisted_test_params = model.task_definition_test_params.isEmpty()
                ? model.test_params
                : model.task_definition_test_params;
            config.insert(QStringLiteral("task_definition"),
                          QVariantMap{{QStringLiteral("uuid"), model.scope_uuid},
                                      {QStringLiteral("model_uuid"), model.model_uuid},
                                      {QStringLiteral("name"), model.scope_name},
                                      {QStringLiteral("directory_name"), model.task_directory},
                                      {QStringLiteral("created_at"), model.created_at},
                                      {QStringLiteral("modified_at"), model.modified_at},
                                      {QStringLiteral("test_params"), persisted_test_params},
                                      {QStringLiteral("dataset_selection"),
                                       selectionToMap(model.test_dataset_selection)}});
        }
    }
    return config;
}

QString ModelTaskConfigService::write(const QString &model_name, ModelTaskType task_type, const QVariantMap &config,
                                      QString *err_msg) const
{
    return write(model_name, task_type, {}, config, err_msg);
}

QString ModelTaskConfigService::write(const QString &model_name, const ModelTaskType task_type,
                                      const QString &task_directory, const QVariantMap &config,
                                      QString *err_msg) const
{
    const QString path = configPath(model_name, task_type, task_directory);
    if (path.isEmpty())
    {
        if (err_msg != nullptr)
            *err_msg = isTestModelTask(task_type) ? QString("测试任务目录为空")
                                                  : QString("任务配置路径为空");
        return {};
    }
    const QString config_dir = QFileInfo(path).absolutePath();
    QDir dir(config_dir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
    {
        if (err_msg != nullptr)
            *err_msg = QString("创建任务配置目录失败: %1").arg(config_dir);
        return {};
    }

    if (!dltool::common::yaml::writeFileAtomic(path, dltool::common::yaml::variantToYaml(config), err_msg,
                                               QString("写入任务配置失败"),
                                               QString("生成任务 YAML 配置失败"),
                                               QString("提交任务 YAML 配置失败")))
        return {};
    return path;
}

QVariantMap ModelTaskConfigService::readParams(const QString &path, ModelTaskConfigField field) const
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
                   root[dltool::common::yaml::toYamlString(modelTaskConfigFieldName(field))])
            .toMap();
    }
    catch (const std::exception &e)
    {
        spdlog::error("读取模型任务配置失败 '{}': {}", file_info.absoluteFilePath().toUtf8().constData(), e.what());
        return {};
    }
}

} // namespace dltool::model
