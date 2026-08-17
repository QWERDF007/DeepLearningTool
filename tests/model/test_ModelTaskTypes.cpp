#include "../test_runner.h"

#include "model/ModelTaskTypes.h"

#include <QTest>

using namespace dltool::model;

class ModelTaskTypesTest : public QObject
{
    Q_OBJECT

private slots:
    void taskDescriptors()
    {
        const ModelTaskDescriptor train = describeModelTask(ModelTaskType::Train);
        QCOMPARE(train.key, QStringLiteral("train"));
        QCOMPARE(train.display_name, QStringLiteral("训练"));
        QVERIFY(train.requires_dataset_export);

        const ModelTaskDescriptor test = describeModelTask(ModelTaskType::Test);
        QCOMPARE(test.key, QStringLiteral("test"));
        QVERIFY(test.requires_dataset_export);

        const ModelTaskDescriptor box = describeModelTask(ModelTaskType::BoxToMask);
        QCOMPARE(box.key, QStringLiteral("box_to_mask"));
        QVERIFY(box.requires_dataset_export);
    }

    void unknownTaskFallback()
    {
        const ModelTaskDescriptor unknown = describeModelTask(ModelTaskType::Unknown);
        QCOMPARE(unknown.key, QStringLiteral("unknown"));
        QCOMPARE(unknown.log_stem, QStringLiteral("task"));
        QVERIFY(!unknown.requires_dataset_export);
    }

    void taskPredicates()
    {
        QVERIFY(isKnownModelTask(ModelTaskType::Train));
        QVERIFY(isKnownModelTask(ModelTaskType::Test));
        QVERIFY(isKnownModelTask(ModelTaskType::BoxToMask));
        QVERIFY(!isKnownModelTask(ModelTaskType::Unknown));
        QVERIFY(isTrainModelTask(ModelTaskType::Train));
        QVERIFY(!isTrainModelTask(ModelTaskType::Test));
        QVERIFY(isTestModelTask(ModelTaskType::Test));
        QVERIFY(!isTestModelTask(ModelTaskType::Train));
    }
};

REGISTER_TEST(ModelTaskTypesTest)

#include "test_ModelTaskTypes.moc"
