#include "data/DataSelectionTreeModel.h"
#include "data/DataManager.h"
#include "data/DatasetViewModelFactory.h"
#include "data/Datasets.h"
#include "data/LabelClasses.h"
#include "database/DataBase.h"
#include "core/CoreDef.h"

#include <QDateTime>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <vector>

namespace {

class DynamicSourceModel final : public QAbstractListModel
{
    Q_OBJECT
public:
    struct Item
    {
        qint64  id;
        QString name;
        QString color;
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(items_.size());
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(items_.size()))
            return {};
        const auto &item = items_[static_cast<size_t>(index.row())];
        if (role == Qt::UserRole + 1)
            return item.id;
        if (role == Qt::DisplayRole)
            return item.name;
        if (role == Qt::UserRole + 3)
            return item.color;
        return {};
    }

    void addItem(qint64 id, const QString &name, const QString &color = QString())
    {
        const int pos = static_cast<int>(items_.size());
        beginInsertRows(QModelIndex(), pos, pos);
        items_.push_back({id, name, color});
        endInsertRows();
    }

    void removeItem(int row)
    {
        if (row < 0 || row >= static_cast<int>(items_.size()))
            return;
        beginRemoveRows(QModelIndex(), row, row);
        items_.erase(items_.begin() + row);
        endRemoveRows();
    }

    void updateName(int row, const QString &new_name)
    {
        if (row < 0 || row >= static_cast<int>(items_.size()))
            return;
        items_[static_cast<size_t>(row)].name = new_name;
        emit dataChanged(index(row), index(row), {Qt::DisplayRole});
    }

    void notifyDataChanged(int row, const QList<int> &roles)
    {
        emit dataChanged(index(row), index(row), roles);
    }

private:
    std::vector<Item> items_;
};

} // namespace

class DataSelectionTreeModelTest final : public QObject
{
    Q_OBJECT

private slots:
    void metadataChangeEmitsDataChangedWithoutReset()
    {
        DynamicSourceModel source;
        source.addItem(1, QStringLiteral("item_1"));
        source.addItem(2, QStringLiteral("item_2"));

        dltool::data::DataSelectionTreeModel tree;
        QSignalSpy reset_spy(&tree, &QAbstractItemModel::modelReset);
        QSignalSpy data_changed_spy(&tree, &QAbstractItemModel::dataChanged);

        tree.setSourceModel(&source);
        QCOMPARE(reset_spy.count(), 1);
        QCOMPARE(tree.rowCount(), 2);
        QCOMPARE(tree.data(tree.index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("item_1"));

        // Renaming item_1: DisplayRole only
        source.updateName(0, QStringLiteral("item_1_renamed"));

        // Reset spy MUST NOT increase! Local dataChanged MUST fire.
        QCOMPARE(reset_spy.count(), 1);
        QCOMPARE(data_changed_spy.count(), 1);
        QCOMPARE(tree.data(tree.index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("item_1_renamed"));
    }

    void roleFilteringIgnoresIrrelevantChanges()
    {
        DynamicSourceModel source;
        source.addItem(1, QStringLiteral("item_1"));

        dltool::data::DataSelectionTreeModel tree;
        QSignalSpy reset_spy(&tree, &QAbstractItemModel::modelReset);
        QSignalSpy data_changed_spy(&tree, &QAbstractItemModel::dataChanged);

        tree.setSourceModel(&source);
        QCOMPARE(reset_spy.count(), 1);

        // Irrelevant role change (e.g. Qt::UserRole + 99)
        source.notifyDataChanged(0, {Qt::UserRole + 99});

        QCOMPARE(reset_spy.count(), 1);
        QCOMPARE(data_changed_spy.count(), 0);
    }

    void structuralChangesPreserveSelectionsByStableId()
    {
        DynamicSourceModel source;
        source.addItem(10, QStringLiteral("item_10"));
        source.addItem(20, QStringLiteral("item_20"));
        source.addItem(30, QStringLiteral("item_30"));

        dltool::data::DataSelectionTreeModel tree;
        tree.setSourceModel(&source);

        // Select item 10 and item 20
        tree.setSelectedId(10, true);
        tree.setSelectedId(20, true);
        QVERIFY(tree.isSelectedId(10));
        QVERIFY(tree.isSelectedId(20));
        QVERIFY(!tree.isSelectedId(30));
        QCOMPARE(tree.selectedCount(), 2);

        QSignalSpy reset_spy(&tree, &QAbstractItemModel::modelReset);

        // Add new item 40
        source.addItem(40, QStringLiteral("item_40"));
        QCOMPARE(tree.rowCount(), 4);

        // Selections must remain intact!
        QVERIFY(tree.isSelectedId(10));
        QVERIFY(tree.isSelectedId(20));
        QVERIFY(!tree.isSelectedId(30));
        QVERIFY(!tree.isSelectedId(40));
        QCOMPARE(tree.selectedCount(), 2);
        QCOMPARE(reset_spy.count(), 0); // Incremental update, no reset!

        // Remove item 20 (row 1)
        source.removeItem(1);
        QCOMPARE(tree.rowCount(), 3);

        // Item 10 must still be selected, item 20 pruned
        QVERIFY(tree.isSelectedId(10));
        QVERIFY(!tree.isSelectedId(20));
        QCOMPARE(tree.selectedCount(), 1);
        QCOMPARE(reset_spy.count(), 0); // Incremental update, no reset!
    }

    void batchUpdatesAreMerged()
    {
        DynamicSourceModel source;
        source.addItem(1, QStringLiteral("item_1"));

        dltool::data::DataSelectionTreeModel tree;
        tree.setSourceModel(&source);
        tree.setSelectedId(1, true);

        // Rapid batch additions
        for (int i = 2; i <= 50; ++i)
        {
            source.addItem(i, QString("item_%1").arg(i));
        }

        QCOMPARE(tree.rowCount(), 50);
        QVERIFY(tree.isSelectedId(1));
        QCOMPARE(tree.selectedCount(), 1);
    }

    void realDataManagerProjectIntegration()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString project_path = QDir(dir.path()).filePath(QStringLiteral("tree_test.dlpro"));
        dltool::database::ProjectDataBase database(project_path);
        QString err;
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        QVERIFY(database.initProject(QStringLiteral("TreeTest"),
                                     static_cast<int>(dltool::core::DeepLearningMethod::AnomalyDetection),
                                     project_path, QStringLiteral("desc"), dir.path(), now, now, err));

        int64_t ds1_id = -1;
        int64_t ds2_id = -1;
        QVERIFY(database.addDataset(QStringLiteral("dataset_1"), ds1_id, err));
        QVERIFY(database.addDataset(QStringLiteral("dataset_2"), ds2_id, err));

        dltool::data::DataManager data_manager(
            static_cast<int>(dltool::core::DeepLearningMethod::AnomalyDetection), &database, dir.path());
        data_manager.waitForOperations();

        auto *tree = dltool::data::DatasetViewModelFactory::createDatasetSelectionModel(&data_manager, this);
        QVERIFY(tree != nullptr);

        QCOMPARE(tree->rowCount(), 2);

        // Select dataset 1
        tree->setSelectedId(ds1_id, true);
        QVERIFY(tree->isSelectedId(ds1_id));
        QVERIFY(!tree->isSelectedId(ds2_id));

        QSignalSpy reset_spy(tree, &QAbstractItemModel::modelReset);
        QSignalSpy data_changed_spy(tree, &QAbstractItemModel::dataChanged);

        // Rename dataset 1 via DataManager
        data_manager.updateDataset(ds1_id, QStringLiteral("dataset_1_renamed"));
        data_manager.waitForOperations();

        // Must update without full tree reset
        QCOMPARE(reset_spy.count(), 0);
        QVERIFY(data_changed_spy.count() >= 1);
        QCOMPARE(tree->data(tree->index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("dataset_1_renamed"));
        QVERIFY(tree->isSelectedId(ds1_id));

        // Add a new dataset via DataManager
        int64_t ds3_id = -1;
        QVERIFY(data_manager.ensureDataset(QStringLiteral("dataset_3"), ds3_id, err));

        QCOMPARE(tree->rowCount(), 3);
        QVERIFY(tree->isSelectedId(ds1_id));
        QCOMPARE(reset_spy.count(), 0);

        data_manager.shutdown();
    }
};

QTEST_GUILESS_MAIN(DataSelectionTreeModelTest)

#include "test_DataSelectionTreeModel.moc"
