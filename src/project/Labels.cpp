#include "project/Labels.h"

#include "data/DataBase.h"
#include "data/LabelData.h"
#include "project/Images.h"
#include "project/LabelClasses.h"

#include <spdlog/spdlog.h>

namespace dltool::project {

LabelInstance::LabelInstance(const int64_t label_id, const int64_t image_id, const int64_t label_class_id,
                             LabelData data, QObject *parent)
    : QObject(parent)
    , label_id_(label_id)
    , image_id_(image_id)
    , label_class_id_(label_class_id)
    , data_(std::move(data))
{

}

LabelInstance::~LabelInstance()
{

}

LabelInstancesListModel::LabelInstancesListModel(data::ProjectDataBase   *database,
                                                 ImageInstancesListModel *image_instances,
                                                 LabelClassesListModel *label_classes, LabelDataFactory factory,
                                                 QObject *parent)
    : QAbstractListModel(parent)
    , database_(database)
    , image_instances_(image_instances)
    , label_classes_(label_classes)
    , factory_(std::move(factory))
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
    if (factory_ == nullptr)
    {
        spdlog::error("查询所有标注失败, 标签数据工厂未初始化");
        return;
    }
    for (size_t i = 0; i < label_ids.size(); ++i)
    {
        LabelData data = factory_();
        data->fromBlob(labels_data[i]);
        label_instances_[label_ids[i]]
            = new LabelInstance(label_ids[i], image_ids[i], label_class_ids[i], std::move(data), this);
    }
    std::reverse(label_ids.begin(), label_ids.end());
    label_ids_.insert(label_ids_.end(), label_ids.begin(), label_ids.end());
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

    if (factory_ == nullptr)
    {
        spdlog::error("添加标注失败: 标签数据工厂未初始化");
        return;
    }

    std::vector<int64_t>              label_types;
    std::vector<LabelData>            labels_data;
    std::vector<std::vector<uint8_t>> labels_data_blob;

    label_types.reserve(image_ids.size());
    labels_data.reserve(image_ids.size());
    labels_data_blob.reserve(image_ids.size());

    for (size_t i = 0; i < image_ids.size(); ++i)
    {
        LabelData label_data = factory_();
        label_data->fromQVariantMap(data[i]);
        label_types.push_back(label_data->type());
        labels_data_blob.push_back(label_data->toBlob());
        labels_data.push_back(std::move(label_data));
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

    for (size_t i = 0; i < label_ids.size(); ++i)
    {
        label_instances_[label_ids[i]]
            = new LabelInstance(label_ids[i], image_ids[i], label_class_ids[i], std::move(labels_data[i]), this);
    }
    std::vector<int64_t> sorted_label_ids(label_ids.begin(), label_ids.end());
    std::reverse(sorted_label_ids.begin(), sorted_label_ids.end());
    label_ids_.insert(label_ids_.end(), sorted_label_ids.begin(), sorted_label_ids.end());
}

void LabelInstancesListModel::getAllImagesLabelIds(std::vector<int64_t> &image_ids,
                                                   std::vector<int64_t> &label_ids) const
{
    for (const auto &[label_id, instance] : label_instances_)
    {
        const int64_t image_id = instance->imageId();
        image_ids.push_back(image_id);
        label_ids.push_back(label_id);
    }
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
    // return label_instances_.at(label_ids_[index.row()])->data()->dataMap();
    return QVariant();
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
    return instance->data()->dataMap();
}

QVariant ImageLabelsListModel::getColor(const QModelIndex &index) const
{
    int64_t        label_id = label_ids_[index.row()];
    LabelInstance *instance = label_instances_->getLabelInstance(label_id);
    if (instance == nullptr)
        return QVariant();
    return label_classes_->getLabelClassColor(instance->labelClassId());
}

ImageLabelsTableModel::ImageLabelsTableModel(ImageInstancesListModel *image_instances,
                                             LabelInstancesListModel *label_instances,
                                             LabelClassesListModel   *label_classes,
                                             const std::pair<std::vector<QString>, std::vector<QString>> &columns,
                                             QObject                                                     *parent)
    : QAbstractTableModel(parent)
    , image_instances_(image_instances)
    , label_instances_(label_instances)
    , label_classes_(label_classes)
    , column_headers_(columns.first)
    , column_keys_(columns.second)
{
    // TODO: 添加列名和数据key
    init();
}

ImageLabelsTableModel::~ImageLabelsTableModel() {}

void ImageLabelsTableModel::init()
{
    if (image_instances_ == nullptr || label_instances_ == nullptr)
        return;
    connect(image_instances_, &ImageInstancesListModel::curImageChanged, this, &ImageLabelsTableModel::resetModel);
}

void ImageLabelsTableModel::resetModel()
{
    beginResetModel();
    const int                          image_id  = image_instances_->getCurImageId();
    const std::vector<ImageInstance *> instances = image_instances_->getImageInstances({image_id});
    if (!instances.empty() && instances.at(0))
    {
        std::set<int64_t> label_ids = instances.at(0)->labelIds();
        label_ids_.clear();
        label_ids_.insert(label_ids_.end(), label_ids.begin(), label_ids.end());
        for (const int64_t label_id : label_ids)
        {
            const LabelInstance *instance = label_instances_->getLabelInstance(label_id);
            if (instance == nullptr)
                continue;
        }
    }
    endResetModel();
}

int ImageLabelsTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || label_instances_ == nullptr)
        return 0;
    return static_cast<int>(label_ids_.size());
}

int ImageLabelsTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(column_headers_.size());
}

QVariant ImageLabelsTableModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= rowCount() || index.column() < 0 || index.column() >= columnCount())
        return QVariant();

    switch (role)
    {
    case DataRole:
        return getData(index);
    default:
        return QVariant();
    }
}

QVariant ImageLabelsTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        if (section < static_cast<int>(column_headers_.size()))
            return column_headers_[section];
    }
    return QVariant();
}

QHash<int, QByteArray> ImageLabelsTableModel::roleNames() const
{
    return {
        {Qt::DisplayRole, "display"},
        {       DataRole,    "data"},
    };
}

void ImageLabelsTableModel::addLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids)
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
    const int row   = rowCount();
    const int count = static_cast<int>(valid_label_ids.size());
    beginInsertRows(QModelIndex(), row, row + count - 1);
    label_ids_.insert(label_ids_.end(), valid_label_ids.begin(), valid_label_ids.end());
    endInsertRows();
}

QVariant ImageLabelsTableModel::getData(const QModelIndex &index) const
{
    const int64_t  label_id = label_ids_[index.row()];
    LabelInstance *instance = label_instances_->getLabelInstance(label_id);
    if (instance == nullptr)
        return QVariant();
    const int col = index.column();
    switch (col)
    {
    case 0:
        return label_classes_->getLabelClassName(instance->labelClassId());
    default:
        return getData(instance, col);
    }
}

QVariant ImageLabelsTableModel::getData(LabelInstance *instance, const int col) const
{
    auto data = instance->data()->dataMap();
    if (col >= static_cast<int>(column_keys_.size()))
        return QVariant();
    return data.value(column_keys_[col], QVariant());
}

} // namespace dltool::project
