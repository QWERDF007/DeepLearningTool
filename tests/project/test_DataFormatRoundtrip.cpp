#include "PersistentProjectFixture.h"

#include "data/DataFormat.h"
#include "data/DataManager.h"

#include <QDir>
#include <QTest>

using namespace dltool::model::integration;

class DataFormatRoundtripIntegrationTest final : public QObject
{
    Q_OBJECT

private slots:
    void importsExportedMaskLabelMeAndCoco()
    {
        PersistentProjectFixture fixture;
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        QString error;
        const qint64 source_dataset = fixture.dataManager()->getDatasetId(PersistentProjectFixture::datasetName());
        QVERIFY2(source_dataset >= 0,
                 qPrintable(QStringLiteral("数据集不存在，请先运行 data-creation: %1")
                                .arg(PersistentProjectFixture::datasetName())));

        int source_images = 0;
        int source_labels = 0;
        QVERIFY2(fixture.datasetCounts(source_dataset, &source_images, &source_labels, &error), qPrintable(error));
        QCOMPARE(source_images, 14);
        QCOMPARE(source_labels, 10);

        const qint64 mask_dataset
            = fixture.ensureDataset(PersistentProjectFixture::maskRoundtripDatasetName(), &error);
        QVERIFY2(mask_dataset >= 0, qPrintable(error));
        int mask_images = 0;
        int mask_labels = 0;
        QVERIFY2(fixture.datasetCounts(mask_dataset, &mask_images, &mask_labels, &error), qPrintable(error));
        if (mask_images == 0)
        {
            const QString export_root
                = QDir(PersistentProjectFixture::maskExportRoot()).filePath(PersistentProjectFixture::datasetName());
            QVERIFY2(fixture.importData(mask_dataset, dltool::data::DataFormat::Mask,
                                       QDir(export_root).filePath("images"), QDir(export_root).filePath("masks"), {},
                                       &error),
                     qPrintable(error));
        }
        QVERIFY2(fixture.datasetCounts(mask_dataset, &mask_images, &mask_labels, &error), qPrintable(error));
        QCOMPARE(mask_images, 9);
        QCOMPARE(mask_labels, 10);

        const qint64 labelme_dataset
            = fixture.ensureDataset(PersistentProjectFixture::labelMeRoundtripDatasetName(), &error);
        QVERIFY2(labelme_dataset >= 0, qPrintable(error));
        int labelme_images = 0;
        int labelme_labels = 0;
        QVERIFY2(fixture.datasetCounts(labelme_dataset, &labelme_images, &labelme_labels, &error), qPrintable(error));
        if (labelme_images == 0)
        {
            const QString export_root
                = QDir(PersistentProjectFixture::labelMeExportRoot()).filePath(PersistentProjectFixture::datasetName());
            QVERIFY2(fixture.importData(labelme_dataset, dltool::data::DataFormat::LabelMe,
                                       QDir(export_root).filePath("images"), QDir(export_root).filePath("annotations"),
                                       {}, &error),
                     qPrintable(error));
        }
        QVERIFY2(fixture.datasetCounts(labelme_dataset, &labelme_images, &labelme_labels, &error), qPrintable(error));
        QCOMPARE(labelme_images, 14);
        QCOMPARE(labelme_labels, 10);

        const qint64 coco_dataset
            = fixture.ensureDataset(PersistentProjectFixture::cocoRoundtripDatasetName(), &error);
        QVERIFY2(coco_dataset >= 0, qPrintable(error));
        int coco_images = 0;
        int coco_labels = 0;
        QVERIFY2(fixture.datasetCounts(coco_dataset, &coco_images, &coco_labels, &error), qPrintable(error));
        if (coco_images == 0)
        {
            const QString export_root
                = QDir(PersistentProjectFixture::cocoExportRoot()).filePath(PersistentProjectFixture::datasetName());
            QVERIFY2(fixture.importData(coco_dataset, dltool::data::DataFormat::COCO,
                                       QDir(export_root).filePath("images"), QDir(export_root).filePath("annotations"), {},
                                       &error),
                     qPrintable(error));
        }
        QVERIFY2(fixture.datasetCounts(coco_dataset, &coco_images, &coco_labels, &error), qPrintable(error));
        QCOMPARE(coco_images, 14);
        QCOMPARE(coco_labels, 10);
    }
};

QTEST_GUILESS_MAIN(DataFormatRoundtripIntegrationTest)

#include "test_DataFormatRoundtrip.moc"
