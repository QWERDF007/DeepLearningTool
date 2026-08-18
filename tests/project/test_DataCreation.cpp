#include "PersistentProjectFixture.h"

#include "data/DataManager.h"

#include <QTest>

using namespace dltool::model::integration;

class DataCreationIntegrationTest final : public QObject
{
    Q_OBJECT

private slots:
    void createsNamedEmptyDataset()
    {
        PersistentProjectFixture fixture;
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        QVERIFY(fixture.dataManager() != nullptr);

        const QString dataset_name = PersistentProjectFixture::datasetName();
        const int     existing_id  = fixture.dataManager()->getDatasetId(dataset_name);

        QString error;
        const qint64 dataset_id = fixture.ensureDataset(dataset_name, &error);
        QVERIFY2(dataset_id >= 0, qPrintable(error));
        QCOMPARE(fixture.dataManager()->getDatasetId(dataset_name), static_cast<int>(dataset_id));
        if (existing_id >= 0)
            QCOMPARE(static_cast<int>(dataset_id), existing_id);

        int image_count = 0;
        int label_count = 0;
        QVERIFY2(fixture.datasetCounts(dataset_id, &image_count, &label_count, &error), qPrintable(error));
        if (existing_id < 0)
        {
            QCOMPARE(image_count, 0);
            QCOMPARE(label_count, 0);
        }
        else
        {
            QVERIFY(image_count >= 0);
            QVERIFY(label_count >= 0);
        }
    }
};

QTEST_GUILESS_MAIN(DataCreationIntegrationTest)

#include "test_DataCreation.moc"
