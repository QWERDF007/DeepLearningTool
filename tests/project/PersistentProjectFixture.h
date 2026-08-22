#pragma once

#include "core/CoreDef.h"
#include "model/ModelManager.h"
#include "model/TaskManager.h"

#include <QVariantMap>
namespace dltool::data {
class DataManager;
}

namespace dltool::project {
class Project;
class ProjectManager;
}

namespace dltool::settings {
class GlobalSettings;
}

namespace dltool::model::integration {

class PythonEnvironmentScope
{
public:
    explicit PythonEnvironmentScope(QString path = {});
    ~PythonEnvironmentScope();

    PythonEnvironmentScope(const PythonEnvironmentScope &)            = delete;
    PythonEnvironmentScope &operator=(const PythonEnvironmentScope &) = delete;

    bool    isValid() const;
    QString error() const;
    QString path() const;

private:
    settings::GlobalSettings *settings_{nullptr};
    QVariant                  previous_path_;
    bool                      previous_auto_save_{true};
    QString                   path_;
    QString                   error_;
};

class PersistentProjectFixture
{
public:
    static constexpr int kMethod = static_cast<int>(dltool::core::DeepLearningMethod::AnomalyDetection);

    static QString projectRoot();
    static QString projectName();
    static QString projectDatabasePath();
    static QString assetRoot();
    static QString imageRoot();
    static QString maskRoot();
    static QString pythonEnvironmentPath();

    static QString datasetName();
    static QString labelMeRoundtripDatasetName();
    static QString cocoRoundtripDatasetName();
    static QString maskRoundtripDatasetName();
    static QString patchcoreModelName();
    static QString patchcoreModelCopyName();
    static QString patchcoreModelRenameName();
    static QString patchcoreTestName();
    static QString dataExportRoot();
    static QString maskExportRoot();
    static QString labelMeExportRoot();
    static QString cocoExportRoot();

    explicit PersistentProjectFixture(bool create_if_missing = false);
    ~PersistentProjectFixture();

    PersistentProjectFixture(const PersistentProjectFixture &)            = delete;
    PersistentProjectFixture &operator=(const PersistentProjectFixture &) = delete;

    bool    isValid() const;
    QString error() const;

    project::Project *project() const;
    data::DataManager *dataManager() const;

    qint64 ensureDataset(const QString &name, QString *error = nullptr) const;
    bool   datasetCounts(qint64 dataset_id, int *image_count, int *label_count, QString *error = nullptr) const;

    bool findPatchcoreModel(QString *model_uuid, QString *error = nullptr) const;
    bool ensurePatchcoreModel(QString *model_uuid, QString *error = nullptr) const;
    bool configurePatchcore(QString *model_uuid, qint64 dataset_id, QString *task_uuid,
                            QString *error = nullptr) const;
    bool findPatchcoreTask(const QString &model_uuid, QString *task_uuid, QString *error = nullptr) const;

    bool importData(qint64 dataset_id, int format, const QString &image_dir, const QString &data_dir,
                   const QVariantMap &label_class_groups, QString *error = nullptr,
                   int timeout_ms = 120000) const;
    bool exportData(qint64 dataset_id, int format, const QString &output_dir, int minimum_images,
                   QString *error = nullptr, int timeout_ms = 120000) const;

    static bool waitForTask(TaskManager *task_manager, int task_id, QString *error = nullptr,
                            int timeout_ms = 1800000);

private:
    bool setPatchcoreParameter(IParams *params, const QString &group_name, const QString &name,
                               const QVariant &value, QString *error) const;
    bool savePatchcoreConfiguration(const QString &model_uuid, qint64 dataset_id, QString *task_uuid,
                                   QString *error) const;

    PythonEnvironmentScope          python_scope_;
    project::ProjectManager         *project_manager_{nullptr};
    project::Project                *project_{nullptr};
    QString                         error_;
};

} // namespace dltool::model::integration
