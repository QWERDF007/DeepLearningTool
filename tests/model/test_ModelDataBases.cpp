#include "../test_runner.h"

#include "database/ModelDataBase.h"
#include "database/ModelTaskDataBase.h"

#include <QTemporaryDir>
#include <QTest>

using namespace dltool::database;

class ModelDataBasesTest : public QObject
{
    Q_OBJECT

private slots:
    void modelDatabaseRoundTripsParametersSelectionsAndTaskIndex()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        ModelDataBase database(QDir(temp.path()).filePath(QStringLiteral("model.db")));
        QString error;
        const QVariantMap params{{QStringLiteral("train"),
                                  QVariantMap{{QStringLiteral("epochs"), 3}, {QStringLiteral("lr"), 0.1}}}};
        QVERIFY2(database.replaceTrainParams(params, &error), qPrintable(error));
        QVariantMap loaded_params;
        QVERIFY2(database.readTrainParams(loaded_params, &error), qPrintable(error));
        QCOMPARE(loaded_params, params);

        QVERIFY(database.replaceDatasets({{QStringLiteral("train"), 4, {1, 2}}}, &error));
        QList<DatasetSelectionRecord> selections;
        QVERIFY(database.readDatasets(selections, &error));
        QCOMPARE(selections.size(), 1);
        QCOMPARE(selections.first().class_ids, QList<qint64>({1, 2}));
        QVERIFY(database.upsertTestTask({QStringLiteral("uuid"), QStringLiteral("Task"), 10, 11}, &error));
        QList<ModelTestTaskRecord> tasks;
        QVERIFY(database.listTestTasks(tasks, &error));
        QCOMPARE(tasks.size(), 1);
        QVERIFY(database.removeTestTask(QStringLiteral("uuid"), &error));
        QVERIFY(database.listTestTasks(tasks, &error));
        QVERIFY(tasks.isEmpty());
    }

    void taskDatabaseRoundTripsInfoParamsSelectionsAndPredictions()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        ModelTaskDataBase database(QDir(temp.path()).filePath(QStringLiteral("task.db")));
        QString error;
        QVERIFY(database.upsertTaskInfo({QStringLiteral("task"), 1, 2}, &error));
        TaskInfoRecord info;
        QVERIFY(database.readTaskInfo(info, &error));
        QCOMPARE(info.task_id, QStringLiteral("task"));
        QVERIFY(database.replaceTestParams({{QStringLiteral("inference"),
                                             QVariantMap{{QStringLiteral("threshold"), 0.5}}}},
                                           &error));
        QVariantMap params;
        QVERIFY(database.readTestParams(params, &error));
        QCOMPARE(params.value(QStringLiteral("inference")).toMap().value(QStringLiteral("threshold")).toDouble(), 0.5);
        QVERIFY(database.replaceDatasets({{QStringLiteral("test"), 8, {4}}}, &error));
        QVERIFY(database.upsertPrediction({10, QVariantMap{{QStringLiteral("score"), 0.8}}}, &error));
        QHash<qint64, QVariant> predictions;
        QVERIFY(database.readPredictions(predictions, &error));
        QCOMPARE(predictions.value(10).toMap().value(QStringLiteral("score")).toDouble(), 0.8);
        QVERIFY(database.clearPredictions(&error));
        predictions.clear();
        QVERIFY(database.readPredictions(predictions, &error));
        QVERIFY(predictions.isEmpty());
    }
};

REGISTER_TEST(ModelDataBasesTest)

#include "test_ModelDataBases.moc"
