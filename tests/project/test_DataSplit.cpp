#include "PersistentProjectFixture.h"

#include "data/DataManager.h"
#include "database/DataBase.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QSet>
#include <QTest>
#include <QTimer>

#include <vector>

using namespace dltool::model::integration;

namespace {

QString nextDatasetName(const dltool::data::DataManager *data_manager, const QString &base_name)
{
    QString candidate = base_name;
    int     index     = 1;
    while (data_manager != nullptr && data_manager->getDatasetId(candidate) >= 0)
    {
        candidate = QStringLiteral("%1(%2)").arg(base_name).arg(index++);
    }
    return candidate;
}

bool imageIdsForDataset(const QString &database_path, const qint64 dataset_id, QSet<qint64> *image_ids,
                        int *label_count, QString *error)
{
    dltool::database::ProjectDataBase database(database_path);
    std::vector<int64_t>              ids;
    std::vector<QString>               paths;
    QString                            database_error;
    if (!database.getImages(dataset_id, ids, paths, database_error))
    {
        if (error != nullptr)
            *error = QStringLiteral("读取子数据集图像失败: %1").arg(database_error);
        return false;
    }

    QSet<qint64> result;
    for (const qint64 image_id : ids)
        result.insert(image_id);

    std::vector<int64_t>             label_ids;
    std::vector<int64_t>             label_image_ids;
    std::vector<int64_t>             label_class_ids;
    std::vector<int64_t>             label_types;
    std::vector<std::vector<uint8_t>> label_data;
    if (!database.getAllLabels(label_ids, label_image_ids, label_class_ids, label_types, label_data, database_error))
    {
        if (error != nullptr)
            *error = QStringLiteral("读取子数据集标注失败: %1").arg(database_error);
        return false;
    }

    int labels = 0;
    for (const qint64 image_id : label_image_ids)
    {
        if (result.contains(image_id))
            ++labels;
    }
    if (image_ids != nullptr)
        *image_ids = result;
    if (label_count != nullptr)
        *label_count = labels;
    return true;
}

} // namespace

class DataSplitIntegrationTest final : public QObject
{
    Q_OBJECT

private slots:
    void createsStratifiedDatasetCopies()
    {
        PersistentProjectFixture fixture;
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        QVERIFY(fixture.dataManager() != nullptr);

        const qint64 source_dataset_id
            = fixture.dataManager()->getDatasetId(PersistentProjectFixture::datasetName());
        QVERIFY2(source_dataset_id >= 0,
                 qPrintable(QStringLiteral("源数据集不存在，请先运行 data-import: %1")
                                .arg(PersistentProjectFixture::datasetName())));

        QString error;
        int     source_image_count = 0;
        int     source_label_count = 0;
        QVERIFY2(fixture.datasetCounts(source_dataset_id, &source_image_count, &source_label_count, &error),
                 qPrintable(error));
        QCOMPARE(source_image_count, 14);
        QCOMPARE(source_label_count, 10);

        const QString source_name = PersistentProjectFixture::datasetName();
        const QString train_name  = nextDatasetName(fixture.dataManager(), source_name + QStringLiteral("-Train"));
        const QString val_name    = nextDatasetName(fixture.dataManager(), source_name + QStringLiteral("-Val"));
        const QString test_name   = nextDatasetName(fixture.dataManager(), source_name + QStringLiteral("-Test"));

        bool        finished = false;
        bool        success  = false;
        QString     message;
        QEventLoop  loop;
        QTimer      timeout;
        timeout.setSingleShot(true);
        QObject::connect(fixture.dataManager(), &dltool::data::DataManager::datasetSplitFinished, &loop,
                         [&](const bool operation_success, const QString &operation_message)
                         {
                             finished = true;
                             success  = operation_success;
                             message  = operation_message;
                             loop.quit();
                         });
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

        timeout.start(120000);
        fixture.dataManager()->splitDataset(source_dataset_id, 0.6, 0.2, 0.2, true);
        if (!finished)
            loop.exec();
        QVERIFY2(finished, "等待数据集划分完成超时");
        QVERIFY2(success, qPrintable(message));

        const qint64 train_id = fixture.dataManager()->getDatasetId(train_name);
        const qint64 val_id   = fixture.dataManager()->getDatasetId(val_name);
        const qint64 test_id  = fixture.dataManager()->getDatasetId(test_name);
        QVERIFY(train_id >= 0);
        QVERIFY(val_id >= 0);
        QVERIFY(test_id >= 0);

        QSet<qint64> source_images;
        QSet<qint64> train_images;
        QSet<qint64> val_images;
        QSet<qint64> test_images;
        int          train_labels = 0;
        int          val_labels   = 0;
        int          test_labels  = 0;
        QVERIFY2(imageIdsForDataset(PersistentProjectFixture::projectDatabasePath(), source_dataset_id,
                                    &source_images, nullptr, &error),
                 qPrintable(error));
        QVERIFY2(imageIdsForDataset(PersistentProjectFixture::projectDatabasePath(), train_id, &train_images,
                                    &train_labels, &error),
                 qPrintable(error));
        QVERIFY2(imageIdsForDataset(PersistentProjectFixture::projectDatabasePath(), val_id, &val_images,
                                    &val_labels, &error),
                 qPrintable(error));
        QVERIFY2(imageIdsForDataset(PersistentProjectFixture::projectDatabasePath(), test_id, &test_images,
                                    &test_labels, &error),
                 qPrintable(error));

        QCOMPARE(train_images.size() + val_images.size() + test_images.size(), source_images.size());
        QCOMPARE(train_labels + val_labels + test_labels, source_label_count);
        for (const qint64 image_id : train_images)
        {
            QVERIFY(!val_images.contains(image_id));
            QVERIFY(!test_images.contains(image_id));
        }
        QSet<qint64> train_val = train_images;
        QSet<qint64> train_test = train_images;
        QSet<qint64> val_test = val_images;
        QVERIFY(train_val.intersect(val_images).isEmpty());
        QVERIFY(train_test.intersect(test_images).isEmpty());
        QVERIFY(val_test.intersect(test_images).isEmpty());
        QCOMPARE(source_images.size(), source_image_count);
    }
};

QTEST_GUILESS_MAIN(DataSplitIntegrationTest)

#include "test_DataSplit.moc"
