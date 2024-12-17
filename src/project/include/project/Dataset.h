#pragma once

#include <QAbstractListModel>
#include <QtQml>
#include <map>

namespace dltool::data {
class ProjectDataBase;
}

namespace dltool::project {

class Dataset : public QObject
{
public:
    Dataset(const int64_t id, const QString &name, QObject *parent = nullptr);
    ~Dataset();

    QString name() const
    {
        return name_;
    }

    bool setName(const QString &name);

private:
    int64_t id_;

    QString name_;
};

class DatasetsListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DatasetsList)
    QML_UNCREATABLE("Can not create DatasetsList directly!")
public:
    DatasetsListModel(data::ProjectDataBase *database, QObject *parent = nullptr);
    ~DatasetsListModel();

    enum Role
    {
        DatasetIdRole = Qt::UserRole + 1,
        NameRole,
        StatsRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    QHash<int, QByteArray> roleNames() const override;

    bool insertRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    Q_INVOKABLE bool addDataset(const QString &name);
    Q_INVOKABLE bool updateDataset(const QString &old_name, const QString &new_name);
    Q_INVOKABLE bool deleteDataset(const QString &name);

private:
    int getDatasetId(const QModelIndex &index) const;

    QVariant getName(const QModelIndex &index) const;
    QVariant getStats(const QModelIndex &index) const;

    data::ProjectDataBase *database_{nullptr};

    std::map<int64_t, Dataset *> datasets_;
};

} // namespace dltool::project
