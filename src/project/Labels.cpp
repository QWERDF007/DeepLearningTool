#include "project/Labels.h"

#include "project/Images.h"
#include "project/LabelClasses.h"

namespace dltool::project {

LabelInstancesListModel::LabelInstancesListModel(data::ProjectDataBase   *database,
                                                 ImageInstancesListModel *image_instances,
                                                 LabelClassesListModel *label_classes, QObject *parent)
    : QAbstractListModel(parent)
    , database_(database)
    , image_instances_(image_instances)
    , label_classes_(label_classes)
{
}

LabelInstancesListModel::~LabelInstancesListModel() {}

int LabelInstancesListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(label_ids_.size());
}

QVariant LabelInstancesListModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= rowCount())
        return QVariant();
    switch (role)
    {
    case LabelIdRole:
        return getLabelId(index);
    case ImageIdRole:
        return getImageId(index);
    case LabelClassIdRole:
        return getLabelClassId(index);
    case DataRole:
        return getData(index);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> LabelInstancesListModel::roleNames() const
{
    return {
        {     LabelIdRole,       "label_id"},
        {     ImageIdRole,       "image_id"},
        {LabelClassIdRole, "label_class_id"},
        {        DataRole,           "data"},
    };
}

LabelInstance *LabelInstancesListModel::getLabelInstance(const int64_t label_id)
{
    auto found = label_instances_.find(label_id);
    if (found == label_instances_.end())
        return nullptr;
    return found->second;
}

int64_t LabelInstancesListModel::getLabelId(const QModelIndex &index) const
{
    return label_ids_[index.row()];
}

int64_t LabelInstancesListModel::getImageId(const QModelIndex &index) const
{
    return label_instances_.at(label_ids_[index.row()])->imageId();
}

int64_t LabelInstancesListModel::getLabelClassId(const QModelIndex &index) const
{
    return label_instances_.at(label_ids_[index.row()])->labelClassId();
}

QVariant LabelInstancesListModel::getData(const QModelIndex &index) const
{
    const LabelInstance::BaseData_t &data = label_instances_.at(label_ids_[index.row()])->data();

    QVariantMap map;
    map["x"]      = data.x;
    map["y"]      = data.y;
    map["width"]  = data.width;
    map["height"] = data.height;

    return map;
}

ImageLabelsListModel::ImageLabelsListModel(ImageInstancesListModel *image_instances,
                                           LabelInstancesListModel *label_instances, QObject *parent)
    : QAbstractListModel(parent)
    , image_instances_(image_instances)
    , label_instances_(label_instances)
{
    init();
}

void ImageLabelsListModel::init()
{
    if (image_instances_ == nullptr || label_instances_ == nullptr)
        return;
    connect(image_instances_, &ImageInstancesListModel::curImageChanged, this, &ImageLabelsListModel::resetModel);
}

void ImageLabelsListModel::resetModel()
{
    beginResetModel();
    // TODO: 获取当前图像的label_ids
    endResetModel();
}

int ImageLabelsListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || label_instances_ == nullptr)
        return 0;
    return static_cast<int>(label_ids_.size());
}

QVariant ImageLabelsListModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= rowCount())
        return QVariant();
    switch (role)
    {
    case LabelIdRole:
        return getLabelId(index);
    case ImageIdRole:
        return getImageId(index);
    case LabelClassIdRole:
        return getLabelClassId(index);
    case DataRole:
        return getData(index);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> ImageLabelsListModel::roleNames() const
{
    return {
        {     LabelIdRole,       "label_id"},
        {     ImageIdRole,       "image_id"},
        {LabelClassIdRole, "label_class_id"},
        {        DataRole,           "data"},
    };
}

int64_t ImageLabelsListModel::getLabelId(const QModelIndex &index) const
{
    return label_ids_[index.row()];
}

int64_t ImageLabelsListModel::getImageId(const QModelIndex &index) const
{
    int64_t        label_id = label_ids_[index.row()];
    LabelInstance *instance = label_instances_->getLabelInstance(label_id);
    if (instance == nullptr)
        return -1;
    return instance->imageId();
}

int64_t ImageLabelsListModel::getLabelClassId(const QModelIndex &index) const
{
    int64_t        label_id = label_ids_[index.row()];
    LabelInstance *instance = label_instances_->getLabelInstance(label_id);
    if (instance == nullptr)
        return -1;
    return instance->labelClassId();
}

QVariant ImageLabelsListModel::getData(const QModelIndex &index) const
{
    int64_t        label_id = label_ids_[index.row()];
    LabelInstance *instance = label_instances_->getLabelInstance(label_id);
    if (instance == nullptr)
        return QVariantMap();
    const LabelInstance::BaseData_t &data = instance->data();

    QVariantMap map;
    map["x"]      = data.x;
    map["y"]      = data.y;
    map["width"]  = data.width;
    map["height"] = data.height;

    return map;
}

} // namespace dltool::project
