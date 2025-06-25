#pragma once

#include <QAbstractListModel>
#include <QtQml>

namespace dltool::data {
class ProjectDataBase;
}

namespace dltool::project {

class ImageInstancesListModel;
class LabelClassesListModel;

class LabelInstance : public QObject
{
public:
    struct BaseData_t
    {
        double x;
        double y;
        double width;
        double height;
    };

    LabelInstance(const int64_t label_id, const int64_t image_id, const int64_t label_class_id, const BaseData_t &data,
                  QObject *parent = nullptr)
        : QObject(parent)
        , label_id_(label_id)
        , image_id_(image_id)
        , label_class_id_(label_class_id)
        , data_(data)
    {
    }

    ~LabelInstance() {}

    int64_t labelId() const
    {
        return label_id_;
    }

    int64_t imageId() const
    {
        return image_id_;
    }

    int64_t labelClassId() const
    {
        return label_class_id_;
    }

    BaseData_t &data()
    {
        return data_;
    }

    const BaseData_t &data() const
    {
        return data_;
    }

private:
    int64_t label_id_;
    int64_t image_id_;
    int64_t label_class_id_;

    BaseData_t data_;
};

class LabelInstancesListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(LabelInstancesModel)
    QML_UNCREATABLE("Can not create LabelInstancesModel directly!")
public:
    LabelInstancesListModel(data::ProjectDataBase *database, ImageInstancesListModel *image_instances,
                            LabelClassesListModel *label_classes, QObject *parent = nullptr);

    ~LabelInstancesListModel();

    enum Role
    {
        LabelIdRole = Qt::UserRole + 1,
        ImageIdRole,
        LabelClassIdRole,
        DataRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    QHash<int, QByteArray> roleNames() const override;

    LabelInstance *getLabelInstance(const int64_t label_id);

private:
    int64_t  getLabelId(const QModelIndex &index) const;
    int64_t  getImageId(const QModelIndex &index) const;
    int64_t  getLabelClassId(const QModelIndex &index) const;
    QVariant getData(const QModelIndex &index) const;

    data::ProjectDataBase   *database_{nullptr};
    ImageInstancesListModel *image_instances_{nullptr};
    LabelClassesListModel   *label_classes_{nullptr};

    std::map<int64_t, LabelInstance *> label_instances_;
    std::vector<int64_t>               label_ids_;
};

class ImageLabelsListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ImageLabelsModel)
    QML_UNCREATABLE("Can not create ImageLabelsModel directly!")
public:
    ImageLabelsListModel(ImageInstancesListModel *image_instances, LabelInstancesListModel *label_instances,
                         QObject *parent = nullptr);

    ~ImageLabelsListModel() {}

    enum Role
    {
        LabelIdRole = Qt::UserRole + 1,
        ImageIdRole,
        LabelClassIdRole,
        DataRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    QHash<int, QByteArray> roleNames() const override;

private:
    void init();

    void resetModel();

    int64_t  getLabelId(const QModelIndex &index) const;
    int64_t  getImageId(const QModelIndex &index) const;
    int64_t  getLabelClassId(const QModelIndex &index) const;
    QVariant getData(const QModelIndex &index) const;

    ImageInstancesListModel *image_instances_{nullptr};
    LabelInstancesListModel *label_instances_{nullptr};

    int64_t image_id_{-1};

    std::vector<int64_t> label_ids_;
};

} // namespace dltool::project
