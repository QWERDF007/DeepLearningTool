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
        QCOMPARE(label_count, 11);

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

    void batchExportCancellationStopsSubsequentDatasetsAndPreservesTarget()
    {
        PersistentProjectFixture fixture;
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        const qint64 dataset_id_1 = fixture.dataManager()->getDatasetId(PersistentProjectFixture::datasetName());
        QVERIFY2(dataset_id_1 >= 0, qPrintable(fixture.error()));

        QString error;
        const QString second_name = QStringLiteral("batch_cancel_second_ds");
        const qint64 dataset_id_2 = fixture.ensureDataset(second_name, &error);
        QVERIFY2(dataset_id_2 >= 0, qPrintable(error));

        // Import images into dataset 2 if empty
        int img_count_2 = 0;
        int lbl_count_2 = 0;
        QVERIFY(fixture.datasetCounts(dataset_id_2, &img_count_2, &lbl_count_2, &error));
        if (img_count_2 == 0)
        {
            QVERIFY2(fixture.importData(dataset_id_2, dltool::data::DataFormat::Folder,
                                        PersistentProjectFixture::imageRoot(), {}, {}, &error),
                     qPrintable(error));
        }

        QTemporaryDir temp_export_dir;
        QVERIFY(temp_export_dir.isValid());

        // Pre-create dataset 2's target directory with a stale sentinel file
        const QString ds2_target_dir = QDir(temp_export_dir.path()).filePath(second_name);
        QVERIFY(QDir().mkpath(ds2_target_dir));
        const QString sentinel_file = QDir(ds2_target_dir).filePath(QStringLiteral("stale_sentinel.txt"));
        {
            QFile file(sentinel_file);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
            file.write("pre-existing old artifact");
            file.close();
        }

        QSignalSpy warn_notifications(dltool::ui::SignalHelper::getInstance(),
                                      &dltool::ui::SignalHelper::warn);
        QSignalSpy success_notifications(dltool::ui::SignalHelper::getInstance(),
                                         &dltool::ui::SignalHelper::success);

        // Start batch export of {dataset_id_1, dataset_id_2}
        fixture.dataManager()->exportDatasets({dataset_id_1, dataset_id_2}, dltool::data::DataFormat::Mask,
                                              temp_export_dir.path());

        // Immediately cancel
        fixture.dataManager()->cancelDataOperation();

        // Wait until data operation finishes
        QTRY_VERIFY_WITH_TIMEOUT(!fixture.dataManager()->dataOperationRunning(), 15000);

        // 1. Success notification must NOT have been emitted
        QCOMPARE(success_notifications.size(), 0);

        // 2. Warn notification for cancellation should be emitted
        QVERIFY(!warn_notifications.isEmpty());
        QVERIFY(warn_notifications.constLast().at(1).toString().contains(QStringLiteral("取消")));

        // 3. Dataset 2's pre-existing sentinel file must remain 100% intact!
        QVERIFY(QFile::exists(sentinel_file));
        QFile check_file(sentinel_file);
        QVERIFY(check_file.open(QIODevice::ReadOnly | QIODevice::Text));
        QCOMPARE(check_file.readAll(), QByteArray("pre-existing old artifact"));
        check_file.close();

        // 4. Dataset 2 was never executed, so no images or new files were published into ds2_target_dir
        const QStringList entries = QDir(ds2_target_dir).entryList(QDir::Files | QDir::NoDotAndDotDot);
        QCOMPARE(entries, QStringList({QStringLiteral("stale_sentinel.txt")}));
    }
};

QTEST_GUILESS_MAIN(DataExportIntegrationTest)

#include "test_DataExport.moc"
