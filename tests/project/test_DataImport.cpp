#include "PersistentProjectFixture.h"

#include "data/DataFormat.h"
#include "data/DataManager.h"

#include <QDir>
#include <QTest>

using namespace dltool::model::integration;

class DataImportIntegrationTest final : public QObject
{
    Q_OBJECT

private slots:
    void importsFolderAndSeparateMaskFixtures()
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

        if (image_count < 14)
        {
            const QVariantMap groups = {
                {QStringLiteral("OK"), QStringLiteral("good")},
                {QStringLiteral("MT_Blowhole"), QStringLiteral("anomaly")},
                {QStringLiteral("MT_Crack"), QStringLiteral("anomaly")},
            };
            QVERIFY2(fixture.importData(dataset_id, dltool::data::DataFormat::Folder,
                                       PersistentProjectFixture::imageRoot(), {}, groups, &error),
                     qPrintable(error));
        }

        QVERIFY2(fixture.datasetCounts(dataset_id, &image_count, &label_count, &error), qPrintable(error));
        QCOMPARE(image_count, 14);

        if (label_count < 9)
        {
            QVERIFY2(fixture.importData(dataset_id, dltool::data::DataFormat::Mask,
                                       PersistentProjectFixture::imageRoot(), PersistentProjectFixture::maskRoot(), {},
                                       &error),
                     qPrintable(error));
        }

        QVERIFY2(fixture.datasetCounts(dataset_id, &image_count, &label_count, &error), qPrintable(error));
        QCOMPARE(image_count, 14);
        QVERIFY2(label_count >= 9, qPrintable(QStringLiteral("Mask 导入后标注数量不足: %1").arg(label_count)));
        QVERIFY(QDir(PersistentProjectFixture::imageRoot()).exists());
        QVERIFY(QDir(PersistentProjectFixture::maskRoot()).exists());
    }
};

QTEST_GUILESS_MAIN(DataImportIntegrationTest)

#include "test_DataImport.moc"
