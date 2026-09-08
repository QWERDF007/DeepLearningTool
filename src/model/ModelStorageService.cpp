#include "model/ModelStorageService.h"

#include "common/Utils.h"
#include "database/ModelDataBase.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
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
        {     ModelStorageLocation::Train,    QStringLiteral("train")},
        {      ModelStorageLocation::Test,     QStringLiteral("test")},
        {      ModelStorageLocation::Logs,     QStringLiteral("logs")},
        {   ModelStorageLocation::Weights,  QStringLiteral("weights")},
        {  ModelStorageLocation::Datasets, QStringLiteral("datasets")},
    };
    return names;
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

QString ModelStorageService::modelsRootPath() const
{
    return path({}, ModelStorageLocation::ModelsRoot);
}

QString ModelStorageService::modelRoot(const QString &model_name) const
{
    return path(model_name, ModelStorageLocation::ModelRoot);
}

QString ModelStorageService::modelDatabasePathAt(const QString &model_root) const
{
    const QString root = cleanPath(QFileInfo(model_root).absoluteFilePath());
    return root.isEmpty() ? QString() : cleanPath(QDir(root).filePath(QStringLiteral("model.db")));
}

QString ModelStorageService::trainWeightsPathAt(const QString &model_root) const
{
    const QString root = cleanPath(QFileInfo(model_root).absoluteFilePath());
    return root.isEmpty() ? QString() : cleanPath(QDir(root).filePath(QStringLiteral("train/weights")));
}

QString ModelStorageService::modelDatabasePath(const QString &model_name) const
{
    return modelDatabasePathAt(modelRoot(model_name));
}

QString ModelStorageService::sharedDatasetPath(const QString &model_name) const
{
    return path(model_name, ModelStorageLocation::Datasets);
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

    const QString result     = cleanPath(QDir(root).filePath(value));
    const QString clean_root = cleanPath(QFileInfo(root).absoluteFilePath());
    if (result.isEmpty() || clean_root.isEmpty()
        || !result.startsWith(clean_root + QStringLiteral("/"), Qt::CaseInsensitive))
        return {};
    return result;
}

bool isModelStoragePath(const QString &models_root, const QString &candidate)
{
    const QString root = cleanPath(QFileInfo(models_root).absoluteFilePath());
    const QString path = cleanPath(QFileInfo(candidate).absoluteFilePath());
    return !root.isEmpty() && !path.isEmpty() && path != root
        && path.startsWith(root + QStringLiteral("/"), Qt::CaseInsensitive);
}

} // namespace

QString ModelStorageService::trainRoot(const QString &model_name) const
{
    return path(model_name, ModelStorageLocation::Train);
}

QString ModelStorageService::trainWeightsPath(const QString &model_name) const
{
    return trainWeightsPathAt(modelRoot(model_name));
}

QString ModelStorageService::trainLogsPath(const QString &model_name) const
{
    const QString root = trainRoot(model_name);
    return root.isEmpty() ? QString() : cleanPath(QDir(root).filePath(QStringLiteral("logs")));
}

QString ModelStorageService::trainDatasetPath(const QString &model_name) const
{
    return sharedDatasetPath(model_name);
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

QString ModelStorageService::testTaskRoot(const QString &model_name, const QString &task_directory) const
{
    return safeTaskChild(testRoot(model_name), task_directory);
}

QString ModelStorageService::testTaskDatabasePath(const QString &model_name, const QString &task_directory) const
{
    const QString root = testTaskRoot(model_name, task_directory);
    return root.isEmpty() ? QString() : cleanPath(QDir(root).filePath(QStringLiteral("task.db")));
}

QString ModelStorageService::testTaskFileListPath(const QString &model_name, const QString &task_directory) const
{
    const QString task_root = testTaskRoot(model_name, task_directory);
    return task_root.isEmpty() ? QString() : cleanPath(QDir(task_root).filePath(QString("test.txt")));
}

QString ModelStorageService::testTaskPredictionPath(const QString &model_name, const QString &task_directory) const
{
    const QString root = testTaskRoot(model_name, task_directory);
    return root.isEmpty() ? QString() : cleanPath(QDir(root).filePath(QStringLiteral("pred")));
}

QString ModelStorageService::testTaskLogPath(const QString &model_name, const QString &task_directory) const
{
    const QString root = testTaskRoot(model_name, task_directory);
    if (root.isEmpty())
        return {};
    return cleanPath(QDir(root).filePath(QStringLiteral("test.log")));
}

ModelTaskPaths ModelStorageService::trainPaths(const QString &model_name) const
{
    ModelTaskPaths paths;
    paths.model_root    = path(model_name, ModelStorageLocation::ModelRoot);
    paths.task_root     = trainRoot(model_name);
    paths.database_path = modelDatabasePath(model_name);
    paths.dataset_dir   = trainDatasetPath(model_name);
    paths.weight_dir    = trainWeightsPath(model_name);
    paths.log_dir       = trainLogsPath(model_name);
    paths.log_path      = trainLogPath(model_name);
    return paths;
}

ModelTaskPaths ModelStorageService::testPaths(const QString &model_name, const QString &task_directory) const
{
    ModelTaskPaths paths;
    paths.model_root     = path(model_name, ModelStorageLocation::ModelRoot);
    paths.task_root      = testTaskRoot(model_name, task_directory);
    paths.database_path  = testTaskDatabasePath(model_name, task_directory);
    paths.dataset_dir    = sharedDatasetPath(model_name);
    paths.weight_dir     = trainWeightsPath(model_name);
    paths.log_dir        = paths.task_root;
    paths.log_path       = testTaskLogPath(model_name, task_directory);
    paths.prediction_dir = testTaskPredictionPath(model_name, task_directory);
    return paths;
}

QString ModelStorageService::operationRoot() const
{
    const QString root = modelsRootPath();
    return root.isEmpty() ? QString() : cleanPath(QDir(root).filePath(QStringLiteral(".operations")));
}

QString ModelStorageService::operationStagingRoot(const QString &operation_id) const
{
    return safeTaskChild(operationRoot(), QStringLiteral("staging-%1").arg(operation_id.trimmed()));
}

QString ModelStorageService::operationQuarantineRoot(const QString &operation_id) const
{
    return safeTaskChild(operationRoot(), QStringLiteral("quarantine-%1").arg(operation_id.trimmed()));
}

QString ModelStorageService::operationJournalPath(const QString &operation_id) const
{
    return safeTaskChild(operationRoot(), QStringLiteral("%1.json").arg(operation_id.trimmed()));
}

QString ModelStorageService::testTaskOperationRoot(const QString &model_name) const
{
    const QString root = testRoot(model_name);
    return root.isEmpty() ? QString() : cleanPath(QDir(root).filePath(QStringLiteral(".operations")));
}

QString ModelStorageService::testTaskOperationJournalPath(const QString &model_name, const QString &operation_id) const
{
    return safeTaskChild(testTaskOperationRoot(model_name), QStringLiteral("%1.json").arg(operation_id.trimmed()));
}

QString ModelStorageService::testTaskOperationStagingRoot(const QString &model_name, const QString &operation_id) const
{
    return safeTaskChild(testTaskOperationRoot(model_name), QStringLiteral("staging-%1").arg(operation_id.trimmed()));
}

QString ModelStorageService::testTaskOperationQuarantineRoot(const QString &model_name, const QString &operation_id) const
{
    return safeTaskChild(testTaskOperationRoot(model_name), QStringLiteral("quarantine-%1").arg(operation_id.trimmed()));
}

bool ModelStorageService::ensureTestTaskStorageAt(const QString &task_root, QString *err_msg) const
{
    const QString root = cleanPath(QFileInfo(task_root).absoluteFilePath());
    if (root.isEmpty())
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("测试任务目录路径无效");
        return false;
    }
    if (!ensureDirectory(root, err_msg, QStringLiteral("测试任务目录为空"),
                         QStringLiteral("创建测试任务目录失败: %1")))
        return false;
    const QString pred_dir = cleanPath(QDir(root).filePath(QStringLiteral("pred")));
    if (!ensureDirectory(pred_dir, err_msg, QStringLiteral("测试任务子目录为空"),
                         QStringLiteral("创建测试任务子目录失败: %1")))
        return false;
    const QString predictions_dir = cleanPath(QDir(root).filePath(QStringLiteral("predictions")));
    if (!ensureDirectory(predictions_dir, err_msg, QStringLiteral("测试任务子目录为空"),
                         QStringLiteral("创建测试任务子目录失败: %1")))
        return false;
    return true;
}

bool ModelStorageService::ensureModelStorageAt(const QString &model_root, QString *err_msg) const
{
    const QString root = cleanPath(QFileInfo(model_root).absoluteFilePath());
    if (!isModelStoragePath(modelsRootPath(), root))
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("模型存储路径非法");
        return false;
    }
    if (!ensureDirectory(root, err_msg, QStringLiteral("模型目录为空"), QStringLiteral("创建模型目录失败: %1")))
        return false;

    const QString database_path = modelDatabasePathAt(root);
    if (database_path.isEmpty())
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("模型数据库路径为空");
        return false;
    }
    database::ModelDataBase model_database(database_path);
    Q_UNUSED(model_database)

    for (const QString &directory : {QDir(root).filePath(QStringLiteral("train")),
                                     QDir(root).filePath(QStringLiteral("train/weights")),
                                     QDir(root).filePath(QStringLiteral("train/logs")),
                                     QDir(root).filePath(QStringLiteral("test")),
                                     QDir(root).filePath(QStringLiteral("datasets"))})
    {
        if (!ensureDirectory(directory, err_msg, QStringLiteral("模型子目录为空"),
                             QStringLiteral("创建模型子目录失败: %1")))
            return false;
    }
    return true;
}

namespace {

bool copyFileChunked(const QString &source, const QString &destination, QString *err_msg,
                     const std::function<bool()> &is_cancelled)
{
    QFile src_file(source);
    if (!src_file.open(QIODevice::ReadOnly))
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("打开源模型文件失败: %1").arg(src_file.errorString());
        return false;
    }

    QFile dst_file(destination);
    if (!dst_file.open(QIODevice::WriteOnly))
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("创建目标模型文件失败: %1").arg(dst_file.errorString());
        return false;
    }

    constexpr qint64 kChunkSize = 64 * 1024;
    QByteArray       buffer;
    buffer.resize(kChunkSize);

    while (!src_file.atEnd())
    {
        if (is_cancelled && is_cancelled())
        {
            dst_file.close();
            QFile::remove(destination);
            if (err_msg != nullptr)
                *err_msg = QStringLiteral("复制模型文件已取消");
            return false;
        }

        const qint64 bytes_read = src_file.read(buffer.data(), kChunkSize);
        if (bytes_read < 0)
        {
            if (err_msg != nullptr)
                *err_msg = QStringLiteral("读取源模型文件失败: %1").arg(src_file.errorString());
            dst_file.close();
            QFile::remove(destination);
            return false;
        }

        if (dst_file.write(buffer.constData(), bytes_read) != bytes_read)
        {
            if (err_msg != nullptr)
                *err_msg = QStringLiteral("写入目标模型文件失败: %1").arg(dst_file.errorString());
            dst_file.close();
            QFile::remove(destination);
            return false;
        }
    }
    return true;
}

} // namespace

bool ModelStorageService::copyDirectoryContents(const QString &source, const QString &target, QString *err_msg,
                                                std::function<bool()> is_cancelled) const
{
    const QFileInfo source_info(source);
    if (!source_info.exists() || !source_info.isDir())
        return true;
    if (!isModelStoragePath(modelsRootPath(), target) || !QDir().mkpath(target))
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("创建模型复制目录失败: %1").arg(target);
        return false;
    }

    QDirIterator iterator(source, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        if (is_cancelled && is_cancelled())
        {
            if (err_msg != nullptr)
                *err_msg = QStringLiteral("复制模型目录已取消");
            return false;
        }

        const QFileInfo item(iterator.next());
        const QString   relative    = QDir(source).relativeFilePath(item.absoluteFilePath());
        const QString   destination = QDir(target).filePath(relative);
        if (item.isDir())
        {
            if (!QDir().mkpath(destination))
            {
                if (err_msg != nullptr)
                    *err_msg = QStringLiteral("创建模型复制子目录失败: %1").arg(destination);
                return false;
            }
        }
        else if (!copyFileChunked(item.absoluteFilePath(), destination, err_msg, is_cancelled))
        {
            return false;
        }
    }
    return true;
}

bool ModelStorageService::moveDirectory(const QString &source, const QString &target, QString *err_msg)
{
    const QString source_path = cleanPath(QFileInfo(source).absoluteFilePath());
    const QString target_path = cleanPath(QFileInfo(target).absoluteFilePath());
    if (!isModelStoragePath(modelsRootPath(), source_path) || !isModelStoragePath(modelsRootPath(), target_path))
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("模型目录移动路径非法");
        return false;
    }
    if (!QDir(source_path).exists())
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("源模型目录不存在: %1").arg(source_path);
        return false;
    }
    if (QFileInfo::exists(target_path))
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("目标模型目录已存在: %1").arg(target_path);
        return false;
    }
    if (!QDir().mkpath(QFileInfo(target_path).absolutePath()) || !QFile::rename(source_path, target_path))
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("移动模型目录失败: %1 -> %2").arg(source_path, target_path);
        return false;
    }
    return true;
}

bool ModelStorageService::removeDirectory(const QString &root, QString *err_msg) const
{
    const QString target = cleanPath(QFileInfo(root).absoluteFilePath());
    if (!isModelStoragePath(modelsRootPath(), target))
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("拒绝删除非法模型目录: %1").arg(target);
        return false;
    }
    if (!QDir(target).exists() || QDir(target).removeRecursively())
        return true;
    if (err_msg != nullptr)
        *err_msg = QStringLiteral("删除模型目录失败: %1").arg(target);
    return false;
}

bool ModelStorageService::ensureTrainStorage(const QString &model_name, QString *err_msg) const
{
    if (!ensureDirectory(trainRoot(model_name), err_msg, QString("训练目录为空"), QString("创建训练目录失败: %1")))
        return false;
    for (const QString &directory :
         {trainWeightsPath(model_name), trainLogsPath(model_name), sharedDatasetPath(model_name)})
    {
        if (!ensureDirectory(directory, err_msg, QString("训练子目录为空"), QString("创建训练子目录失败: %1")))
            return false;
    }
    return true;
}

bool ModelStorageService::ensureTestStorage(const QString &model_name, QString *err_msg) const
{
    return ensureDirectory(testRoot(model_name), err_msg, QString("测试目录为空"), QString("创建测试目录失败: %1"));
}

bool ModelStorageService::ensureTestTaskStorage(const QString &model_name, const QString &task_directory,
                                                QString *err_msg) const
{
    if (!ensureTestStorage(model_name, err_msg))
        return false;
    const QString task_root = testTaskRoot(model_name, task_directory);
    return ensureTestTaskStorageAt(task_root, err_msg);
}

bool ModelStorageService::ensureModelStorage(const QString &model_name, QString *err_msg) const
{
    return ensureModelStorageAt(modelRoot(model_name), err_msg);
}

bool ModelStorageService::removeModelStorage(const QString &model_name, QString *err_msg) const
{
    const QString target = modelRoot(model_name);
    if (!QDir(target).exists())
        return true;
    return removeDirectory(target, err_msg);
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
