#include "data/DataSelectionTreeModel.h"

#include <QSignalSpy>
#include <QTest>

namespace {

class SourceModel final : public QAbstractListModel
{
public:
    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : 1;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() != 0)
            return {};
        if (role == Qt::UserRole + 1)
            return 1;
        if (role == Qt::DisplayRole)
            return QStringLiteral("item");
        return {};
    }

    void notifyDataChanged(const QList<int> &roles)
    {
        emit dataChanged(index(0), index(0), roles);
    }
};

} // namespace

class DataSelectionTreeModelTest final : public QObject
{
    Q_OBJECT

private slots:
    void rebuildsOnlyWhenSourceRolesAffectTree()
    {
        SourceModel source;
        dltool::data::DataSelectionTreeModel tree;
        QSignalSpy                       reset_spy(&tree, &QAbstractItemModel::modelReset);

        tree.setSourceModel(&source);
        QCOMPARE(reset_spy.count(), 1);

        source.notifyDataChanged({Qt::UserRole + 2});
        QCOMPARE(reset_spy.count(), 1);

        source.notifyDataChanged({Qt::UserRole + 1});
        QCOMPARE(reset_spy.count(), 2);

        source.notifyDataChanged({Qt::DisplayRole});
        QCOMPARE(reset_spy.count(), 3);

        source.notifyDataChanged({});
        QCOMPARE(reset_spy.count(), 4);
    }
};

QTEST_GUILESS_MAIN(DataSelectionTreeModelTest)

#include "test_DataSelectionTreeModel.moc"
