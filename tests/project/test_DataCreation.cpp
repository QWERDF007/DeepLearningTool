#include "PersistentProjectFixture.h"

#include "data/DataManager.h"
#include "ui/ProgressManager.h"

#include <QDateTime>
#include <QSignalSpy>
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

    void progressManagerNotTriggeredForLightweightOperations()
    {
        PersistentProjectFixture fixture;
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        auto *data_manager = fixture.dataManager();
        QVERIFY(data_manager != nullptr);

        auto *progress = dltool::ui::ProgressManager::getInstance();
        progress->reset();

        QSignalSpy start_spy(progress, &dltool::ui::ProgressManager::runningStateChanged);
        QSignalSpy progress_spy(progress, &dltool::ui::ProgressManager::progressChanged);

        // 1. addDataset should not trigger ProgressManager
        const QString test_dataset_name = QStringLiteral("ProgressTest_%1").arg(QDateTime::currentMSecsSinceEpoch());
        data_manager->addDataset(test_dataset_name);
        QTRY_VERIFY_WITH_TIMEOUT(!data_manager->dataOperationRunning(), 5000);

        QCOMPARE(start_spy.count(), 0);
        QCOMPARE(progress_spy.count(), 0);
        QVERIFY(!progress->getIsRunning());

        const int dataset_id = data_manager->getDatasetId(test_dataset_name);
        QVERIFY(dataset_id >= 0);

        // 2. updateDataset should not trigger ProgressManager
        const QString renamed_dataset_name = QStringLiteral("Renamed_%1").arg(test_dataset_name);
        data_manager->updateDataset(dataset_id, renamed_dataset_name);
        QTRY_VERIFY_WITH_TIMEOUT(!data_manager->dataOperationRunning(), 5000);

        QCOMPARE(start_spy.count(), 0);
        QCOMPARE(progress_spy.count(), 0);
        QVERIFY(!progress->getIsRunning());

        // 3. importData with invalid/unsupported format should not trigger ProgressManager
        data_manager->importData(dataset_id, 99999, {}, {});
        QCoreApplication::processEvents();

        QCOMPARE(start_spy.count(), 0);
        QCOMPARE(progress_spy.count(), 0);
        QVERIFY(!progress->getIsRunning());

        // 4. copyToDatasetAsync (regardless of count) should not trigger ProgressManager
        // and tests concurrency rejection of importData while an operation is running
        std::vector<int64_t> batch_ids(60, 999999);
        data_manager->copyToDatasetAsync(batch_ids, dataset_id, nullptr, {});
        if (data_manager->dataOperationRunning())
        {
            // Trigger import while busy: should be rejected immediately without starting ProgressManager task
            data_manager->importData(dataset_id, 0, {}, {});
        }
        QTRY_VERIFY_WITH_TIMEOUT(!data_manager->dataOperationRunning(), 5000);

        QCOMPARE(start_spy.count(), 0);
        QCOMPARE(progress_spy.count(), 0);
        QVERIFY(!progress->getIsRunning());

        // 5. moveToDatasetAsync (regardless of count) should not trigger ProgressManager
        data_manager->moveToDatasetAsync(batch_ids, dataset_id, nullptr, {});
        QTRY_VERIFY_WITH_TIMEOUT(!data_manager->dataOperationRunning(), 5000);

        QCOMPARE(start_spy.count(), 0);
        QCOMPARE(progress_spy.count(), 0);
        QVERIFY(!progress->getIsRunning());

        // 6. deleteSelectedImages should not trigger ProgressManager
        data_manager->deleteSelectedImages();
        QTRY_VERIFY_WITH_TIMEOUT(!data_manager->dataOperationRunning(), 5000);

        QCOMPARE(start_spy.count(), 0);
        QCOMPARE(progress_spy.count(), 0);
        QVERIFY(!progress->getIsRunning());
    }
};

QTEST_GUILESS_MAIN(DataCreationIntegrationTest)

#include "test_DataCreation.moc"
