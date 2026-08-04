#pragma once

#include "database/DataBase.h"
#include "database/ModelDatabaseTypes.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QVariantMap>

namespace dltool::database {

/**
 * @brief Persistent storage owned by one test-task directory.
 */
class DATABASE_API ModelTaskDataBase final : public DataBase
{
public:
    explicit ModelTaskDataBase(const QString &path, QObject *parent = nullptr);
    ~ModelTaskDataBase() override;

    bool readTaskInfo(TaskInfoRecord &info, QString *err_msg = nullptr) const;
    bool upsertTaskInfo(const TaskInfoRecord &info, QString *err_msg = nullptr) const;

    bool readTestParams(QVariantMap &params, QString *err_msg = nullptr) const;
    bool replaceTestParams(const QVariantMap &params, QString *err_msg = nullptr) const;

    bool readDatasets(QList<DatasetSelectionRecord> &selections, QString *err_msg = nullptr) const;
    bool replaceDatasets(const QList<DatasetSelectionRecord> &selections, QString *err_msg = nullptr) const;

    bool clearPredictions(QString *err_msg = nullptr) const;
    bool readPredictions(QHash<qint64, QVariant> &predictions, QString *err_msg = nullptr) const;
    bool upsertPrediction(const PredictionRecord &prediction, QString *err_msg = nullptr) const;

private:
    bool ensureSchema(QString *err_msg = nullptr) const;
};

} // namespace dltool::database

