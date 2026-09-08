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
    bool replacePredictions(const QHash<qint64, QVariant> &predictions, QString *err_msg = nullptr) const;

    bool readAdaptiveThresholdApplied(bool &applied, QString *err_msg = nullptr) const;
    bool writeAdaptiveThresholdApplied(bool applied, QString *err_msg = nullptr) const;

    bool readExecutionState(QVariantMap &state, QString *err_msg = nullptr) const;
    bool writeExecutionState(const QVariantMap &state, QString *err_msg = nullptr) const;

    bool readPreprocessingConfig(QVariantMap &config, QString *err_msg = nullptr) const;
    bool writePreprocessingConfig(const QVariantMap &config, QString *err_msg = nullptr) const;

    /**
     * @brief 获取当前任务预测表（prediction 表）的轻量级指纹（行数、最大 ID、总数据长度）。
     *
     * 避免在 GUI 线程全量加载或反序列化所有预测记录。
     */
    bool getPredictionFingerprint(QString &fingerprint, QString *err_msg = nullptr) const;

private:
    bool ensureSchema(QString *err_msg = nullptr) const;
};

} // namespace dltool::database

