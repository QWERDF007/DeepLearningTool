#include "model/ModelStorageService.h"

#include "common/Utils.h"

#include <QDir>
#include <QFileInfo>
#include <array>
#include <map>

using dltool::common::cleanPath;
using dltool::common::ensureDirectory;

namespace dltool::model {

namespace {

/**
 * @brief 获取存储位置名称映射表
 * @return 位置名映射
 */
const std::map<ModelStorageLocation, QString> &storageLocationNames()
{
    static const std::map<ModelStorageLocation, QString> names = {
        {ModelStorageLocation::ModelsRoot,   QStringLiteral("models")},
        { ModelStorageLocation::ModelRoot,                         {}},
        {    ModelStorageLocation::Train,     QStringLiteral("train")},
        {     ModelStorageLocation::Test,      QStringLiteral("test")},
        {   ModelStorageLocation::Results,  QStringLiteral("results")},
        {      ModelStorageLocation::Logs,     QStringLiteral("logs")},
        {   ModelStorageLocation::Weights,  QStringLiteral("weights")},
        {  ModelStorageLocation::Datasets, QStringLiteral("datasets")},
        {   ModelStorageLocation::Configs,  QStringLiteral("configs")},
    };
    return names;
}

/**
 * @brief 获取模型子目录位置列表
 * @return 子目录位置数组
 */
const std::array<ModelStorageLocation, 2> &modelChildLocations()
{
    static const std::array<ModelStorageLocation, 2> locations = {ModelStorageLocation::Train, ModelStorageLocation::Test};
    return locations;
}

} // namespace

QString cleanModelPath(const QString &path)
{
    return cleanPath(path);
}

QString modelStorageLocationName(ModelStorageLocation location)
{
    const auto &names = storageLocationNames();
    const auto  found = names.find(location);
    return found != names.end() ? found->second : QString();
}

ModelStorageService::ModelStorageService(QString project_dir)
    : project_dir_(cleanPath(project_dir))
{
}

void ModelStorageService::setProjectDirectory(const QString &project_dir)
{
    project_dir_ = cleanPath(project_dir);
}

QString ModelStorageService::projectDirectory() const
{
    return project_dir_;
}

QString ModelStorageService::storageMetadataPath(const QString &model_name) const
{
    const QString root = path(model_name, ModelStorageLocation::ModelRoot);
    return root.isEmpty() ? QString() : cleanPath(QDir(root).filePath(QStringLiteral("storage.yaml")));
}

QString ModelStorageService::path(const QString &model_name, ModelStorageLocation location) const
{
    const QString project_dir = projectDirectory();
    if (project_dir.isEmpty())
        return {};

    const QString root
        = cleanPath(QDir(project_dir).filePath(modelStorageLocationName(ModelStorageLocation::ModelsRoot)));
    if (location == ModelStorageLocation::ModelsRoot)
        return root;

    const QString trimmed_model_name = model_name.trimmed();
    if (trimmed_model_name.isEmpty())
        return {};

    const QString model_dir = cleanPath(QDir(root).filePath(trimmed_model_name));
    if (model_dir == root || !model_dir.startsWith(root + QStringLiteral("/"), Qt::CaseInsensitive))
        return {};
    if (location == ModelStorageLocation::ModelRoot)
        return model_dir;

    const QString child_name = modelStorageLocationName(location);
    if (child_name.isEmpty())
        return {};
    return cleanPath(QDir(model_dir).filePath(child_name));
}

namespace {

QString safeTaskChild(const QString &root, const QString &child)
{
    const QString value = child.trimmed();
    if (root.isEmpty() || value.isEmpty() || value == QStringLiteral(".") || value == QStringLiteral("..")
        || value.contains(QChar('/')) || value.contains(QChar('\\')))
        return {};

    const QString result = cleanPath(QDir(root).filePath(value));
    const QString clean_root = cleanPath(QFileInfo(root).absoluteFilePath());
    if (result.isEmpty() || clean_root.isEmpty() || !result.startsWith(clean_root + QStringLiteral("/"),
                                                                  Qt::CaseInsensitive))
        return {};
    return result;
}

} // namespace

QString ModelStorageService::trainRoot(const QString &model_name) const
{
    return path(model_name, ModelStorageLocation::Train);
}

QString ModelStorageService::trainConfigPath(const QString &model_name) const
{
    const QString root = trainRoot(model_name);
    return root.isEmpty() ? QString() : cleanPath(QDir(root).filePath(QStringLiteral("config.yaml")));
}

QString ModelStorageService::trainWeightsPath(const QString &model_name) const
{
    const QString root = trainRoot(model_name);
    return root.isEmpty() ? QString() : cleanPath(QDir(root).filePath(QStringLiteral("weights")));
}

QString ModelStorageService::trainLogsPath(const QString &model_name) const
{
    const QString root = trainRoot(model_name);
    return root.isEmpty() ? QString() : cleanPath(QDir(root).filePath(QStringLiteral("logs")));
}

QString ModelStorageService::trainDatasetPath(const QString &model_name) const
{
    const QString root = trainRoot(model_name);
    return root.isEmpty() ? QString() : cleanPath(QDir(root).filePath(QStringLiteral("datasets")));
}

QString ModelStorageService::trainLogPath(const QString &model_name) const
{
    const QString root = trainLogsPath(model_name);
    return root.isEmpty() ? QString() : cleanPath(QDir(root).filePath(QStringLiteral("train.log")));
}

QString ModelStorageService::testRoot(const QString &model_name) const
{
    return path(model_name, ModelStorageLocation::Test);
}

QString ModelStorageService::testTasksPath(const QString &model_name) const
{
    const QString root = testRoot(model_name);
    return root.isEmpty() ? QString() : cleanPath(QDir(root).filePath(QStringLiteral("tasks.yaml")));
}

QString ModelStorageService::testLogsPath(const QString &model_name) const
{
    const QString root = testRoot(model_name);
    return root.isEmpty() ? QString() : cleanPath(QDir(root).filePath(QStringLiteral("logs")));
}

QString ModelStorageService::testTaskRoot(const QString &model_name, const QString &task_directory) const
{
    return safeTaskChild(testRoot(model_name), task_directory);
}

QString ModelStorageService::testTaskConfigPath(const QString &model_name, const QString &task_directory) const
{
    const QString root = testTaskRoot(model_name, task_directory);
    return root.isEmpty() ? QString() : cleanPath(QDir(root).filePath(QStringLiteral("config.yaml")));
}

QString ModelStorageService::testTaskDatasetPath(const QString &model_name, const QString &task_directory) const
{
    const QString root = testTaskRoot(model_name, task_directory);
    return root.isEmpty() ? QString() : cleanPath(QDir(root).filePath(QStringLiteral("datasets")));
}

QString ModelStorageService::testTaskPredictionPath(const QString &model_name, const QString &task_directory) const
{
    const QString root = testTaskRoot(model_name, task_directory);
    return root.isEmpty() ? QString() : cleanPath(QDir(root).filePath(QStringLiteral("pred")));
}

QString ModelStorageService::testTaskPredictionConfigPath(const QString &model_name, const QString &task_directory) const
{
    const QString root = testTaskPredictionPath(model_name, task_directory);
    return root.isEmpty() ? QString() : cleanPath(QDir(root).filePath(QStringLiteral("config.yaml")));
}

QString ModelStorageService::testTaskPredictionImagesPath(const QString &model_name, const QString &task_directory) const
{
    const QString root = testTaskPredictionPath(model_name, task_directory);
    return root.isEmpty() ? QString() : cleanPath(QDir(root).filePath(QStringLiteral("images.txt")));
}

QString ModelStorageService::testTaskPredictionManifestPath(const QString &model_name, const QString &task_directory) const
{
    const QString root = testTaskPredictionPath(model_name, task_directory);
    return root.isEmpty() ? QString() : cleanPath(QDir(root).filePath(QStringLiteral("manifest.yaml")));
}

QString ModelStorageService::testTaskDatasetManifestPath(const QString &model_name,
                                                         const QString &task_directory) const
{
    const QString root = testTaskDatasetPath(model_name, task_directory);
    if (root.isEmpty())
        return {};

    for (const QString &relative : {QStringLiteral("test/manifest.yaml"), QStringLiteral("manifest.yaml"),
                                    QStringLiteral("test.yaml")})
    {
        const QString candidate = cleanPath(QDir(root).filePath(relative));
        if (QFileInfo(candidate).isFile())
            return candidate;
    }
    return {};
}

QString ModelStorageService::testTaskLogPath(const QString &model_name, const QString &task_uuid) const
{
    const QString root = testLogsPath(model_name);
    const QString safe_uuid = task_uuid.trimmed();
    if (root.isEmpty() || safe_uuid.isEmpty() || safe_uuid.contains(QChar('/')) || safe_uuid.contains(QChar('\\')))
        return {};
    return cleanPath(QDir(root).filePath(safe_uuid + QStringLiteral(".log")));
}

ModelTaskPaths ModelStorageService::trainPaths(const QString &model_name) const
{
    ModelTaskPaths paths;
    paths.model_root = path(model_name, ModelStorageLocation::ModelRoot);
    paths.task_root = trainRoot(model_name);
    paths.editable_config_path = trainConfigPath(model_name);
    paths.dataset_dir = trainDatasetPath(model_name);
    paths.weight_dir = trainWeightsPath(model_name);
    paths.log_dir = trainLogsPath(model_name);
    paths.log_path = trainLogPath(model_name);
    return paths;
}

ModelTaskPaths ModelStorageService::testPaths(const QString &model_name, const QString &task_directory,
                                              const QString &task_uuid) const
{
    ModelTaskPaths paths;
    paths.model_root = path(model_name, ModelStorageLocation::ModelRoot);
    paths.task_root = testTaskRoot(model_name, task_directory);
    paths.editable_config_path = testTaskConfigPath(model_name, task_directory);
    paths.dataset_dir = testTaskDatasetPath(model_name, task_directory);
    paths.weight_dir = trainWeightsPath(model_name);
    paths.log_dir = testLogsPath(model_name);
    // Test logs are shared by all task directories and are keyed by the
    // stable task UUID, so a rename never changes the log association.
    paths.log_path = testTaskLogPath(model_name, task_uuid.trimmed().isEmpty() ? task_directory : task_uuid);
    paths.prediction_dir = testTaskPredictionPath(model_name, task_directory);
    paths.prediction_config_path = testTaskPredictionConfigPath(model_name, task_directory);
    paths.prediction_images_path = testTaskPredictionImagesPath(model_name, task_directory);
    paths.prediction_manifest_path = testTaskPredictionManifestPath(model_name, task_directory);
    return paths;
}

bool ModelStorageService::ensureTrainStorage(const QString &model_name, QString *err_msg) const
{
    if (!ensureDirectory(trainRoot(model_name), err_msg, QString("训练目录为空"), QString("创建训练目录失败: %1")))
        return false;
    for (const QString &directory : {trainWeightsPath(model_name), trainLogsPath(model_name),
                                    trainDatasetPath(model_name)})
    {
        if (!ensureDirectory(directory, err_msg, QString("训练子目录为空"), QString("创建训练子目录失败: %1")))
            return false;
    }
    return true;
}

bool ModelStorageService::ensureTestStorage(const QString &model_name, QString *err_msg) const
{
    if (!ensureDirectory(testRoot(model_name), err_msg, QString("测试目录为空"), QString("创建测试目录失败: %1")))
        return false;
    return ensureDirectory(testLogsPath(model_name), err_msg, QString("测试日志目录为空"),
                           QString("创建测试日志目录失败: %1"));
}

bool ModelStorageService::ensureTestTaskStorage(const QString &model_name, const QString &task_directory,
                                                QString *err_msg) const
{
    if (!ensureTestStorage(model_name, err_msg))
        return false;
    const QString task_root = testTaskRoot(model_name, task_directory);
    if (!ensureDirectory(task_root, err_msg, QString("测试任务目录为空"),
                         QString("创建测试任务目录失败: %1")))
        return false;
    for (const QString &directory : {testTaskDatasetPath(model_name, task_directory),
                                     testTaskPredictionPath(model_name, task_directory)})
    {
        if (!ensureDirectory(directory, err_msg, QString("测试任务子目录为空"),
                             QString("创建测试任务子目录失败: %1")))
            return false;
    }
    return true;
}

bool ModelStorageService::ensureModelStorage(const QString &model_name, QString *err_msg) const
{
    const QString model_dir = path(model_name, ModelStorageLocation::ModelRoot);
    if (!ensureDirectory(model_dir, err_msg, QString("目录路径为空"), QString("创建目录失败: %1")))
        return false;

    for (const ModelStorageLocation child_location : modelChildLocations())
    {
        if (!ensureDirectory(path(model_name, child_location), err_msg, QString("目录路径为空"),
                             QString("创建目录失败: %1")))
            return false;
    }
    return ensureTrainStorage(model_name, err_msg) && ensureTestStorage(model_name, err_msg);
}

bool ModelStorageService::removeModelStorage(const QString &model_name, QString *err_msg) const
{
    const QString root   = cleanPath(QFileInfo(path({}, ModelStorageLocation::ModelsRoot)).absoluteFilePath());
    const QString target = cleanPath(QFileInfo(path(model_name, ModelStorageLocation::ModelRoot)).absoluteFilePath());
    if (root.isEmpty() || target.isEmpty() || target == root
        || !target.startsWith(root + QStringLiteral("/"), Qt::CaseInsensitive))
    {
        if (err_msg != nullptr)
            *err_msg = QString("拒绝删除非法模型目录: %1").arg(target);
        return false;
    }

    QDir dir(target);
    if (!dir.exists())
        return true;
    if (!dir.removeRecursively())
    {
        if (err_msg != nullptr)
            *err_msg = QString("删除模型目录失败: %1").arg(target);
        return false;
    }
    return true;
}

bool ModelStorageService::renameModelStorage(const QString &old_model_name, const QString &new_model_name,
                                             QString *err_msg) const
{
    const QString source = path(old_model_name, ModelStorageLocation::ModelRoot);
    const QString target = path(new_model_name, ModelStorageLocation::ModelRoot);
    if (source.isEmpty() || target.isEmpty())
    {
        if (err_msg != nullptr)
            *err_msg = QString("模型目录路径为空");
        return false;
    }
    if (source == target)
        return true;
    if (!QDir(source).exists())
    {
        if (err_msg != nullptr)
            *err_msg = QString("模型目录不存在: %1").arg(source);
        return false;
    }
    if (QDir(target).exists())
    {
        if (err_msg != nullptr)
            *err_msg = QString("目标模型目录已存在: %1").arg(target);
        return false;
    }

    QDir parent_dir(QFileInfo(source).absoluteDir());
    if (!parent_dir.rename(QFileInfo(source).fileName(), QFileInfo(target).fileName()))
    {
        if (err_msg != nullptr)
            *err_msg = QString("重命名模型目录失败: %1 -> %2").arg(source, target);
        return false;
    }
    return true;
}

} // namespace dltool::model
