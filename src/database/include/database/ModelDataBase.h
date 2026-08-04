#pragma once

#include "database/DataBase.h"
#include "database/ModelDatabaseTypes.h"

#include <QList>
#include <QString>
#include <QVariantMap>

namespace dltool::database {

/**
 * @brief Persistent storage owned by one model directory.
 *
 * The project database remains the source of model metadata.  This database
 * only stores model parameters, dataset selections and the test-task index.
 */
class DATABASE_API ModelDataBase final : public DataBase
{
public:
    explicit ModelDataBase(const QString &path, QObject *parent = nullptr);
    ~ModelDataBase() override;

    bool readTrainParams(QVariantMap &params, QString *err_msg = nullptr) const;
    bool replaceTrainParams(const QVariantMap &params, QString *err_msg = nullptr) const;

    bool readDatasets(QList<DatasetSelectionRecord> &selections, QString *err_msg = nullptr) const;
    bool replaceDatasets(const QList<DatasetSelectionRecord> &selections, QString *err_msg = nullptr) const;

    bool listTestTasks(QList<ModelTestTaskRecord> &tasks, QString *err_msg = nullptr) const;
    bool upsertTestTask(const ModelTestTaskRecord &task, QString *err_msg = nullptr) const;
    bool removeTestTask(const QString &task_id, QString *err_msg = nullptr) const;

private:
    bool ensureSchema(QString *err_msg = nullptr) const;
};

} // namespace dltool::database

