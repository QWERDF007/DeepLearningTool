#include "../test_runner.h"

#include "model/ModelDatasetSelection.h"

#include <QTest>

using namespace dltool::model;

class ModelDatasetSelectionTest : public QObject
{
    Q_OBJECT

private slots:
    void membershipAndSerializationAreDeterministic()
    {
        ModelDatasetSelections selections;
        selections.train.dataset_ids.insert(4);
        selections.validation.label_classes.insert({7, 12});
        selections.test.label_classes.insert({7, 3});

        QVERIFY(!selections.train.isEmpty());
        QVERIFY(selections.train.containsDataset(4));
        QVERIFY(selections.train.contains(4, -1));
        QVERIFY(selections.validation.containsLabelClass(7, 12));
        QVERIFY(!selections.validation.contains(7, 99));
        QVERIFY(!selections.test.containsLabelClass(-1, 3));

        QCOMPARE(selectedDatasetIds(selections), std::vector<int64_t>({4, 7}));
        const QVariantMap map = modelDatasetSelectionsMap(selections);
        QCOMPARE(map.value(QStringLiteral("train")).toMap().value(QStringLiteral("dataset_ids")).toList().size(), 1);
        QCOMPARE(map.value(QStringLiteral("validation")).toMap()
                     .value(QStringLiteral("label_classes"))
                     .toList()
                     .first()
                     .toMap()
                     .value(QStringLiteral("label_class_id"))
                     .toLongLong(),
                 qint64(12));
    }

    void databaseRowsRoundTripAndExpandWholeDataset()
    {
        ModelDatasetSelections selections;
        selections.train.dataset_ids.insert(8);
        selections.test.label_classes.insert({8, 4});
        selections.test.label_classes.insert({8, 2});

        const auto rows = databaseDatasetSelections(selections, [](qint64 dataset_id)
                                                     { return dataset_id == 8 ? QList<qint64>{3, 1, 3} : QList<qint64>{}; });
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).type, QStringLiteral("train"));
        QCOMPARE(rows.at(0).class_ids, QList<qint64>({1, 3}));
        QCOMPARE(rows.at(1).type, QStringLiteral("test"));
        QCOMPARE(rows.at(1).class_ids, QList<qint64>({2, 4}));

        const ModelDatasetSelections restored = modelDatasetSelectionsFromDatabase(rows);
        QVERIFY(restored.train.containsLabelClass(8, 1));
        QVERIFY(restored.train.containsLabelClass(8, 3));
        QVERIFY(restored.test.containsLabelClass(8, 2));
        QVERIFY(restored.test.containsLabelClass(8, 4));
        QVERIFY(!restored.validation.contains(8, 1));
    }
};

REGISTER_TEST(ModelDatasetSelectionTest)

#include "test_ModelDatasetSelection.moc"
