#pragma once

#include "dltool/model/Export.h"
#include "model/ModelTestTask.h"

#include <QList>
#include <QString>
#include <QVariantMap>

namespace dltool::model {

/**
 * @brief 测试任务定义的唯一持久化入口。
 *
 * 索引保存于模型目录的 model.db，任务参数和数据集选择保存于各自任务
 * 目录的 task.db。仓库不维护运行状态，也不暴露 QObject。
 */
class MODEL_API ModelTestTaskRepository
{
public:
    explicit ModelTestTaskRepository(QString project_dir = {});

    void setProjectDirectory(const QString &project_dir);
    QString projectDirectory() const;

    QList<ModelTestTaskDefinition> listTasks(const QString &model_name, QString *err_msg = nullptr) const;
    bool loadTask(const QString &model_name, const QString &uuid, ModelTestTaskDefinition &task,
                  QString *err_msg = nullptr) const;
    bool saveTask(const QString &model_name, const ModelTestTaskDefinition &task, QString *err_msg = nullptr) const;
    bool createTask(const QString &model_name, const QString &model_uuid, const QString &name,
                    const QVariantMap &test_params, const ModelDatasetSelection &dataset_selection,
                    ModelTestTaskDefinition &task, QString *err_msg = nullptr) const;
    bool renameTask(const QString &model_name, const QString &uuid, const QString &name,
                    QString *err_msg = nullptr) const;
    bool removeTask(const QString &model_name, const QString &uuid, QString *err_msg = nullptr) const;

    QString modelDatabasePath(const QString &model_name) const;

    static QString validateTaskName(const QString &name);
    static QString directoryNameForTask(const QString &name);

private:
    bool ensureTaskRoot(const QString &model_name, const ModelTestTaskDefinition &task,
                        QString *err_msg = nullptr) const;

    QString project_dir_;
};

} // namespace dltool::model
