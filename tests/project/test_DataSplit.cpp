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
        candidate = QStringLiteral("%1_%2").arg(base_name).arg(index++);
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
        QCOMPARE(source_label_count, 11);

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

    void copiesImagesWithLabelsAndTagsAtomically()
    {
        PersistentProjectFixture fixture;
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        QVERIFY(fixture.dataManager() != nullptr);

        const qint64 source_dataset_id
            = fixture.dataManager()->getDatasetId(PersistentProjectFixture::datasetName());
        QVERIFY(source_dataset_id >= 0);

        QString error;
        QSet<qint64> source_image_ids;
        int source_label_count = 0;
        QVERIFY2(imageIdsForDataset(PersistentProjectFixture::projectDatabasePath(), source_dataset_id,
                                    &source_image_ids, &source_label_count, &error),
                 qPrintable(error));
        QVERIFY(!source_image_ids.isEmpty());

        std::vector<int64_t> ids_to_copy;
        for (const qint64 id : source_image_ids)
        {
            ids_to_copy.push_back(id);
            if (ids_to_copy.size() == 3)
                break;
        }

        const QString target_name = nextDatasetName(fixture.dataManager(), QStringLiteral("Copy-Atomic-Target"));
        const qint64  target_id   = fixture.ensureDataset(target_name, &error);
        QVERIFY2(target_id >= 0, qPrintable(error));

        bool       finished = false;
        bool       success  = false;
        QString    message;
        QEventLoop loop;
        QTimer     timeout;
        timeout.setSingleShot(true);

        fixture.dataManager()->copyToDatasetAsync(
            ids_to_copy, target_id, nullptr,
            [&](const bool op_success, const QString &op_message)
            {
                finished = true;
                success  = op_success;
                message  = op_message;
                loop.quit();
            },
            false);

        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(30000);
        if (!finished)
            loop.exec();

        QVERIFY2(finished, "复制图像操作超时");
        QVERIFY2(success, qPrintable(message));

        QSet<qint64> target_images;
        int          target_labels = 0;
        QVERIFY2(imageIdsForDataset(PersistentProjectFixture::projectDatabasePath(), target_id,
                                    &target_images, &target_labels, &error),
                 qPrintable(error));
        QCOMPARE(target_images.size(), 3);

        // Reopening database directly confirms committed records
        {
            dltool::database::ProjectDataBase reopened_db(PersistentProjectFixture::projectDatabasePath());
            std::vector<int64_t> db_ids;
            std::vector<QString> db_paths;
            QString db_error;
            QVERIFY(reopened_db.getImages(target_id, db_ids, db_paths, db_error));
            QCOMPARE(db_ids.size(), static_cast<size_t>(3));
        }
    }

    void movesImagesAtomicallyBetweenDatasets()
    {
        PersistentProjectFixture fixture;
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        QVERIFY(fixture.dataManager() != nullptr);

        QString error;
        const QString source_name = nextDatasetName(fixture.dataManager(), QStringLiteral("Move-Source"));
        const qint64  source_id   = fixture.ensureDataset(source_name, &error);
        QVERIFY2(source_id >= 0, qPrintable(error));

        const QString target_name = nextDatasetName(fixture.dataManager(), QStringLiteral("Move-Target"));
        const qint64  target_id   = fixture.ensureDataset(target_name, &error);
        QVERIFY2(target_id >= 0, qPrintable(error));

        // Copy 2 images into source_id first
        const qint64 base_dataset_id = fixture.dataManager()->getDatasetId(PersistentProjectFixture::datasetName());
        QSet<qint64> base_images;
        QVERIFY(imageIdsForDataset(PersistentProjectFixture::projectDatabasePath(), base_dataset_id, &base_images, nullptr, &error));
        std::vector<int64_t> to_copy;
        for (const qint64 id : base_images)
        {
            to_copy.push_back(id);
            if (to_copy.size() == 2)
                break;
        }

        {
            bool finished = false;
            QEventLoop loop;
            fixture.dataManager()->copyToDatasetAsync(to_copy, source_id, nullptr,
                [&](bool, const QString &) { finished = true; loop.quit(); }, false);
            if (!finished)
                loop.exec();
        }

        QSet<qint64> source_images;
        QVERIFY(imageIdsForDataset(PersistentProjectFixture::projectDatabasePath(), source_id, &source_images, nullptr, &error));
        QCOMPARE(source_images.size(), 2);

        std::vector<int64_t> to_move;
        for (const qint64 id : source_images)
            to_move.push_back(id);

        // Move to target_id
        bool       move_finished = false;
        bool       move_success  = false;
        QString    move_message;
        QEventLoop move_loop;
        fixture.dataManager()->moveToDatasetAsync(
            to_move, target_id, nullptr,
            [&](const bool op_success, const QString &op_message)
            {
                move_finished = true;
                move_success  = op_success;
                move_message  = op_message;
                move_loop.quit();
            },
            false);
        if (!move_finished)
            move_loop.exec();

        QVERIFY2(move_finished, "移动图像操作超时");
        QVERIFY2(move_success, qPrintable(move_message));

        QSet<qint64> source_after;
        QSet<qint64> target_after;
        QVERIFY(imageIdsForDataset(PersistentProjectFixture::projectDatabasePath(), source_id, &source_after, nullptr, &error));
        QVERIFY(imageIdsForDataset(PersistentProjectFixture::projectDatabasePath(), target_id, &target_after, nullptr, &error));
        QCOMPARE(source_after.size(), 0);
        QCOMPARE(target_after.size(), 2);
    }

    void atomicRollbackLeavesNoLeftoverRecords()
    {
        dltool::database::ProjectDataBase db(PersistentProjectFixture::projectDatabasePath());

        // 1. copyImagesAtomic with early cancellation: 0 records inserted
        dltool::database::ProjectDataBase::ImageSnapshot snap;
        snap.path = QStringLiteral("fictional/test/path.png");
        snap.tag_ids = {1};
        dltool::database::ProjectDataBase::LabelSnapshot lbl;
        lbl.label_class_id = 1;
        lbl.label_type = 1;
        lbl.data = {1, 2, 3, 4};
        snap.labels.push_back(lbl);

        std::vector<int64_t> dataset_ids;
        std::vector<QString> dataset_names;
        QString err;
        QVERIFY(db.getAllDatasets(dataset_ids, dataset_names, err));
        QVERIFY(!dataset_ids.empty());
        const int64_t test_dataset_id = dataset_ids.front();

        int64_t count_before = db.getImagesCount(test_dataset_id);

        dltool::database::ProjectDataBase::AtomicCopyOutput copy_out;
        bool copy_ok = db.copyImagesAtomic(
            test_dataset_id, {snap}, copy_out, err,
            []() { return true; } /* cancel immediately */);
        QVERIFY(!copy_ok);
        QVERIFY(copy_out.image_ids.empty());
        QVERIFY(copy_out.label_ids.empty());
        QCOMPARE(db.getImagesCount(test_dataset_id), count_before);

        // 2. splitDatasetAtomic with early cancellation: 0 datasets or images created
        const QString phantom_dataset_name = QStringLiteral("Atomic-Rollback-Phantom-%1")
                                                 .arg(QDateTime::currentMSecsSinceEpoch());
        dltool::database::ProjectDataBase::DatasetSplitTarget target;
        target.name = phantom_dataset_name;
        target.images = {snap};

        dltool::database::ProjectDataBase::AtomicSplitOutput split_out;
        bool split_ok = db.splitDatasetAtomic(
            {target}, split_out, err,
            []() { return true; } /* cancel immediately */);
        QVERIFY(!split_ok);
        QVERIFY(split_out.dataset_ids.empty());
        QVERIFY(split_out.image_ids.empty());

        // Verify phantom dataset was rolled back completely
        std::vector<int64_t> after_ids;
        std::vector<QString> after_names;
        QVERIFY(db.getAllDatasets(after_ids, after_names, err));
        for (const QString &name : after_names)
        {
            QVERIFY2(name != phantom_dataset_name, "回滚失败：遗留了子数据集记录");
        }

        dltool::database::ProjectDataBase::ImageSnapshot valid_snapshot;
        valid_snapshot.path = QStringLiteral("transaction-probe.png");
        bool cancelled_after_insert = false;
        QVERIFY(!db.copyImagesAtomic(test_dataset_id, {valid_snapshot, valid_snapshot}, copy_out, err,
                                     [&]()
                                     {
                                         if (copy_out.image_ids.empty())
                                             return false;
                                         cancelled_after_insert = true;
                                         return true;
                                     }));
        QVERIFY(cancelled_after_insert);
        QVERIFY(copy_out.image_ids.empty());
        QVERIFY(copy_out.label_ids.empty());
        QCOMPARE(db.getImagesCount(test_dataset_id), count_before);
        target.images = {valid_snapshot};
        dltool::database::ProjectDataBase::DatasetSplitTarget invalid_target;
        invalid_target.name = QString();
        bool observed_inserted_image = false;
        QVERIFY(!db.splitDatasetAtomic(
            {target, invalid_target}, split_out, err,
            [&]()
            {
                observed_inserted_image = observed_inserted_image || !split_out.image_ids.empty();
                return false;
            }));
        QVERIFY(observed_inserted_image);
        QVERIFY(split_out.dataset_ids.empty());
        QVERIFY(split_out.image_ids.empty());
        QVERIFY(split_out.label_ids.empty());
        dltool::database::ProjectDataBase reopened(PersistentProjectFixture::projectDatabasePath());
        after_ids.clear();
        after_names.clear();
        QVERIFY(reopened.getAllDatasets(after_ids, after_names, err));
        QVERIFY(std::find(after_names.begin(), after_names.end(), phantom_dataset_name) == after_names.end());

        // moveImagesAtomic: update is performed, then cancellation must roll it back.
        const QString source_name = QStringLiteral("Move-Rollback-Source-%1").arg(QDateTime::currentMSecsSinceEpoch());
        const QString target_name = QStringLiteral("Move-Rollback-Target-%1").arg(QDateTime::currentMSecsSinceEpoch());
        int64_t source_id = -1;
        int64_t target_id = -1;
        QVERIFY(db.addDataset(source_name, source_id, err));
        QVERIFY(db.addDataset(target_name, target_id, err));
        std::vector<int64_t> movable_ids;
        QVERIFY(db.copyImagesAtomic(source_id, {valid_snapshot}, copy_out, err));
        movable_ids = copy_out.image_ids;
        int callback_count = 0;
        QVERIFY(!db.moveImagesAtomic(movable_ids, target_id, err, [&]() { return ++callback_count > 2; }));
        QCOMPARE(callback_count, 3);
        QSet<qint64> source_after_move;
        QVERIFY(imageIdsForDataset(PersistentProjectFixture::projectDatabasePath(), source_id, &source_after_move, nullptr, &err));
        QCOMPARE(source_after_move.size(), 1);
        QVERIFY(source_after_move.contains(movable_ids.front()));
        QCOMPARE(reopened.getImagesCount(target_id), 0);
        QVERIFY(db.deleteDatasetsWithContents({source_id, target_id}, err));

        // 3. moveImagesAtomic with invalid target dataset: fails without moving
        bool move_ok = db.moveImagesAtomic({1, 2}, -99999, err);
        QVERIFY(!move_ok);
    }
};

QTEST_GUILESS_MAIN(DataSplitIntegrationTest)

#include "test_DataSplit.moc"
