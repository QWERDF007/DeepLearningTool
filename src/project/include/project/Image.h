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
    ImageInstance(const int64_t id, const QString &path, QObject *parent = nullptr);
    ~ImageInstance();

    QString path() const
    {
        return path_;
    }

    int64_t id() const
    {
        return id_;
    }

private:
    int64_t id_;

    QString path_;
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

    bool addImageInstance(const int64_t dataset_id, const QString &path);
    bool deleteImageInstance(const int64_t image_id);

private:
    data::ProjectDataBase *database_{nullptr};

    std::map<int64_t, ImageInstance *> image_instances_;
};

} // namespace dltool::project
