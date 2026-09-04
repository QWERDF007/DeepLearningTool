#include "PersistentProjectFixture.h"

#include "data/DataFormat.h"
#include "data/DataManager.h"
#include "ui/SignalHelper.h"

#include <QSignalSpy>
#include <QTest>

using namespace dltool::model::integration;

class DataExportIntegrationTest final : public QObject
{
    Q_OBJECT

private slots:
    void exportsMaskLabelMeAndCoco()
    {
        PersistentProjectFixture fixture;
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        QString error;
        const qint64 dataset_id = fixture.dataManager()->getDatasetId(PersistentProjectFixture::datasetName());
        QVERIFY2(dataset_id >= 0,
                 qPrintable(QStringLiteral("数据集不存在，请先运行 data-creation: %1")
                                .arg(PersistentProjectFixture::datasetName())));

        int image_count = 0;
        int label_count = 0;
        QVERIFY2(fixture.datasetCounts(dataset_id, &image_count, &label_count, &error), qPrintable(error));
        QCOMPARE(image_count, 14);
        QCOMPARE(label_count, 10);

        QSignalSpy export_notifications(dltool::ui::SignalHelper::getInstance(),
                                        &dltool::ui::SignalHelper::success);

        QVERIFY2(fixture.exportData(dataset_id, dltool::data::DataFormat::Mask,
                                    PersistentProjectFixture::maskExportRoot(), image_count, &error),
                 qPrintable(error));
        QVERIFY(!export_notifications.isEmpty());
        QVERIFY(export_notifications.constLast().at(1).toString().contains(QStringLiteral("耗时")));

        QVERIFY2(fixture.exportData(dataset_id, dltool::data::DataFormat::LabelMe,
                                    PersistentProjectFixture::labelMeExportRoot(), image_count, &error),
                 qPrintable(error));
        QVERIFY(export_notifications.size() >= 2);
        QVERIFY(export_notifications.constLast().at(1).toString().contains(QStringLiteral("耗时")));

        QVERIFY2(fixture.exportData(dataset_id, dltool::data::DataFormat::COCO,
                                    PersistentProjectFixture::cocoExportRoot(), image_count, &error),
                 qPrintable(error));
        QVERIFY(export_notifications.size() >= 3);
        QVERIFY(export_notifications.constLast().at(1).toString().contains(QStringLiteral("耗时")));
    }

    void rejectsRelativeOrInvalidExportPath()
    {
        PersistentProjectFixture fixture;
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        const qint64 dataset_id = fixture.dataManager()->getDatasetId(PersistentProjectFixture::datasetName());
        QVERIFY2(dataset_id >= 0, qPrintable(fixture.error()));

        QSignalSpy error_notifications(dltool::ui::SignalHelper::getInstance(),
                                       &dltool::ui::SignalHelper::error);

        fixture.dataManager()->exportDatasets({dataset_id}, dltool::data::DataFormat::Mask,
                                              QStringLiteral("relative/path/export"));
        QCOMPARE(error_notifications.size(), 1);
        QVERIFY(error_notifications.constLast().at(1).toString().contains(QStringLiteral("绝对路径")));

        fixture.dataManager()->exportDatasets({dataset_id}, dltool::data::DataFormat::Mask, QString());
        QCOMPARE(error_notifications.size(), 2);
    }
};

QTEST_GUILESS_MAIN(DataExportIntegrationTest)

#include "test_DataExport.moc"
