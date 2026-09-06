#include "data/DatasetExportSource.h"

#include <QTest>

#include <cstdint>
#include <vector>

class DatasetExportSourceTest final : public QObject
{
    Q_OBJECT

private slots:
    void snapshotExposesSortedImmutableExportData()
    {
        dltool::data::DatasetExportSnapshot::SnapshotData data;

        data.datasets.emplace(7, dltool::data::DatasetExportSnapshot::Dataset{7, QStringLiteral("dataset")});
        data.images.emplace(
            20,
            dltool::data::DatasetExportSnapshot::Image{
                20,
                7,
                QStringLiteral("images/20.png"),
                {{QStringLiteral("label_class_id"), 11}},
                {201, 200, 201}});
        data.images.emplace(10,
                           dltool::data::DatasetExportSnapshot::Image{
                               10, 7, QStringLiteral("images/10.png"), {}, {}});
        data.labels.emplace(200,
                            dltool::data::DatasetExportSnapshot::Label{
                                200, 11, {{QStringLiteral("x"), 4.0}}});
        data.labels.emplace(201,
                            dltool::data::DatasetExportSnapshot::Label{
                                201, 11, {{QStringLiteral("x"), 5.0}}});
        data.label_classes.emplace(11,
                                   dltool::data::DatasetExportSnapshot::LabelClass{
                                       11, QStringLiteral("scratch"), QStringLiteral("#ff0000"),
                                       QStringLiteral("anomaly")});

        const dltool::data::DatasetExportSnapshot snapshot(data);

        data.datasets.at(7).name = QStringLiteral("changed");
        data.images.at(20).path = QStringLiteral("changed.png");
        data.images.at(20).label_ids.push_back(999);
        data.labels.at(200).data.insert(QStringLiteral("x"), 8.0);
        data.label_classes.at(11).name = QStringLiteral("changed-class");

        QCOMPARE(snapshot.allImageIds(), std::vector<int64_t>({10, 20}));
        QCOMPARE(snapshot.imageDatasetId(20), qint64(7));
        QCOMPARE(snapshot.imagePath(20), QStringLiteral("images/20.png"));
        QCOMPARE(snapshot.imageLevelLabelData(20).value(QStringLiteral("label_class_id")).toLongLong(),
                 qint64(11));
        QCOMPARE(snapshot.imageLabelIds(20), std::vector<int64_t>({200, 201}));
        QCOMPARE(snapshot.labelClassId(200), qint64(11));
        QCOMPARE(snapshot.labelData(200).value(QStringLiteral("x")).toDouble(), 4.0);
        QCOMPARE(snapshot.labelClassName(11), QStringLiteral("scratch"));
        QCOMPARE(snapshot.labelClassColor(11), QStringLiteral("#ff0000"));
        QCOMPARE(snapshot.labelClassGroup(11), QStringLiteral("anomaly"));
        QCOMPARE(snapshot.datasetName(7), QStringLiteral("dataset"));
        QCOMPARE(snapshot.imagePath(999), QString());
    }
};

QTEST_GUILESS_MAIN(DatasetExportSourceTest)

#include "test_DatasetExportSource.moc"
