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
const std::array<ModelStorageLocation, 5> &modelChildLocations()
{
    static const std::array<ModelStorageLocation, 5> locations = {
        ModelStorageLocation::Results,  ModelStorageLocation::Logs,    ModelStorageLocation::Weights,
        ModelStorageLocation::Datasets, ModelStorageLocation::Configs,
    };
    return locations;
}

} // namespace

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
    return true;
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
