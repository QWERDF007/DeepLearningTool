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

/**
 * @brief 获取任务配置文件名映射表
 * @return 文件名映射
 */
const std::map<ModelTaskConfigFile, QString> &taskConfigFileNames()
{
    static const std::map<ModelTaskConfigFile, QString> names = {
        {ModelTaskConfigFile::Train, QStringLiteral("train.yaml")},
        { ModelTaskConfigFile::Test,  QStringLiteral("test.yaml")},
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
    };
    return names;
}

/**
 * @brief 解析输出目录路径（绝对路径直接返回，相对路径基于 model_dir 拼接）
 * @param model_dir 模型目录
 * @param path 配置路径
 * @param fallback 回退路径
 * @return 解析后的绝对路径
 */
QString resolveModelOutputPath(const QString &model_dir, const QString &path, const QString &fallback)
{
    QString value = cleanPath(path);
    if (value.isEmpty())
        value = fallback;
    if (QFileInfo(value).isAbsolute())
        return value;
    return cleanPath(QDir(model_dir).filePath(value));
}

/**
 * @brief 规范化参数组中的输出目录路径
 * @param groups 参数组键值对
 * @param group_name 组名称
 * @param model_dir 模型目录
 * @param fallback 回退路径
 */
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
    const QString config_dir = storage_.path(model_name, ModelStorageLocation::Configs);
    const QString file_name  = modelTaskConfigFileName(file);
    if (config_dir.isEmpty() || file_name.isEmpty())
        return {};
    return cleanPath(QDir(config_dir).filePath(file_name));
}

LoadedModelTaskConfigs ModelTaskConfigService::load(const QString &model_uuid, const QString &model_name) const
{
    const QString train_config_path = configPath(model_name, ModelTaskConfigFile::Train);
    const QString test_config_path  = configPath(model_name, ModelTaskConfigFile::Test);

    LoadedModelTaskConfigs configs;
    configs.model_uuid   = model_uuid;
    configs.train_params = readParams(train_config_path, ModelTaskConfigField::TrainParams);
    configs.test_params  = readParams(test_config_path, ModelTaskConfigField::TestParams);

    if (configs.train_params.isEmpty())
        configs.train_params = readParams(test_config_path, ModelTaskConfigField::TrainParams);
    if (configs.test_params.isEmpty())
        configs.test_params = readParams(train_config_path, ModelTaskConfigField::TestParams);

    return configs;
}

QVariantMap ModelTaskConfigService::build(const ModelTaskConfigInput &model, ModelTaskType task_type,
                                          const QVariantMap &datasets) const
{
    if (model.model_uuid.trimmed().isEmpty() || model.model_name.trimmed().isEmpty())
        return {};

    const QString model_dir  = storage_.path(model.model_name, ModelStorageLocation::ModelRoot);
    const QString result_dir = storage_.path(model.model_name, ModelStorageLocation::Results);
    const QString log_dir    = storage_.path(model.model_name, ModelStorageLocation::Logs);
    const QString weight_dir = storage_.path(model.model_name, ModelStorageLocation::Weights);

    QVariantMap config;
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::ModelUuid), model.model_uuid);
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::ModelName), model.model_name);
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::TaskType), modelTaskKey(task_type));
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::Framework), model.framework_name);
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::ModelArchitecture), model.model_architecture);
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::ModelDir), model_dir);
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::ResultDir), result_dir);
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::LogDir), log_dir);
    config.insert(modelTaskConfigFieldName(ModelTaskConfigField::WeightDir), weight_dir);
    if (!datasets.isEmpty())
        config.insert(modelTaskConfigFieldName(ModelTaskConfigField::Datasets), datasets);

    QVariantMap train_values = model.train_params;
    normalizeOutputDir(train_values, modelTaskConfigFieldName(ModelTaskConfigField::Trainer), model_dir,
                       modelStorageLocationName(ModelStorageLocation::Results));
    if (!train_values.isEmpty())
        config.insert(modelTaskConfigFieldName(ModelTaskConfigField::TrainParams), train_values);

    QVariantMap test_values = model.test_params;
    normalizeOutputDir(test_values, modelTaskConfigFieldName(ModelTaskConfigField::Inference), model_dir,
                       modelStorageLocationName(ModelStorageLocation::Results));
    if (!test_values.isEmpty())
        config.insert(modelTaskConfigFieldName(ModelTaskConfigField::TestParams), test_values);
    return config;
}

QString ModelTaskConfigService::write(const QString &model_name, ModelTaskType task_type, const QVariantMap &config,
                                      QString *err_msg) const
{
    const QString config_dir = storage_.path(model_name, ModelStorageLocation::Configs);
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
                                         QStringLiteral("写入任务配置失败"), QStringLiteral("生成任务 YAML 配置失败")))
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
