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

LabelInstance::~LabelInstance() {}

LabelInstancesListModel::LabelInstancesListModel(data::ProjectDataBase   *database,
                                                 ImageInstancesListModel *image_instances,
                                                 LabelClassesListModel *label_classes, LabelDataFactory factory,
                                                 QObject *parent)
    : QAbstractListModel(parent)
    , database_(database)
    , image_instances_(image_instances)
    , label_classes_(label_classes)
    , factory_(std::move(factory))
    , selection_(new QItemSelectionModel(this))
{
    init();
}

LabelInstancesListModel::~LabelInstancesListModel() {}

void LabelInstancesListModel::init()
{
    if (database_ == nullptr)
    {
        spdlog::error("初始化标注失败: 数据库未初始化");
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
        spdlog::error("查询所有标注失败: 标签数据工厂未初始化");
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
        auto instance = image_instances_->getImageInstance(image_ids[i]);

        LabelData label_data = factory_();
        label_data->fromQVariantMap(data[i], instance->imageRect());
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
        spdlog::error("添加标注失败: 标签ID数量 {} 与图像ID数量 {} 不一致!", label_ids.size(), image_ids.size());
        return;
    }

    for (size_t i = 0; i < label_ids.size(); ++i)
    {
        label_instances_[label_ids[i]]
            = new LabelInstance(label_ids[i], image_ids[i], label_class_ids[i], std::move(labels_data[i]), this);
    }
    std::vector<int64_t> sorted_label_ids(label_ids.begin(), label_ids.end());
    std::reverse(sorted_label_ids.begin(), sorted_label_ids.end());
    beginInsertRows(QModelIndex(), 0, static_cast<int>(sorted_label_ids.size()) - 1);
    label_ids_.insert(label_ids_.end(), sorted_label_ids.begin(), sorted_label_ids.end());
    endInsertRows();
    // TODO: 更新选中状态
    spdlog::info("添加 {} 个标注成功", label_ids.size());
}

void LabelInstancesListModel::deleteLabels(const std::vector<int64_t> &label_ids)
{
    if (database_ == nullptr)
    {
        spdlog::error("添加标注失败: 数据库未初始化");
        return;
    }
    QString err_msg;
    bool    ok = database_->deleteLabels(label_ids, err_msg);
    if (!ok)
    {
        spdlog::error("删除 {} 个标注失败: {}", label_ids.size(), err_msg.toUtf8().constData());
        return;
    }
    for (const auto &label_id : label_ids)
    {
        label_instances_.erase(label_id);
    }
    std::vector<int64_t> new_label_ids;
    new_label_ids.reserve(label_instances_.size());
    for (const auto &[label_id, _] : label_instances_)
    {
        new_label_ids.push_back(label_id);
    }
    std::reverse(new_label_ids.begin(), new_label_ids.end());
    beginResetModel();
    label_ids_ = new_label_ids;
    endResetModel();
    // TODO: 更新选中状态
    spdlog::info("删除 {} 个标注成功", label_ids.size());
}

std::vector<std::vector<int64_t>> LabelInstancesListModel::getImagesLabelIds(
    const std::vector<int64_t> &image_ids) const
{
    std::vector<std::vector<int64_t>> images_label_ids;
    images_label_ids.reserve(image_ids.size());
    for (size_t i = 0; i < image_ids.size(); ++i)
    {
        const int64_t        image_id = image_ids[i];
        std::vector<int64_t> label_ids;
        for (const auto &[label_id, instance] : label_instances_)
        {
            if (instance->imageId() == image_id)
                label_ids.push_back(label_id);
        }
        images_label_ids.push_back(label_ids);
    }
    return images_label_ids;
}

std::vector<int64_t> LabelInstancesListModel::getLabelIds(const std::vector<int64_t> &image_ids) const
{
    std::vector<int64_t> label_ids;
    label_ids.reserve(image_ids.size());
    std::set<int64_t> image_ids_set(image_ids.begin(), image_ids.end());
    for (const auto &[label_id, instance] : label_instances_)
    {
        const int64_t image_id = instance->imageId();
        if (image_ids_set.count(image_id))
            label_ids.push_back(label_id);
    }
    return label_ids;
}

std::vector<int64_t> LabelInstancesListModel::getImageIds(const std::vector<int64_t> &label_ids) const
{
    std::vector<int64_t> image_ids;
    image_ids.reserve(label_ids.size());
    for (const auto &label_id : label_ids)
    {
        auto found = label_instances_.find(label_id);
        if (found == label_instances_.end())
            image_ids.push_back(-1);
        else
            image_ids.push_back(found->second->imageId());
    }
    return image_ids;
}

int LabelInstancesListModel::getLabelId(const QModelIndex &index) const
{
    return label_ids_[index.row()];
}

int LabelInstancesListModel::getImageId(const QModelIndex &index) const
{
    return label_instances_.at(label_ids_[index.row()])->imageId();
}

int LabelInstancesListModel::getLabelClassId(const QModelIndex &index) const
{
    return label_instances_.at(label_ids_[index.row()])->labelClassId();
}

QVariant LabelInstancesListModel::getData(const QModelIndex &index) const
{
    // TODO: 获取标注数据
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
    , selection_(new QItemSelectionModel(this))
{
    init();
}

void ImageLabelsListModel::init()
{
    connect(selection_, &QItemSelectionModel::selectionChanged, this, &ImageLabelsListModel::updateSelection);
}

void ImageLabelsListModel::resetModel()
{
    beginResetModel();
    label_ids_.clear();
    const int                          image_id  = image_instances_->getCurImageId();
    const std::vector<ImageInstance *> instances = image_instances_->getImageInstances({image_id});
    if (!instances.empty() && instances.at(0))
    {
        std::set<int64_t> label_ids = instances.at(0)->labelIds();
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
    case SelectedRole:
        return getSelected(index);
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
        {       SelectedRole,       "selected"},
    };
}

void ImageLabelsListModel::addLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids)
{
    if (image_ids.empty() || label_ids.empty())
        return;
    size_t size = label_ids.size();

    const int64_t cur_image_id = image_instances_->getCurImageId();

    std::vector<int64_t> valid_label_ids;
    valid_label_ids.reserve(size);
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

void ImageLabelsListModel::deleteLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids)
{
    if (image_ids.empty() || label_ids.empty())
        return;
    size_t size = label_ids.size();

    const int64_t cur_image_id = image_instances_->getCurImageId();

    std::vector<int64_t> valid_label_ids;
    valid_label_ids.reserve(size);
    for (size_t i = 0; i < image_ids.size(); ++i)
    {
        if (image_ids[i] == cur_image_id)
            valid_label_ids.push_back(label_ids[i]);
    }
    if (valid_label_ids.empty())
        return;
    beginResetModel();
    std::vector<int64_t> new_label_ids;
    for (const auto &label_id : label_ids_)
    {
        if (std::find(valid_label_ids.begin(), valid_label_ids.end(), label_id) == valid_label_ids.end())
            new_label_ids.push_back(label_id);
    }
    label_ids_ = new_label_ids;
    endResetModel();
}

void ImageLabelsListModel::onCurrentImageChanged()
{
    resetModel();
}

void ImageLabelsListModel::shiftSelect(int current_index, int previous_index,
                                       QItemSelectionModel::SelectionFlags command)
{
    const int top    = std::min(current_index, previous_index);
    const int bottom = std::max(current_index, previous_index);

    QItemSelection selection;
    selection.select(index(top), index(bottom));
    selection_->select(selection, command);
}

void ImageLabelsListModel::selectAll()
{
    QItemSelection selection;
    selection.select(index(0), index(static_cast<int>(label_ids_.size()) - 1));
    selection_->select(selection, QItemSelectionModel::Select | QItemSelectionModel::Rows);
}

void ImageLabelsListModel::updateSelection(const QItemSelection &selected, const QItemSelection &deselected)
{
    const QModelIndexList &dselected_items = deselected.indexes();

    int top{-1};
    int bottom{-1};
    for (const QModelIndex &index : dselected_items)
    {
        const int row = index.row();
        if (top == -1)
            top = row;
        else
            top = std::min(top, row);
        bottom = std::max(bottom, row);
    }
    emit dataChanged(index(top), index(bottom), {SelectedRole});

    top    = -1;
    bottom = -1;

    const QModelIndexList &selected_items = selected.indexes();
    for (const QModelIndex &index : selected_items)
    {
        const int row = index.row();
        if (top == -1)
            top = row;
        else
            top = std::min(top, row);
        bottom = std::max(bottom, row);
    }
    emit dataChanged(index(top), index(bottom), {SelectedRole});
}

int ImageLabelsListModel::getLabelId(const QModelIndex &index) const
{
    return label_ids_[index.row()];
}

int ImageLabelsListModel::getImageId(const QModelIndex &index) const
{
    int64_t        label_id = label_ids_[index.row()];
    LabelInstance *instance = label_instances_->getLabelInstance(label_id);
    if (instance == nullptr)
        return -1;
    return instance->imageId();
}

int ImageLabelsListModel::getLabelClassId(const QModelIndex &index) const
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

QVariant ImageLabelsListModel::getSelected(const QModelIndex &index) const
{
    return selection_->isSelected(index);
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
    , selection_(new QItemSelectionModel(this))
{
    // TODO: 添加列名和数据key
    init();
}

ImageLabelsTableModel::~ImageLabelsTableModel() {}

void ImageLabelsTableModel::init()
{
    connect(selection_, &QItemSelectionModel::selectionChanged, this, &ImageLabelsTableModel::updateSelection);
}

void ImageLabelsTableModel::resetModel()
{
    beginResetModel();
    label_ids_.clear();
    const int                          image_id  = image_instances_->getCurImageId();
    const std::vector<ImageInstance *> instances = image_instances_->getImageInstances({image_id});
    if (!instances.empty() && instances.at(0))
    {
        std::set<int64_t> label_ids = instances.at(0)->labelIds();
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
    case SelectedRole:
        return getSelected(index);
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
        {Qt::DisplayRole,  "display"},
        {       DataRole,     "data"},
        {   SelectedRole, "selected"},
    };
}

void ImageLabelsTableModel::addLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids)
{
    if (image_ids.empty() || label_ids.empty())
        return;
    size_t size = label_ids.size();

    const int64_t cur_image_id = image_instances_->getCurImageId();

    std::vector<int64_t> valid_label_ids;
    valid_label_ids.reserve(size);
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

void ImageLabelsTableModel::deleteLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids)
{
    if (image_ids.empty() || label_ids.empty())
        return;
    size_t size = label_ids.size();

    const int64_t cur_image_id = image_instances_->getCurImageId();

    std::vector<int64_t> valid_label_ids;
    valid_label_ids.reserve(size);
    for (size_t i = 0; i < image_ids.size(); ++i)
    {
        if (image_ids[i] == cur_image_id)
            valid_label_ids.push_back(label_ids[i]);
    }
    if (valid_label_ids.empty())
        return;
    beginResetModel();
    std::vector<int64_t> new_label_ids;
    for (const auto &label_id : label_ids_)
    {
        if (std::find(valid_label_ids.begin(), valid_label_ids.end(), label_id) == valid_label_ids.end())
            new_label_ids.push_back(label_id);
    }
    label_ids_ = new_label_ids;
    endResetModel();
}

void ImageLabelsTableModel::onCurrentImageChanged()
{
    resetModel();
}

std::vector<int64_t> ImageLabelsTableModel::getSelectedLabelIds() const
{
    std::vector<int64_t> label_ids;
    for (const auto &selected : selection_->selectedRows())
    {
        const int row = selected.row();
        if (row < 0 || row >= static_cast<int>(label_ids_.size()))
            continue;
        label_ids.push_back(label_ids_[row]);
    }
    return label_ids;
}

void ImageLabelsTableModel::shiftSelect(int current_index, int previous_index,
                                        QItemSelectionModel::SelectionFlags command)
{
    const int top    = std::min(current_index, previous_index);
    const int bottom = std::max(current_index, previous_index);

    QItemSelection selection;
    selection.select(index(top, 0), index(bottom, static_cast<int>(column_headers_.size()) - 1));
    selection_->select(selection, command);
}

void ImageLabelsTableModel::selectAll()
{
    QItemSelection    selection;
    const QModelIndex top_left = index(0, 0);
    const QModelIndex bottom_right
        = index(static_cast<int>(label_ids_.size()) - 1, static_cast<int>(column_headers_.size()) - 1);
    selection.select(top_left, bottom_right);
    selection_->select(selection, QItemSelectionModel::Select | QItemSelectionModel::Rows);
}

void ImageLabelsTableModel::setLastIndex(int last_index)
{
    if (last_index_ != last_index)
    {
        last_index_ = last_index;
        emit lastSelectedIndexChanged();
    }
}

void ImageLabelsTableModel::updateSelection(const QItemSelection &selected, const QItemSelection &deselected)
{
    const QModelIndexList &dselected_items = deselected.indexes();

    int top{-1}, left{-1};
    int bottom{-1}, right{-1};
    for (const QModelIndex &index : dselected_items)
    {
        const int row = index.row();
        const int col = index.column();
        if (top == -1)
        {
            top    = row;
            left   = col;
            bottom = row;
            right  = col;
        }
        else
        {
            top    = std::min(top, row);
            left   = std::min(left, col);
            bottom = std::max(bottom, row);
            right  = std::max(right, col);
        }
    }
    emit dataChanged(index(top, left), index(bottom, right), {SelectedRole});

    top    = -1;
    bottom = -1;
    left   = -1;
    right  = -1;

    const QModelIndexList &selected_items = selected.indexes();
    for (const QModelIndex &index : selected_items)
    {
        const int row = index.row();
        const int col = index.column();
        if (top == -1)
        {
            top    = row;
            left   = col;
            bottom = row;
            right  = col;
        }
        else
        {
            top    = std::min(top, row);
            left   = std::min(left, col);
            bottom = std::max(bottom, row);
            right  = std::max(right, col);
        }
    }
    emit dataChanged(index(top, left), index(bottom, right), {SelectedRole});
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
        return getClassData(instance);
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

QVariant ImageLabelsTableModel::getClassData(LabelInstance *instance) const
{
    const int64_t class_id = instance->labelClassId();
    QVariantMap   data{
          { "class_name",  label_classes_->getLabelClassName(class_id)},
          {"class_color", label_classes_->getLabelClassColor(class_id)},
    };
    return data;
}

QVariant ImageLabelsTableModel::getSelected(const QModelIndex &index) const
{
    return selection_->isSelected(index);
}

} // namespace dltool::project
