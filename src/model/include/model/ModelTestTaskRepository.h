#pragma once

#include "dltool/model/Export.h"
#include "model/ModelTestTask.h"

#include <QList>
#include <QString>
#include <QVariantMap>

#include <optional>

namespace dltool::model {

/**
 * @brief 测试任务定义的唯一持久化入口。
 *
 * 索引保存于 models/<model>/test/tasks.yaml，任务配置保存于各自任务目录。
 * 仓库不维护运行状态，也不暴露 QObject，便于任务控制器和 QML 管理器共享。
 */
class MODEL_API ModelTestTaskRepository
{
public:
    explicit ModelTestTaskRepository(QString project_dir = {});

    void setProjectDirectory(const QString &project_dir);
    QString projectDirectory() const;

    QList<ModelTestTaskDefinition> listTasks(const QString &model_name, QString *err_msg = nullptr) const;
    QString currentTaskUuid(const QString &model_name, QString *err_msg = nullptr) const;
    bool setCurrentTaskUuid(const QString &model_name, const QString &uuid, QString *err_msg = nullptr) const;
    bool loadTask(const QString &model_name, const QString &uuid, ModelTestTaskDefinition &task,
                  QString *err_msg = nullptr) const;
    bool saveTask(const QString &model_name, const ModelTestTaskDefinition &task, QString *err_msg = nullptr) const;
    bool createTask(const QString &model_name, const QString &model_uuid, const QString &name,
                    const QVariantMap &test_params, const ModelDatasetSelection &dataset_selection,
                    ModelTestTaskDefinition &task, QString *err_msg = nullptr) const;
    bool renameTask(const QString &model_name, const QString &uuid, const QString &name,
                    QString *err_msg = nullptr) const;
    bool removeTask(const QString &model_name, const QString &uuid, QString *err_msg = nullptr) const;
    bool writeResult(const QString &model_name, const QString &task_directory, const QVariantMap &result,
                     QString *err_msg = nullptr) const;

    QString tasksPath(const QString &model_name) const;

    static QString validateTaskName(const QString &name);
    static QString directoryNameForTask(const QString &name);

private:
    bool writeIndex(const QString &model_name, const QList<ModelTestTaskDefinition> &tasks,
                    QString *err_msg = nullptr,
                    const std::optional<QString> &current_uuid_override = std::nullopt) const;
    bool writeTaskConfig(const QString &model_name, const ModelTestTaskDefinition &task,
                         QString *err_msg = nullptr) const;
    bool readTaskConfig(const QString &model_name, ModelTestTaskDefinition &task, QString *err_msg = nullptr) const;
    bool ensureTaskRoot(const QString &model_name, const ModelTestTaskDefinition &task,
                        QString *err_msg = nullptr) const;

    QString project_dir_;
};

} // namespace dltool::model
