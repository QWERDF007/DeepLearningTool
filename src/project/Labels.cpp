#include "project/Labels.h"

#include "data/DataBase.h"
#include "project/Images.h"
#include "project/LabelClasses.h"

#include <json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace dltool::project {

std::vector<uint8_t> LabelData_t::toBlob() const
{
    json j = json{
        {     "x",      x},
        {     "y",      y},
        { "width",  width},
        {"height", height},
    };

    return json::to_bson(j);
}

void LabelData_t::fromBlob(const std::vector<uint8_t> &blob)
{
    json j = json::from_bson(blob);
    x      = j.value("x", -1);
    y      = j.value("y", -1);
    width  = j.value("width", -1);
    height = j.value("height", -1);
}

LabelInstancesListModel::LabelInstancesListModel(data::ProjectDataBase   *database,
                                                 ImageInstancesListModel *image_instances,
                                                 LabelClassesListModel *label_classes, QObject *parent)
    : QAbstractListModel(parent)
    , database_(database)
    , image_instances_(image_instances)
    , label_classes_(label_classes)
{
    init();
}

LabelInstancesListModel::~LabelInstancesListModel() {}

void LabelInstancesListModel::init()
{
    if (database_ == nullptr)
    {
        spdlog::error("初始化标注(Label)失败: 数据库未初始化");
        return;
    }
    std::vector<int64_t>              label_ids, image_ids, label_class_ids, label_types;
    std::vector<std::vector<uint8_t>> labels_data;
    QString                           err_msg;
    bool ok = database_->getAllLabels(label_ids, image_ids, label_class_ids, label_types, labels_data, err_msg);
    if (!ok)
    {
        spdlog::error("查询所有标注失败: {}", err_msg.toUtf8().constData());
        return;
    }
    std::map<int64_t, std::vector<int64_t>> images_label_ids;
    for (size_t i = 0; i < label_ids.size(); ++i)
    {
        LabelData_t label_data;
        label_data.fromBlob(labels_data[i]);
        insert(label_ids[i], image_ids[i], label_class_ids[i], label_data);
        if (images_label_ids.find(image_ids[i]) == images_label_ids.end())
            images_label_ids.emplace(image_ids[i], std::vector<int64_t>());
        images_label_ids[image_ids[i]].emplace_back(label_ids[i]);
    }
    for (auto &[image_id, image_label_ids] : images_label_ids)
    {
        auto instance = image_instances_->getImageInstance(image_id);
        instance->addLabelIds(image_label_ids);
    }
}

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

void LabelInstancesListModel::addLabels(std::vector<int64_t> &label_ids, const std::vector<int64_t> &image_ids,
                                        const std::vector<int64_t>     &label_class_ids,
                                        const std::vector<QVariantMap> &data)
{
    if (database_ == nullptr)
    {
        spdlog::error("添加标注失败: 数据库未初始化");
        return;
    }
    if (image_instances_ == nullptr)
    {
        spdlog::error("添加标注失败: 图像实例列表未初始化");
        return;
    }

    std::vector<LabelData_t>          labels_data;
    std::vector<std::vector<uint8_t>> labels_data_blob;
    std::vector<int64_t>              label_types;

    for (size_t i = 0; i < image_ids.size(); ++i)
    {
        const QVariantMap &_data = data[i];
        LabelData_t        label_data;
        label_data.type   = LabelType::Rect;
        label_data.x      = _data.value("x", 0).toDouble();
        label_data.y      = _data.value("y", 0).toDouble();
        label_data.width  = _data.value("width", 0).toDouble();
        label_data.height = _data.value("height", 0).toDouble();
        labels_data.push_back(label_data);
        label_types.push_back(label_data.type);
        labels_data_blob.push_back(label_data.toBlob());
    }

    QString err_msg;
    bool    ok = database_->addLabels(image_ids, label_class_ids, label_types, labels_data_blob, label_ids, err_msg);
    if (!ok)
    {
        spdlog::error("添加标注失败: {}", err_msg.toUtf8().constData());
        return;
    }
    if (label_ids.size() != image_ids.size())
    {
        spdlog::error("添加标注失败: 标签ID数量与图像ID数量不一致, {} != {}", label_ids.size(), image_ids.size());
        return;
    }
    std::map<int64_t, std::vector<int64_t>> images_label_ids;
    for (size_t i = 0; i < label_ids.size(); ++i)
    {
        insert(label_ids[i], image_ids[i], label_class_ids[i], labels_data[i]);
        if (images_label_ids.find(image_ids[i]) == images_label_ids.end())
            images_label_ids[image_ids[i]] = std::vector<int64_t>();
        images_label_ids[image_ids[i]].push_back(label_ids[i]);
    }
    for (const auto&[image_id, image_label_ids] : images_label_ids)
    {
        ImageInstance *instance = image_instances_->getImageInstance(image_id);
        if (instance)
            instance->addLabelIds(image_label_ids);
    }
}

void LabelInstancesListModel::insert(const int64_t label_id, const int64_t image_id, const int64_t label_class_id,
                                     const LabelData_t &data)
{
    label_ids_.push_back(label_id);
    label_instances_.emplace(label_id, new LabelInstance(label_id, image_id, label_class_id, data, this));
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
    const LabelData_t &data = label_instances_.at(label_ids_[index.row()])->data();

    QVariantMap map;
    map["x"]      = data.x;
    map["y"]      = data.y;
    map["width"]  = data.width;
    map["height"] = data.height;

    return map;
}

ImageLabelsListModel::ImageLabelsListModel(ImageInstancesListModel *image_instances,
                                           LabelInstancesListModel *label_instances,
                                           LabelClassesListModel *label_classes, QObject *parent)
    : QAbstractListModel(parent)
    , image_instances_(image_instances)
    , label_instances_(label_instances)
    , label_classes_(label_classes)
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
    const int                          image_id  = image_instances_->getCurImageId();
    const std::vector<ImageInstance *> instances = image_instances_->getImageInstances({image_id});
    if (!instances.empty() && instances.at(0))
    {
        std::set<int64_t> label_ids = instances.at(0)->labelIds();
        label_ids_.clear();
        label_ids_.insert(label_ids_.end(), label_ids.begin(), label_ids.end());
    }
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
    case LabelClassColorRole:
        return getColor(index);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> ImageLabelsListModel::roleNames() const
{
    return {
        {        LabelIdRole,       "label_id"},
        {        ImageIdRole,       "image_id"},
        {   LabelClassIdRole, "label_class_id"},
        {           DataRole,           "data"},
        {LabelClassColorRole,          "color"},
    };
}

void ImageLabelsListModel::addLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids)
{
    if (image_ids.empty() || label_ids.empty())
        return;
    const int64_t        cur_image_id = image_instances_->getCurImageId();
    std::vector<int64_t> valid_label_ids;
    for (size_t i = 0; i < image_ids.size(); ++i)
    {
        if (image_ids[i] == cur_image_id)
            valid_label_ids.push_back(label_ids[i]);
    }
    if (valid_label_ids.empty())
        return;
    int row   = rowCount();
    int count = static_cast<int>(valid_label_ids.size());
    beginInsertRows(QModelIndex(), row, row + count - 1);
    label_ids_.insert(label_ids_.end(), valid_label_ids.begin(), valid_label_ids.end());
    endInsertRows();
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
    const LabelData_t &data = instance->data();

    QVariantMap map;
    map["x"]      = data.x;
    map["y"]      = data.y;
    map["width"]  = data.width;
    map["height"] = data.height;

    return map;
}

QVariant ImageLabelsListModel::getColor(const QModelIndex &index) const
{
    int64_t        label_id = label_ids_[index.row()];
    LabelInstance *instance = label_instances_->getLabelInstance(label_id);
    if (instance == nullptr)
        return QVariant();
    return label_classes_->getLabelClassColor(instance->labelClassId());
}

} // namespace dltool::project
