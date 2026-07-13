#pragma once

#include "dltool/model/Export.h"

#include <QString>

namespace dltool::model {

enum class ModelStorageLocation
{
    ModelsRoot,
    ModelRoot,
    Results,
    Logs,
    Weights,
    Datasets,
    Configs,
};

MODEL_API QString cleanModelPath(const QString &path);
MODEL_API QString modelStorageLocationName(ModelStorageLocation location);

class MODEL_API ModelStorageService
{
public:
    explicit ModelStorageService(QString project_dir = {});

    void setProjectDirectory(const QString &project_dir);

    QString projectDirectory() const;
    QString path(const QString &model_name, ModelStorageLocation location) const;

    bool ensureModelStorage(const QString &model_name, QString *err_msg = nullptr) const;
    bool removeModelStorage(const QString &model_name, QString *err_msg = nullptr) const;
    bool renameModelStorage(const QString &old_model_name, const QString &new_model_name,
                            QString *err_msg = nullptr) const;

private:
    QString project_dir_;
};

} // namespace dltool::model
