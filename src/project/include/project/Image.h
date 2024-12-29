#pragma once

#include <QAbstractListModel>
#include <QtQml>
#include <map>

namespace dltool::data {
class ProjectDataBase;
}

namespace dltool::project {

class ImageInstance : public QObject
{
public:
    ImageInstance(const int64_t dataset_id, const int64_t image_id, const QString &path, QObject *parent = nullptr);
    ~ImageInstance();

    int64_t datasetId() const
    {
        return dataset_id_;
    }

    int64_t imageId() const
    {
        return image_id_;
    }

    QString name() const
    {
        return name_;
    }

    QString path() const
    {
        return path_;
    }

private:
    int64_t dataset_id_;

    int64_t image_id_;

    QString path_;

    QString name_;
};

class ImageInstancesListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ImageInstancesList)
    QML_UNCREATABLE("Can not create ImageInstancesList directly!")
public:
    ImageInstancesListModel(data::ProjectDataBase *database, QObject *parent = nullptr);
    ~ImageInstancesListModel();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    enum Role
    {
        ImageIdRole = Qt::UserRole + 1,
        NameRole,
        PathRole,
    };

    QHash<int, QByteArray> roleNames() const override;

    bool addImageInstances(const int64_t dataset_id, const std::vector<QString> &paths);
    bool addImageInstances(const int64_t dataset_id, const QString &image_idr);
    bool deleteImageInstances(const std::vector<int64_t> &image_ids);

    static std::vector<QString> getImagePaths(const QString &image_idr);

    static std::vector<QString> getFiles(const QString &path, const QStringList &name_filters, bool recursive);

private:
    void init();

    QVariant getImageId(const QModelIndex &index) const;
    QVariant getImageName(const QModelIndex &index) const;
    QVariant getImagePath(const QModelIndex &index) const;

    data::ProjectDataBase *database_{nullptr};

    std::map<int64_t, ImageInstance *> image_instances_;
};

} // namespace dltool::project
