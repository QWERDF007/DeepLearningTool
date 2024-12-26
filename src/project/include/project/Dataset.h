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

    int64_t id() const
    {
        return id_;
    }

private:
    int64_t id_;

    QString name_;
};

class DatasetsListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
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

    bool addDataset(const QString &name);
    bool updateDataset(const QString &old_name, const QString &new_name);
    bool deleteDataset(const int64_t dataset_id);

    QList<QString> getDatasetsName() const;

private:
    int getDatasetId(const QModelIndex &index) const;

    QVariant getName(const QModelIndex &index) const;
    QVariant getStats(const QModelIndex &index) const;

    data::ProjectDataBase *database_{nullptr};

    std::map<int64_t, Dataset *> datasets_;
};

} // namespace dltool::project
