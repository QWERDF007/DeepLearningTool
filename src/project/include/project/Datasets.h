#pragma once

#include <QAbstractListModel>
#include <QtQml>
#include <map>
#include <set>

namespace dltool::data {
class ProjectDataBase;
}

namespace dltool::project {

class Dataset : public QObject
{
public:
    Dataset(const int64_t id, const QString &name, QObject *parent = nullptr)
        : QObject(parent)
        , id_(id)
        , name_(name)

    {
    }

    ~Dataset() {}

    QString name() const
    {
        return name_;
    }

    bool setName(const QString &name)
    {
        if (name_ == name)
            return false;
        name_ = name;
        return true;
    }

    int64_t id() const
    {
        return id_;
    }

    const std::set<int64_t> &imageIds() const
    {
        return image_ids_;
    }

    std::set<int64_t> &imageIds()
    {
        return image_ids_;
    }

    void addImageIds(const std::vector<int64_t> &image_ids)
    {
        image_ids_.insert(image_ids.begin(), image_ids.end());
    }

    void removeImageIds(const std::vector<int64_t> &image_ids)
    {
        for (const auto &image_id : image_ids)
        {
            image_ids_.erase(image_id);
        }
    }

private:
    int64_t id_;

    QString name_;

    std::set<int64_t> image_ids_;
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
        ProgressRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    QHash<int, QByteArray> roleNames() const override;

    bool insertRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    bool addDataset(const QString &name);
    bool updateDataset(const int64_t dataset_id, const QString &name);
    bool deleteDataset(const int64_t dataset_id);

    QList<QString> getAllDatasetsName() const;

    int     getDatasetId(const QString &dataset_name) const;
    QString getDatasetName(const int dataset_id) const;

    void addImages(const std::vector<int64_t> &dataset_id, const std::vector<int64_t> &image_ids);
    void deleteImages(const std::vector<int64_t> &dataset_id, const std::vector<int64_t> &image_ids);

    void setStats(const std::vector<int64_t> &dataset_ids, const std::vector<int64_t> &image_ids,
                  const std::vector<std::vector<int64_t>> &images_labels_count);

private:
    void init();

    int getDatasetId(const QModelIndex &index) const;

    QVariant getName(const QModelIndex &index) const;
    QVariant getStats(const QModelIndex &index) const;
    QVariant getProgress(const QModelIndex &index) const;

    void onStatsChanged();

    data::ProjectDataBase *database_{nullptr};

    std::map<int64_t, Dataset *> datasets_;

    std::map<int64_t, int64_t> labelled_image_stats_;

signals:
    void statsChanged();
};

} // namespace dltool::project
