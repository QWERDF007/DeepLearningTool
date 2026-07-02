#include "data/Labels.h"

#include "data/Images.h"
#include "data/LabelClasses.h"
#include "data/LabelData.h"
#include "database/DataBase.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <iterator>
#include <set>

namespace dltool::data {

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

LabelInstancesListModel::LabelInstancesListModel(dltool::database::ProjectDataBase *database,
                                                 ImageInstancesListModel           *image_instances,
                                                 LabelClassesListModel             *label_classes,
                                                 LabelDataHelper label_data_helper, bool load_from_database,
                                                 QObject *parent)
    : QAbstractListModel(parent)
    , database_(database)
    , image_instances_(image_instances)
    , label_classes_(label_classes)
    , label_data_helper_(std::move(label_data_helper))
    , selection_(new QItemSelectionModel(this))
{
    init(load_from_database);
}

LabelInstancesListModel::~LabelInstancesListModel() {}

void LabelInstancesListModel::init(bool load_from_database)
{
    if (load_from_database)
    {
        loadLabelsFromDatabase();
    }
}

void LabelInstancesListModel::loadLabelsFromDatabase()
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
    if (label_data_helper_ == nullptr)
    {
        spdlog::error("查询所有标注失败: 标签数据工厂未初始化");
        return;
    }
    std::vector<LoadedLabelInstance> labels;
    labels.reserve(label_ids.size());
    for (size_t i = 0; i < label_ids.size(); ++i)
    {
        LabelData data = label_data_helper_->createLabelData();
        data->fromBlob(labels_data[i]);

        LoadedLabelInstance label;
        label.label_id       = label_ids[i];
        label.image_id       = image_ids[i];
        label.label_class_id = label_class_ids[i];
        label.data           = std::move(data);
        labels.push_back(std::move(label));
    }
    replaceAllLabels(std::move(labels));
}

void LabelInstancesListModel::replaceAllLabels(std::vector<LoadedLabelInstance> labels)
{
    if (selection_)
    {
        selection_->clear();
    }

    beginResetModel();
    for (auto &[_, instance] : full_label_instances_)
    {
        delete instance;
    }
    full_label_instances_.clear();
    label_ids_.clear();
    filtered_label_ids_.clear();
    is_filtered_ = false;
    last_index_  = -1;

    std::sort(labels.begin(), labels.end(), [](const LoadedLabelInstance &left, const LoadedLabelInstance &right)
              { return left.label_id > right.label_id; });

    label_ids_.reserve(labels.size());
    for (LoadedLabelInstance &label : labels)
    {
        label_ids_.push_back(label.label_id);
        full_label_instances_[label.label_id]
            = new LabelInstance(label.label_id, label.image_id, label.label_class_id, std::move(label.data), this);
    }
    endResetModel();
    emit lastIndexChanged();
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
    case LabelClassNameRole:
        return getLabelClassName(index);
    case LabelClassColorRole:
        return getLabelClassColor(index);
    case DataRole:
        return getData(index);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> LabelInstancesListModel::roleNames() const
{
    return {
        {        LabelIdRole,          "label_id"},
        {        ImageIdRole,          "image_id"},
        {   LabelClassIdRole,    "label_class_id"},
        { LabelClassNameRole,  "label_class_name"},
        {LabelClassColorRole, "label_class_color"},
        {           DataRole,              "data"},
    };
}

LabelInstance *LabelInstancesListModel::getLabelInstance(const int64_t label_id)
{
    auto found = full_label_instances_.find(label_id);
    if (found == full_label_instances_.end())
        return nullptr;
    return found->second;
}

bool LabelInstancesListModel::tryAddLabels(std::vector<int64_t> &label_ids, const std::vector<int64_t> &image_ids,
                                           const std::vector<int64_t>     &label_class_ids,
                                           const std::vector<QVariantMap> &data, QString *err_msg)
{
    label_ids.clear();

    if (database_ == nullptr || image_instances_ == nullptr || label_data_helper_ == nullptr)
    {
        const QString message = QString("数据库、图像列表或标签数据工厂未初始化");
        if (err_msg != nullptr)
        {
            *err_msg = message;
        }
        spdlog::error("添加标注失败: {}", message.toUtf8().constData());
        return false;
    }

    if (image_ids.size() != label_class_ids.size() || image_ids.size() != data.size())
    {
        const QString message = QString("image_ids=%1, label_class_ids=%2, data=%3 数量不一致")
                                    .arg(image_ids.size())
                                    .arg(label_class_ids.size())
                                    .arg(data.size());
        if (err_msg != nullptr)
        {
            *err_msg = message;
        }
        spdlog::error("添加标注失败: {}", message.toUtf8().constData());
        return false;
    }

    for (const int64_t image_id : image_ids)
    {
        if (image_instances_->getImageInstance(image_id) == nullptr)
        {
            const QString message = QString("图像实例不存在, image_id=%1").arg(image_id);
            if (err_msg != nullptr)
            {
                *err_msg = message;
            }
            spdlog::error("添加标注失败: {}", message.toUtf8().constData());
            return false;
        }
    }

    addLabels(label_ids, image_ids, label_class_ids, data, err_msg);
    if (label_ids.size() != image_ids.size())
    {
        const QString message
            = QString("写入后的标签 ID 数量 %1 与图像 ID 数量 %2 不一致").arg(label_ids.size()).arg(image_ids.size());
        if (err_msg != nullptr && err_msg->isEmpty())
        {
            *err_msg = message;
        }
        spdlog::error("添加标注失败: {}", message.toUtf8().constData());
        label_ids.clear();
        return false;
    }

    return true;
}

void LabelInstancesListModel::addLabels(std::vector<int64_t> &label_ids, const std::vector<int64_t> &image_ids,
                                        const std::vector<int64_t>     &label_class_ids,
                                        const std::vector<QVariantMap> &data, QString *err_msg)
{
    label_ids.clear();

    if (database_ == nullptr)
    {
        const QString message = QString("数据库未初始化");
        if (err_msg != nullptr)
        {
            *err_msg = message;
        }
        spdlog::error("添加标注失败: {}", message.toUtf8().constData());
        return;
    }
    if (image_instances_ == nullptr)
    {
        const QString message = QString("图像实例列表未初始化");
        if (err_msg != nullptr)
        {
            *err_msg = message;
        }
        spdlog::error("添加标注失败: {}", message.toUtf8().constData());
        return;
    }

    if (label_data_helper_ == nullptr)
    {
        const QString message = QString("标签数据工厂未初始化");
        if (err_msg != nullptr)
        {
            *err_msg = message;
        }
        spdlog::error("添加标注失败: {}", message.toUtf8().constData());
        return;
    }

    if (image_ids.size() != label_class_ids.size() || image_ids.size() != data.size())
    {
        const QString message = QString("image_ids=%1, label_class_ids=%2, data=%3 数量不一致")
                                    .arg(image_ids.size())
                                    .arg(label_class_ids.size())
                                    .arg(data.size());
        if (err_msg != nullptr)
        {
            *err_msg = message;
        }
        spdlog::error("添加标注失败: {}", message.toUtf8().constData());
        return;
    }

    if (image_ids.empty())
    {
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
        if (instance == nullptr)
        {
            const QString message = QString("图像实例不存在, image_id=%1").arg(image_ids[i]);
            if (err_msg != nullptr)
            {
                *err_msg = message;
            }
            spdlog::error("添加标注失败: {}", message.toUtf8().constData());
            label_ids.clear();
            return;
        }

        LabelData label_data = label_data_helper_->createLabelData();
        if (label_data == nullptr)
        {
            const QString message = QString("标签数据创建失败, image_id=%1").arg(image_ids[i]);
            if (err_msg != nullptr)
            {
                *err_msg = message;
            }
            spdlog::error("添加标注失败: {}", message.toUtf8().constData());
            label_ids.clear();
            return;
        }
        label_data->fromQVariantMap(data[i], instance->imageRect());
        label_types.push_back(label_data->type());
        labels_data_blob.push_back(label_data->toBlob());
        labels_data.push_back(std::move(label_data));
    }

    QString db_err_msg;
    bool    ok = database_->addLabels(image_ids, label_class_ids, label_types, labels_data_blob, label_ids, db_err_msg);
    if (!ok)
    {
        if (err_msg != nullptr)
        {
            *err_msg = db_err_msg;
        }
        spdlog::error("添加标注失败: {}", db_err_msg.toUtf8().constData());
        return;
    }
    if (label_ids.size() != image_ids.size())
    {
        const QString message
            = QString("标签ID数量 %1 与图像ID数量 %2 不一致").arg(label_ids.size()).arg(image_ids.size());
        if (err_msg != nullptr)
        {
            *err_msg = message;
        }
        spdlog::error("添加标注失败: {}", message.toUtf8().constData());
        return;
    }

    for (size_t i = 0; i < label_ids.size(); ++i)
    {
        full_label_instances_[label_ids[i]]
            = new LabelInstance(label_ids[i], image_ids[i], label_class_ids[i], std::move(labels_data[i]), this);
    }
    std::vector<int64_t> sorted_label_ids(label_ids.begin(), label_ids.end());
    std::reverse(sorted_label_ids.begin(), sorted_label_ids.end());
    beginInsertRows(QModelIndex(), 0, static_cast<int>(sorted_label_ids.size()) - 1);
    label_ids_.insert(label_ids_.begin(), sorted_label_ids.begin(), sorted_label_ids.end());
    endInsertRows();
    // TODO: 更新选中状态
    spdlog::info("添加 {} 个标注成功", label_ids.size());
}

void LabelInstancesListModel::updateLabelsData(const std::vector<int64_t>     &label_ids,
                                               const std::vector<int64_t>     &image_ids,
                                               const std::vector<QVariantMap> &data)
{
    if (database_ == nullptr)
    {
        spdlog::error("更新标注失败: 数据库未初始化");
        return;
    }
    if (image_instances_ == nullptr)
    {
        spdlog::error("更新标注失败: 图像实例列表未初始化");
        return;
    }

    std::vector<std::vector<uint8_t>> labels_data_blob;
    labels_data_blob.reserve(label_ids.size());
    for (size_t i = 0; i < label_ids.size(); ++i)
    {
        auto instance = image_instances_->getImageInstance(image_ids[i]);

        const LabelData &label_data = full_label_instances_[label_ids[i]]->data();
        label_data->fromQVariantMap(data[i], instance->imageRect());
        labels_data_blob.push_back(label_data->toBlob());
    }

    QString err_msg;
    bool    ok = database_->updateLabelsData(label_ids, labels_data_blob, err_msg);
    if (!ok)
    {
        spdlog::error("更新标注数据失败: {}", err_msg.toUtf8().constData());
        return;
    }
    beginResetModel();
    // TODO: 只刷新需要更新的数据, 而不是全部数据
    endResetModel();
    spdlog::info("更新 {} 个标注数据成功", label_ids.size());
}

void LabelInstancesListModel::updateLabelsClass(const std::vector<int64_t> &label_ids,
                                                const std::vector<int64_t> &label_class_ids)
{
    if (database_ == nullptr)
    {
        spdlog::error("更新标注类别失败: 数据库未初始化");
        return;
    }
    if (label_ids.size() != label_class_ids.size())
    {
        spdlog::error("更新标注类别失败: 标签ID数量 {} 与类别ID数量 {} 不一致!", label_ids.size(),
                      label_class_ids.size());
        return;
    }

    QString err_msg;
    bool    ok = database_->updateLabelsClass(label_ids, label_class_ids, err_msg);
    if (!ok)
    {
        spdlog::error("更新标注类别失败: {}", err_msg.toUtf8().constData());
        return;
    }

    // 更新内存中的标注实例
    for (size_t i = 0; i < label_ids.size(); ++i)
    {
        auto found = full_label_instances_.find(label_ids[i]);
        if (found != full_label_instances_.end())
        {
            found->second->setLabelClassId(label_class_ids[i]);
        }
    }

    beginResetModel();
    endResetModel();
    spdlog::info("更新 {} 个标注类别成功", label_ids.size());
}

void LabelInstancesListModel::labelClassUpdated(const int64_t label_class_id)
{
    // TODO:
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
    const std::set<int64_t> deleted_label_ids(label_ids.begin(), label_ids.end());

    beginResetModel();

    for (const auto &label_id : deleted_label_ids)
    {
        auto found = full_label_instances_.find(label_id);
        if (found == full_label_instances_.end())
            continue;

        delete found->second;
        full_label_instances_.erase(found);
    }

    if (is_filtered_)
    {
        filtered_label_ids_.erase(
            std::remove_if(filtered_label_ids_.begin(), filtered_label_ids_.end(),
                           [&deleted_label_ids](int64_t label_id)
                           { return deleted_label_ids.find(label_id) != deleted_label_ids.end(); }),
            filtered_label_ids_.end());
        label_ids_ = filtered_label_ids_;
    }
    else
    {
        label_ids_.clear();
        label_ids_.reserve(full_label_instances_.size());
        for (const auto &[label_id, _] : full_label_instances_)
        {
            label_ids_.push_back(label_id);
        }
        std::reverse(label_ids_.begin(), label_ids_.end());
    }

    if (selection_)
        selection_->clear();
    last_index_ = -1;

    endResetModel();
    emit lastIndexChanged();
    spdlog::info("删除 {} 个标注成功", label_ids.size());
}

std::vector<int64_t> LabelInstancesListModel::getImageLabelIds(int64_t image_id) const
{
    std::vector<int64_t> label_ids;
    for (const auto &[label_id, instance] : full_label_instances_)
    {
        if (instance != nullptr && instance->imageId() == image_id)
        {
            label_ids.push_back(label_id);
        }
    }
    return label_ids;
}

std::vector<std::vector<int64_t>> LabelInstancesListModel::getImagesLabelIds(
    const std::vector<int64_t> &image_ids) const
{
    std::map<int64_t, std::vector<int64_t>> labels_by_image_id;
    for (const auto &[label_id, instance] : full_label_instances_)
    {
        if (instance != nullptr)
        {
            labels_by_image_id[instance->imageId()].push_back(label_id);
        }
    }

    std::vector<std::vector<int64_t>> images_label_ids;
    images_label_ids.reserve(image_ids.size());
    for (const int64_t image_id : image_ids)
    {
        auto found = labels_by_image_id.find(image_id);
        if (found == labels_by_image_id.end())
        {
            images_label_ids.emplace_back();
            continue;
        }
        images_label_ids.push_back(found->second);
    }
    return images_label_ids;
}

int64_t LabelInstancesListModel::getImageId(const int64_t label_id) const
{
    auto found = full_label_instances_.find(label_id);
    if (found == full_label_instances_.end())
        return -1;
    return found->second->imageId();
}

std::vector<int64_t> LabelInstancesListModel::getImageIds(const std::vector<int64_t> &label_ids) const
{
    std::vector<int64_t> image_ids;
    image_ids.reserve(label_ids.size());
    for (const auto &label_id : label_ids)
    {
        image_ids.push_back(getImageId(label_id));
    }
    return image_ids;
}

std::vector<int64_t> LabelInstancesListModel::getLabelIds(const int64_t label_class_id) const
{
    std::vector<int64_t> label_ids;
    label_ids.reserve(full_label_instances_.size());
    for (const auto &[label_id, instance] : full_label_instances_)
    {
        if (instance->labelClassId() == label_class_id)
            label_ids.push_back(label_id);
    }
    return label_ids;
}

int64_t LabelInstancesListModel::getLabelClassId(const int64_t label_id) const
{
    auto found = full_label_instances_.find(label_id);
    if (found == full_label_instances_.end())
        return -1;
    return found->second->labelClassId();
}

std::vector<int64_t> LabelInstancesListModel::getLabelClassIds(const std::vector<int64_t> &label_ids) const
{
    std::vector<int64_t> label_class_ids;
    label_class_ids.reserve(label_ids.size());
    for (const auto &label_id : label_ids)
    {
        label_class_ids.push_back(getLabelClassId(label_id));
    }
    return label_class_ids;
}

int LabelInstancesListModel::getLabelId(const QModelIndex &index) const
{
    return label_ids_[index.row()];
}

int LabelInstancesListModel::getImageId(const QModelIndex &index) const
{
    return full_label_instances_.at(label_ids_[index.row()])->imageId();
}

int LabelInstancesListModel::getLabelClassId(const QModelIndex &index) const
{
    return getLabelClassId(label_ids_[index.row()]);
}

QString LabelInstancesListModel::getLabelClassName(const QModelIndex &index) const
{
    if (!label_classes_)
        return QString();

    int64_t label_class_id = getLabelClassId(label_ids_[index.row()]);
    return label_classes_->getLabelClassName(label_class_id);
}

QString LabelInstancesListModel::getLabelClassColor(const QModelIndex &index) const
{
    if (!label_classes_)
        return QString();

    int64_t label_class_id = getLabelClassId(label_ids_[index.row()]);
    return label_classes_->getLabelClassColor(label_class_id);
}

QVariant LabelInstancesListModel::getData(const QModelIndex &index) const
{
    if (index.row() < 0 || index.row() >= rowCount())
        return QVariant();
    int64_t label_id = label_ids_[index.row()];
    auto    found    = full_label_instances_.find(label_id);
    if (found == full_label_instances_.end())
        return QVariant();
    return found->second->data()->dataMap();
}

void LabelInstancesListModel::applyFilter(const std::function<bool(int64_t)> &image_filter_func)
{
    applyFilter(image_filter_func, std::function<bool(int64_t)>(), std::function<bool(int64_t)>());
}

void LabelInstancesListModel::applyFilter(const std::function<bool(int64_t)> &image_filter_func,
                                          const std::function<bool(int64_t)> &label_class_filter_func)
{
    applyFilter(image_filter_func, label_class_filter_func, std::function<bool(int64_t)>());
}

void LabelInstancesListModel::applyFilter(const std::function<bool(int64_t)> &image_filter_func,
                                          const std::function<bool(int64_t)> &label_class_filter_func,
                                          const std::function<bool(int64_t)> &label_filter_func)
{
    if (!image_filter_func && !label_class_filter_func && !label_filter_func)
    {
        qWarning() << "applyFilter called with null filter function";
        return;
    }

    rebuildFilteredList(image_filter_func, label_class_filter_func, label_filter_func);
    is_filtered_ = true;

    beginResetModel();
    label_ids_ = filtered_label_ids_;
    endResetModel();

    // Handle selection state after filtering
    if (rowCount() == 0)
    {
        // No labels after filtering, clear selection
        selection_->clear();
    }
}

void LabelInstancesListModel::clearFilter()
{
    if (!is_filtered_)
    {
        return; // No filter active, nothing to clear
    }

    is_filtered_ = false;
    filtered_label_ids_.clear();

    beginResetModel();
    // Restore full list
    label_ids_.clear();
    label_ids_.reserve(full_label_instances_.size());
    for (const auto &[label_id, _] : full_label_instances_)
    {
        label_ids_.push_back(label_id);
    }
    std::reverse(label_ids_.begin(), label_ids_.end());
    endResetModel();
}

void LabelInstancesListModel::rebuildFilteredList(const std::function<bool(int64_t)> &image_filter_func,
                                                  const std::function<bool(int64_t)> &label_class_filter_func,
                                                  const std::function<bool(int64_t)> &label_filter_func)
{
    filtered_label_ids_.clear();
    filtered_label_ids_.reserve(full_label_instances_.size());

    // Iterate through full_label_instances_ and apply filter based on image_id / label_class_id
    for (const auto &[label_id, instance] : full_label_instances_)
    {
        const int64_t image_id       = instance->imageId();
        const int64_t label_class_id = instance->labelClassId();

        const bool image_ok = image_filter_func ? image_filter_func(image_id) : true;
        const bool class_ok = label_class_filter_func ? label_class_filter_func(label_class_id) : true;
        const bool label_ok = label_filter_func ? label_filter_func(label_id) : true;

        if (image_ok && class_ok && label_ok)
        {
            filtered_label_ids_.push_back(label_id);
        }
    }

    // Maintain reverse order (newest first)
    std::reverse(filtered_label_ids_.begin(), filtered_label_ids_.end());
}

void LabelInstancesListModel::shiftSelect(int current_index, int previous_index,
                                          QItemSelectionModel::SelectionFlags command)
{
    const int top    = std::min(current_index, previous_index);
    const int bottom = std::max(current_index, previous_index);

    QItemSelection selection;
    selection.select(index(top), index(bottom));
    selection_->select(selection, command);
}

void LabelInstancesListModel::selectAll()
{
    if (rowCount() <= 0)
        return;

    QItemSelection selection;
    selection.select(index(0), index(rowCount() - 1));
    selection_->select(selection, QItemSelectionModel::Select);
}

std::vector<int64_t> LabelInstancesListModel::getSelectedLabelIds() const
{
    std::vector<int64_t> label_ids;
    if (!selection_)
        return label_ids;

    const QModelIndexList selected_indexes = selection_->selectedIndexes();
    label_ids.reserve(selected_indexes.size());
    for (const QModelIndex &selected_index : selected_indexes)
    {
        const int row = selected_index.row();
        if (row < 0 || row >= static_cast<int>(label_ids_.size()))
            continue;
        label_ids.push_back(label_ids_[row]);
    }
    return label_ids;
}

std::vector<int64_t> LabelInstancesListModel::getSelectedImageIds() const
{
    std::vector<int64_t> image_ids = getImageIds(getSelectedLabelIds());
    image_ids.erase(std::remove_if(image_ids.begin(), image_ids.end(), [](const int64_t image_id) { return image_id < 0; }),
                    image_ids.end());
    std::sort(image_ids.begin(), image_ids.end());
    image_ids.erase(std::unique(image_ids.begin(), image_ids.end()), image_ids.end());
    return image_ids;
}

void LabelInstancesListModel::setLastIndex(int last_index)
{
    if (last_index_ != last_index)
    {
        last_index_ = last_index;
        emit lastIndexChanged();
    }
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
    hovered_indices_.clear();

    if (image_instances_ != nullptr && label_instances_ != nullptr)
    {
        const int64_t image_id = image_instances_->getCurrentImageId();
        if (image_id >= 0)
        {
            label_ids_ = label_instances_->getImageLabelIds(image_id);
        }
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
    case HoveredRole:
        return getHovered(index);
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
        {        HoveredRole,        "hovered"},
    };
}

void ImageLabelsListModel::addLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids)
{
    if (image_ids.empty() || label_ids.empty())
        return;
    size_t size = label_ids.size();

    const int64_t current_image_id = image_instances_->getCurrentImageId();

    std::vector<int64_t> valid_label_ids;
    valid_label_ids.reserve(size);
    for (size_t i = 0; i < image_ids.size(); ++i)
    {
        if (image_ids[i] == current_image_id)
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

void ImageLabelsListModel::updateLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids)
{
    if (image_ids.empty() || label_ids.empty())
        return;
    size_t size = label_ids.size();

    const int64_t current_image_id = image_instances_->getCurrentImageId();

    std::vector<int64_t> valid_label_ids;
    valid_label_ids.reserve(size);
    for (size_t i = 0; i < image_ids.size(); ++i)
    {
        if (image_ids[i] == current_image_id)
            valid_label_ids.push_back(label_ids[i]);
    }
    if (valid_label_ids.empty())
        return;

    for (const int64_t label_id : valid_label_ids)
    {
        const auto label_it = std::find(label_ids_.begin(), label_ids_.end(), label_id);
        if (label_it == label_ids_.end())
        {
            continue;
        }

        const int row = static_cast<int>(std::distance(label_ids_.begin(), label_it));
        emit      dataChanged(index(row), index(row), {DataRole, LabelClassIdRole, LabelClassColorRole});
    }
}

void ImageLabelsListModel::deleteLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids)
{
    if (image_ids.empty() || label_ids.empty())
        return;
    size_t size = label_ids.size();

    const int64_t current_image_id = image_instances_->getCurrentImageId();

    std::vector<int64_t> valid_label_ids;
    valid_label_ids.reserve(size);
    for (size_t i = 0; i < image_ids.size(); ++i)
    {
        if (image_ids[i] == current_image_id)
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
    hovered_indices_.clear();
    endResetModel();
}

void ImageLabelsListModel::labelClassUpdated(const int64_t label_class_id)
{
    if (label_ids_.empty())
    {
        return;
    }

    int top    = -1;
    int bottom = -1;
    for (int row = 0; row < static_cast<int>(label_ids_.size()); ++row)
    {
        LabelInstance *instance = label_instances_->getLabelInstance(label_ids_[row]);
        if (instance == nullptr || instance->labelClassId() != label_class_id)
        {
            continue;
        }

        top    = top == -1 ? row : std::min(top, row);
        bottom = std::max(bottom, row);
    }

    if (top >= 0)
    {
        emit dataChanged(index(top), index(bottom), {LabelClassColorRole});
    }
}

void ImageLabelsListModel::onCurrentImageChanged()
{
    resetModel();
}

std::vector<int64_t> ImageLabelsListModel::getSelectedLabelIds() const
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

QVariantMap ImageLabelsListModel::getData(const int index) const
{
    if (index < 0 || index >= static_cast<int>(label_ids_.size()))
        return QVariantMap();
    int64_t        label_id = label_ids_[index];
    LabelInstance *instance = label_instances_->getLabelInstance(label_id);
    if (instance == nullptr || instance->data() == nullptr)
        return QVariantMap();
    auto data              = instance->data()->dataMap();
    data["label_id"]       = static_cast<qlonglong>(label_id);
    data["label_class_id"] = static_cast<qlonglong>(instance->labelClassId());
    data["color"]          = label_classes_->getLabelClassColor(instance->labelClassId());
    data["index"]          = index;
    return data;
}

QVariantMap ImageLabelsListModel::getEditedData(const QVariantMap &data, const QPointF &start, const QPointF &end)
{
    if (label_instances_ == nullptr || label_instances_->helper() == nullptr)
        return QVariantMap();
    auto image_instance = image_instances_->getImageInstance(image_instances_->getCurrentImageId());
    if (image_instance == nullptr)
        return QVariantMap();
    return label_instances_->helper()->getEditedData(data, start, end, image_instance->imageRect());
}

std::vector<int> ImageLabelsListModel::getIndicesAt(const QPointF &pos) const
{
    std::vector<int> indices;
    if (label_ids_.empty())
        return indices;
    auto image_instance = image_instances_->getImageInstance(image_instances_->getCurrentImageId());
    if (image_instance == nullptr)
        return indices;
    QRectF image_rect = image_instance->imageRect();
    if (!image_rect.contains(pos))
        return indices;
    if (label_instances_ == nullptr || label_instances_->helper() == nullptr)
        return indices;
    for (int i = 0; i < static_cast<int>(label_ids_.size()); ++i)
    {
        auto label_instance = label_instances_->getLabelInstance(label_ids_[i]);
        if (label_instance == nullptr)
            continue;
        if (label_instances_->helper()->isInside(pos, label_instance->data()))
        {
            indices.push_back(i);
        }
    }
    return indices;
}

int ImageLabelsListModel::getTopSelectedIndex() const
{
    int index = -1;
    for (const auto &selected : selection_->selectedRows())
    {
        index = std::max(index, selected.row());
    }
    return index;
}

QModelIndex ImageLabelsListModel::chooseIndex(const std::vector<int> &indices) const
{
    if (indices.empty())
        return QModelIndex();
    int size = static_cast<int>(indices.size());
    for (int i = 0; i < size; ++i)
    {
        if (selection_->isSelected(index(indices[i])))
        {
            int next_index = (i + 1) % size;
            return index(indices[next_index]);
        }
    }
    return index(indices[0]);
}

bool ImageLabelsListModel::isInside(const QPointF &pos, const int index) const
{
    if (index < 0 || index >= static_cast<int>(label_ids_.size()))
        return false;
    if (label_instances_ == nullptr || label_instances_->helper() == nullptr)
        return false;
    auto label_instance = label_instances_->getLabelInstance(label_ids_[index]);
    if (label_instance == nullptr)
        return false;
    return label_instances_->helper()->isInside(pos, label_instance->data());
}

QVariantMap ImageLabelsListModel::hitTestHandle(const QPointF &pos, const int index, const double scale) const
{
    if (index < 0 || index >= static_cast<int>(label_ids_.size()))
    {
        return QVariantMap{
            {    "found", false},
            {"direction",    ""}
        };
    }
    if (label_instances_ == nullptr || label_instances_->helper() == nullptr)
    {
        return QVariantMap{
            {    "found", false},
            {"direction",    ""}
        };
    }
    auto label_instance = label_instances_->getLabelInstance(label_ids_[index]);
    if (label_instance == nullptr)
    {
        return QVariantMap{
            {    "found", false},
            {"direction",    ""}
        };
    }
    return label_instances_->helper()->hitTestHandle(pos, label_instance->data(), scale);
}

void ImageLabelsListModel::setHovered(const std::vector<int> &indices)
{
    std::set<int> new_hovered_indices(indices.begin(), indices.end());
    if (hovered_indices_ == new_hovered_indices)
        return;

    std::vector<int> changed_indices;
    std::set_symmetric_difference(hovered_indices_.begin(), hovered_indices_.end(), new_hovered_indices.begin(),
                                  new_hovered_indices.end(), std::back_inserter(changed_indices));

    hovered_indices_ = new_hovered_indices;

    for (const int row : changed_indices)
    {
        if (row >= 0 && row < static_cast<int>(label_ids_.size()))
        {
            emit dataChanged(index(row), index(row), {HoveredRole});
        }
    }
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
    if (label_ids_.empty())
    {
        return;
    }

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
    if (top >= 0 && bottom >= top)
    {
        emit dataChanged(index(top), index(bottom), {SelectedRole});
    }

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
    if (top >= 0 && bottom >= top)
    {
        emit dataChanged(index(top), index(bottom), {SelectedRole});
    }
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
    return getData(index.row());
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

QVariant ImageLabelsListModel::getHovered(const QModelIndex &index) const
{
    const int row = index.row();
    return hovered_indices_.find(row) != hovered_indices_.end();
}

ImageLabelsTableModel::ImageLabelsTableModel(ImageInstancesListModel *image_instances,
                                             LabelInstancesListModel *label_instances,
                                             LabelClassesListModel *label_classes, QObject *parent)
    : QAbstractTableModel(parent)
    , image_instances_(image_instances)
    , label_instances_(label_instances)
    , label_classes_(label_classes)
    , selection_(new QItemSelectionModel(this))
{
    // TODO: 添加列名和数据key
    init();
}

ImageLabelsTableModel::~ImageLabelsTableModel() {}

void ImageLabelsTableModel::init()
{
    connect(selection_, &QItemSelectionModel::selectionChanged, this, &ImageLabelsTableModel::updateSelection);
    if (label_instances_ == nullptr || label_instances_->helper() == nullptr)
    {
        column_headers_.clear();
        column_keys_.clear();
        spdlog::warn("init image labels table without label data helper");
        return;
    }
    auto data       = label_instances_->helper()->dataColumns();
    column_headers_ = data.first;
    column_keys_    = data.second;
}

void ImageLabelsTableModel::resetModel()
{
    beginResetModel();
    label_ids_.clear();

    if (image_instances_ != nullptr && label_instances_ != nullptr)
    {
        const int64_t image_id = image_instances_->getCurrentImageId();
        if (image_id >= 0)
        {
            label_ids_ = label_instances_->getImageLabelIds(image_id);
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
    case ClassDataRole:
        return getClassData(index);
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
        {Qt::DisplayRole,    "display"},
        {       DataRole,       "data"},
        {  ClassDataRole, "class_data"},
        {   SelectedRole,   "selected"},
    };
}

void ImageLabelsTableModel::addLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids)
{
    if (image_ids.empty() || label_ids.empty())
        return;
    size_t size = label_ids.size();

    const int64_t current_image_id = image_instances_->getCurrentImageId();

    std::vector<int64_t> valid_label_ids;
    valid_label_ids.reserve(size);
    for (size_t i = 0; i < image_ids.size(); ++i)
    {
        if (image_ids[i] == current_image_id)
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

void ImageLabelsTableModel::updateLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids)
{
    if (image_ids.empty() || label_ids.empty())
        return;
    size_t size = label_ids.size();

    const int64_t current_image_id = image_instances_->getCurrentImageId();

    std::vector<int64_t> valid_label_ids;
    valid_label_ids.reserve(size);
    for (size_t i = 0; i < image_ids.size(); ++i)
    {
        if (image_ids[i] == current_image_id)
            valid_label_ids.push_back(label_ids[i]);
    }
    if (valid_label_ids.empty())
        return;
    if (label_ids_.empty() || column_headers_.empty())
        return;
    emit dataChanged(index(0, 0),
                     index(static_cast<int>(label_ids_.size()) - 1, static_cast<int>(column_headers_.size()) - 1),
                     {DataRole, ClassDataRole});
}

void ImageLabelsTableModel::deleteLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_ids)
{
    if (image_ids.empty() || label_ids.empty())
        return;
    size_t size = label_ids.size();

    const int64_t current_image_id = image_instances_->getCurrentImageId();

    std::vector<int64_t> valid_label_ids;
    valid_label_ids.reserve(size);
    for (size_t i = 0; i < image_ids.size(); ++i)
    {
        if (image_ids[i] == current_image_id)
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

void ImageLabelsTableModel::labelClassUpdated(const int64_t label_class_id)
{
    if (label_ids_.empty() || column_headers_.empty())
        return;

    emit dataChanged(index(0, 0),
                     index(static_cast<int>(label_ids_.size()) - 1, static_cast<int>(column_headers_.size()) - 1),
                     {ClassDataRole});
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

int ImageLabelsTableModel::findRowByLabelId(int64_t label_id) const
{
    for (size_t i = 0; i < label_ids_.size(); ++i)
    {
        if (label_ids_[i] == label_id)
        {
            return static_cast<int>(i);
        }
    }
    return -1; // Not found
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
    if (label_ids_.empty() || column_headers_.empty())
        return;

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
    const auto emitSelectionChanged = [this](const QItemSelection &selection)
    {
        const QModelIndexList items = selection.indexes();
        int                   top{-1};
        int                   left{-1};
        int                   bottom{-1};
        int                   right{-1};
        for (const QModelIndex &index : items)
        {
            const int row = index.row();
            const int col = index.column();
            if (row < 0 || row >= rowCount() || col < 0 || col >= columnCount())
                continue;
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
        if (top >= 0 && bottom >= top && left >= 0 && right >= left)
            emit dataChanged(index(top, left), index(bottom, right), {SelectedRole});
    };

    emitSelectionChanged(deselected);
    emitSelectionChanged(selected);
}

QVariant ImageLabelsTableModel::getData(const QModelIndex &index) const
{
    const int64_t  label_id = label_ids_[index.row()];
    LabelInstance *instance = label_instances_->getLabelInstance(label_id);
    if (instance == nullptr || instance->data() == nullptr)
        return QVariant();
    auto      data = instance->data()->dataMap();
    const int col  = index.column();
    if (col >= static_cast<int>(column_keys_.size()))
        return QVariant();
    return data.value(column_keys_[col], QVariant());
}

QVariant ImageLabelsTableModel::getClassData(const QModelIndex &index) const
{
    const int64_t  label_id = label_ids_[index.row()];
    LabelInstance *instance = label_instances_->getLabelInstance(label_id);
    if (instance == nullptr)
        return QVariant();
    const int64_t class_id = instance->labelClassId();
    QVariantMap   data{
          { "class_name",  label_classes_->getLabelClassName(class_id)},
          {"class_color", label_classes_->getLabelClassColor(class_id)},
    };
    return data;
}

QVariant ImageLabelsTableModel::getSelected(const QModelIndex &index) const
{
    if (!selection_)
        return false;
    return selection_->isSelected(index);
}

QString ImageLabelsListModel::getLabelSummaryForImage(int64_t image_id) const
{
    // 空指针保护
    if (!label_instances_ || !label_classes_)
    {
        return QString();
    }

    // 通过 label_instances_ 获取指定图像的所有 label_ids
    std::vector<std::vector<int64_t>> images_label_ids = label_instances_->getImagesLabelIds({image_id});
    if (images_label_ids.empty() || images_label_ids[0].empty())
    {
        return QString();
    }

    const std::vector<int64_t> &label_ids = images_label_ids[0];

    // 统计每个类别的标注数量
    std::map<int64_t, int> class_count;
    for (const int64_t label_id : label_ids)
    {
        int64_t label_class_id = label_instances_->getLabelClassId(label_id);
        if (label_class_id >= 0)
        {
            class_count[label_class_id]++;
        }
    }

    // 格式化为 "标签总览：\n- 类别名 : 数量\n..." 的字符串
    QString summary = QString::fromUtf8("标签总览：");
    for (const auto &[label_class_id, count] : class_count)
    {
        QString class_name = label_classes_->getLabelClassName(label_class_id);
        if (!class_name.isEmpty())
        {
            summary += QString("\n  - %1 : %2").arg(class_name).arg(count);
        }
    }

    return summary;
}

QString ImageLabelsListModel::getLabelColorForImage(int64_t image_id) const
{
    // 空指针保护
    if (!label_instances_ || !label_classes_)
    {
        return QString();
    }

    // 通过 label_instances_ 获取指定图像的所有 label_ids
    std::vector<std::vector<int64_t>> images_label_ids = label_instances_->getImagesLabelIds({image_id});
    if (images_label_ids.empty() || images_label_ids[0].empty())
    {
        return QString();
    }

    const std::vector<int64_t> &label_ids = images_label_ids[0];

    // 获取第一个 label_id 的 label_class_id
    int64_t first_label_id       = label_ids[0];
    int64_t first_label_class_id = label_instances_->getLabelClassId(first_label_id);
    if (first_label_class_id < 0)
    {
        return QString();
    }

    // 通过 label_classes_ 获取颜色字符串
    QString color = label_classes_->getLabelClassColor(first_label_class_id);
    return color;
}

} // namespace dltool::data
