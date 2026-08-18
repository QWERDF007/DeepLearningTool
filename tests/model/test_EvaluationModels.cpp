#include "../test_runner.h"

#include "model/ModelEvaluationModels.h"

#include <QTest>

using namespace dltool::model;

class EvaluationModelsTest : public QObject
{
    Q_OBJECT

private slots:
    void metricModelExposesRolesAndUndefinedText()
    {
        EvaluationMetricModel model;
        EvaluationMetricRecord record;
        record.key   = QStringLiteral("cat");
        record.label = QStringLiteral("Cat");
        record.class_id = 3;
        record.tp = 2;
        record.fp = 1;
        record.fn = 2;
        record.precision = 2.0 / 3.0;
        record.recall = 0.5;
        record.precision_defined = true;
        record.recall_defined = true;
        model.setRecords({record});

        QCOMPARE(model.rowCount(), 1);
        const QModelIndex index = model.index(0, 0);
        QCOMPARE(index.data(EvaluationMetricModel::KeyRole).toString(), QStringLiteral("cat"));
        QCOMPARE(index.data(EvaluationMetricModel::PredictedCountRole).toLongLong(), qint64(3));
        QCOMPARE(index.data(EvaluationMetricModel::LabeledCountRole).toLongLong(), qint64(4));
        QCOMPARE(index.data(EvaluationMetricModel::PrecisionTextRole).toString(), QStringLiteral("0.667"));
        QCOMPARE(index.data(EvaluationMetricModel::ApTextRole).toString(), QStringLiteral("—"));
        QVERIFY(model.roleNames().value(EvaluationMetricModel::ClassIdRole) == "classId");
    }

    void confusionModelKeepsRectangularLayoutAndRoles()
    {
        EvaluationConfusionModel model;
        EvaluationConfusionCell first;
        first.row_key = QStringLiteral("1");
        first.column_key = QStringLiteral("1");
        first.row_label = QStringLiteral("Cat");
        first.column_label = QStringLiteral("Cat");
        first.count = 4;
        first.cell_kind = evaluation::CellKind::Match;
        first.diagonal = true;

        EvaluationConfusionCell second = first;
        second.column_key = QStringLiteral("FP");
        second.column_label = QStringLiteral("FP");
        second.count = 1;
        second.cell_kind = evaluation::CellKind::FalsePositive;

        EvaluationConfusionCell third = first;
        third.row_key = QStringLiteral("FN");
        third.row_label = QStringLiteral("FN");
        third.count = 2;
        third.cell_kind = evaluation::CellKind::FalseNegative;

        EvaluationConfusionCell fourth = first;
        fourth.row_key = QStringLiteral("FN");
        fourth.column_key = QStringLiteral("FP");
        fourth.count = 3;
        fourth.cell_kind = evaluation::CellKind::NotApplicable;

        model.setRecords({first, second, third, fourth});
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.columnCount(), 2);
        QCOMPARE(model.headerData(0, Qt::Horizontal).toString(), QStringLiteral("Cat"));
        QCOMPARE(model.headerData(1, Qt::Horizontal).toString(), QStringLiteral("FP"));
        QCOMPARE(model.index(1, 0).data(EvaluationConfusionModel::RowKeyRole).toString(), QStringLiteral("FN"));
        QCOMPARE(model.index(0, 1).data(EvaluationConfusionModel::CountRole).toLongLong(), qint64(1));
        QCOMPARE(model.index(0, 1).data(EvaluationConfusionModel::CellKindValueRole).toInt(),
                 static_cast<int>(evaluation::CellKind::FalsePositive));
    }

    void instanceAndImageModelsRebuildDerivedValues()
    {
        EvaluationImageRecord image;
        image.id = 10;
        image.dataset_id = 20;
        image.name = QStringLiteral("sample.png");
        image.width = 32;
        image.height = 24;
        image.gt.push_back(EvaluationGroundTruthData{7, 3, QStringLiteral("Cat"), {}, {}, {}, false});
        image.predictions.push_back(EvaluationPredictionData{QStringLiteral("p"), 10, 3,
                                                               QStringLiteral("Cat"), 0.8, {}, {}, {}});

        EvaluationImageModel image_model;
        image_model.setRecords({image});
        QCOMPARE(image_model.rowCount(), 1);
        const QModelIndex image_index = image_model.index(0, 0);
        QCOMPARE(image_index.data(EvaluationImageModel::GtLabelIdsRole).toList().at(0).toLongLong(), qint64(7));
        QCOMPARE(image_index.data(EvaluationImageModel::GtClassIdsRole).toList().at(0).toInt(), 3);
        QCOMPARE(image_index.data(EvaluationImageModel::PredClassIdsRole).toList().at(0).toInt(), 3);
        QCOMPARE(image_index.data(EvaluationImageModel::ScoreRole).toDouble(), 0.8);
        QVERIFY(image_index.data(EvaluationImageModel::HasGtRole).toBool());
        QVERIFY(image_index.data(EvaluationImageModel::HasPredRole).toBool());

        EvaluationInstanceRecord event;
        event.event_uuid = QStringLiteral("event-1");
        event.image_id = 10;
        event.dataset_id = 20;
        event.status = evaluation::Status::FalsePositive;
        event.score = 0.8;
        event.pred_class_id = 3;
        EvaluationInstanceModel instance_model;
        instance_model.setRecords({event});
        instance_model.setSelectedEvent(QStringLiteral("event-1"));
        const QModelIndex instance_index = instance_model.index(0, 0);
        QCOMPARE(instance_index.data(EvaluationInstanceModel::StatusRole).toString(),
                 evaluation::statusKey(evaluation::Status::FalsePositive));
        QVERIFY(instance_index.data(EvaluationInstanceModel::SelectedRole).toBool());
        instance_model.setSelectedEvent({});
        QVERIFY(!instance_model.index(0, 0).data(EvaluationInstanceModel::SelectedRole).toBool());
    }

    void proxiesFilterByGlobalAndCellContracts()
    {
        EvaluationInstanceRecord image_a;
        image_a.event_uuid = QStringLiteral("image-a");
        image_a.image_id = 1;
        image_a.dataset_id = 10;
        image_a.gt_class_id = 3;
        EvaluationInstanceRecord image_b;
        image_b.event_uuid = QStringLiteral("image-b");
        image_b.image_id = 2;
        image_b.dataset_id = 20;
        image_b.gt_class_id = 4;

        EvaluationInstanceModel images;
        images.setRecords({image_a, image_b});
        EvaluationGlobalFilterProxyModel global;
        global.setSourceModel(&images);
        global.setDatasetIds({10});
        QCOMPARE(global.rowCount(), 1);
        QCOMPARE(global.index(0, 0).data(EvaluationInstanceModel::ImageIdRole).toLongLong(), qint64(1));
        global.setDatasetIds({});
        global.setClassIds({4});
        QCOMPARE(global.rowCount(), 1);
        QCOMPARE(global.index(0, 0).data(EvaluationInstanceModel::ImageIdRole).toLongLong(), qint64(2));

        EvaluationInstanceRecord tp;
        tp.event_uuid = QStringLiteral("tp");
        tp.status = evaluation::Status::TruePositive;
        tp.gt_class_id = 3;
        tp.pred_class_id = 3;
        tp.score = 0.9;
        EvaluationInstanceRecord fp;
        fp.event_uuid = QStringLiteral("fp");
        fp.status = evaluation::Status::FalsePositive;
        fp.pred_class_id = 3;
        fp.score = 0.4;
        EvaluationInstanceModel instances;
        instances.setRecords({tp, fp});
        EvaluationCellFilterProxyModel cell;
        cell.setSourceModel(&instances);
        cell.setStatus(evaluation::statusKey(evaluation::Status::FalsePositive));
        QCOMPARE(cell.rowCount(), 1);
        QCOMPARE(cell.index(0, 0).data(EvaluationInstanceModel::EventUuidRole).toString(), QStringLiteral("fp"));
        cell.setStatus({});
        cell.setMatrixRow(QStringLiteral("3"));
        cell.setMatrixColumn(QStringLiteral("3"));
        QCOMPARE(cell.rowCount(), 1);
        QCOMPARE(cell.index(0, 0).data(EvaluationInstanceModel::EventUuidRole).toString(), QStringLiteral("tp"));
        cell.setMatrixRow({});
        cell.setMatrixColumn({});
        cell.setMinScore(0.5);
        QCOMPARE(cell.rowCount(), 1);
        QCOMPARE(cell.index(0, 0).data(EvaluationInstanceModel::EventUuidRole).toString(), QStringLiteral("tp"));
    }

    void chartModelExposesStructuredDescriptor()
    {
        EvaluationChartModel model;
        const QVariantMap descriptor{{QStringLiteral("kind"), QStringLiteral("line")},
                                     {QStringLiteral("chart_id"), QStringLiteral("precision_recall")},
                                     {QStringLiteral("data"), QVariantMap{{QStringLiteral("labels"), QVariantList{1, 2}}}}};
        model.setRecords({descriptor});
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.index(0, 0).data(EvaluationChartModel::KindRole).toString(), QStringLiteral("line"));
        QCOMPARE(model.descriptor(0).value(QStringLiteral("chart_id")).toString(), QStringLiteral("precision_recall"));
        QVERIFY(model.descriptor(-1).isEmpty());
    }
};

REGISTER_TEST(EvaluationModelsTest)

#include "test_EvaluationModels.moc"
