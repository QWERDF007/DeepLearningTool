#include "model/ModelStorageService.h"

#include "common/Utils.h"

#include <QDir>
#include <QFileInfo>
#include <array>
#include <map>

namespace dltool::model {

namespace {

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

const std::array<ModelStorageLocation, 5> &modelChildLocations()
{
    static const std::array<ModelStorageLocation, 5> locations = {
        ModelStorageLocation::Results,  ModelStorageLocation::Logs,    ModelStorageLocation::Weights,
        ModelStorageLocation::Datasets, ModelStorageLocation::Configs,
    };
    return locations;
}

} // namespace

QString cleanModelPath(const QString &path)
{
    return dltool::common::cleanPath(path);
}

QString modelStorageLocationName(ModelStorageLocation location)
{
    const auto &names = storageLocationNames();
    const auto  found = names.find(location);
    return found != names.end() ? found->second : QString();
}

ModelStorageService::ModelStorageService(QString project_dir)
    : project_dir_(cleanModelPath(project_dir))
{
}

void ModelStorageService::setProjectDirectory(const QString &project_dir)
{
    project_dir_ = cleanModelPath(project_dir);
}

QString ModelStorageService::projectDirectory() const
{
    return project_dir_;
}

QString ModelStorageService::path(const QString &uuid, ModelStorageLocation location) const
{
    const QString project_dir = projectDirectory();
    if (project_dir.isEmpty())
        return {};

    const QString root
        = cleanModelPath(QDir(project_dir).filePath(modelStorageLocationName(ModelStorageLocation::ModelsRoot)));
    if (location == ModelStorageLocation::ModelsRoot)
        return root;

    const QString trimmed_uuid = uuid.trimmed();
    if (trimmed_uuid.isEmpty())
        return {};

    const QString model_dir = cleanModelPath(QDir(root).filePath(trimmed_uuid));
    if (location == ModelStorageLocation::ModelRoot)
        return model_dir;

    const QString child_name = modelStorageLocationName(location);
    if (child_name.isEmpty())
        return {};
    return cleanModelPath(QDir(model_dir).filePath(child_name));
}

bool ModelStorageService::ensureModelStorage(const QString &uuid, QString *err_msg) const
{
    const QString model_dir = path(uuid, ModelStorageLocation::ModelRoot);
    if (!ensureDirectory(model_dir, err_msg))
        return false;

    for (const ModelStorageLocation child_location : modelChildLocations())
    {
        if (!ensureDirectory(path(uuid, child_location), err_msg))
            return false;
    }
    return true;
}

bool ModelStorageService::removeModelStorage(const QString &uuid, QString *err_msg) const
{
    const QString root   = cleanModelPath(QFileInfo(path({}, ModelStorageLocation::ModelsRoot)).absoluteFilePath());
    const QString target = cleanModelPath(QFileInfo(path(uuid, ModelStorageLocation::ModelRoot)).absoluteFilePath());
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

bool ModelStorageService::ensureDirectory(const QString &path, QString *err_msg) const
{
    const QString cleaned = cleanModelPath(path);
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

} // namespace dltool::model
