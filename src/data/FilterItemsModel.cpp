#include "data/FilterItemsModel.h"

#include "data/Datasets.h"
#include "data/ImageTags.h"
#include "data/LabelClasses.h"

#include <spdlog/spdlog.h>

#include <unordered_set>

namespace dltool::data {

// ============================================================================
// FilterItemsModel 实现
// ============================================================================

FilterItemsModel::FilterItemsModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int FilterItemsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(items_.size());
}

QVariant FilterItemsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(items_.size()))
        return QVariant();

    const FilterItem &item = items_[index.row()];

    switch (role)
    {
    case IdRole:
        return QVariant::fromValue(item.id);
    case TextRole:
        return item.text;
    case CheckedRole:
        return item.checked;
    default:
        return QVariant();
    }
}

bool FilterItemsModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(items_.size()))
        return false;

    FilterItem &item = items_[index.row()];

    switch (role)
    {
    case CheckedRole:
        if (value.canConvert<bool>())
        {
            item.checked = value.toBool();
            emit dataChanged(index, index, {CheckedRole});
            return true;
        }
        break;
    default:
        break;
    }

    return false;
}

Qt::ItemFlags FilterItemsModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

QHash<int, QByteArray> FilterItemsModel::roleNames() const
{
    return {
        {     IdRole,      "id"},
        {   TextRole,    "text"},
        {CheckedRole, "checked"}
    };
}

void FilterItemsModel::clear()
{
    if (items_.empty())
        return;

    beginResetModel();
    items_.clear();
    endResetModel();
}

void FilterItemsModel::append(int64_t id, const QString &text, bool checked)
{
    int row = static_cast<int>(items_.size());
    beginInsertRows(QModelIndex(), row, row);
    items_.emplace_back(id, text, checked);
    endInsertRows();
}

std::vector<int64_t> FilterItemsModel::getCheckedIds() const
{
    std::vector<int64_t> checked_ids;
    checked_ids.reserve(items_.size());

    for (const auto &item : items_)
    {
        if (item.checked)
        {
            checked_ids.push_back(item.id);
        }
    }

    return checked_ids;
}

void FilterItemsModel::setAllChecked(bool checked)
{
    if (items_.empty())
        return;

    for (size_t i = 0; i < items_.size(); ++i)
    {
        items_[i].checked = checked;
    }

    // 通知所有行的checked状态已改变
    emit dataChanged(index(0), index(static_cast<int>(items_.size()) - 1), {CheckedRole});
}

// ============================================================================
// DatasetFilterItemsModel 实现
// ============================================================================

DatasetFilterItemsModel::DatasetFilterItemsModel(QObject *parent)
    : FilterItemsModel(parent)
{
}

void DatasetFilterItemsModel::populateFromDatasets(QAbstractItemModel *datasets_model)
{
    if (!datasets_model)
    {
        spdlog::warn("DatasetFilterItemsModel::populateFromDatasets: datasets_model is null");
        return;
    }

    // 保存现有未选中项的ID，以便重新填充后恢复选中状态
    std::unordered_set<int64_t> unchecked_ids;
    for (const auto &item : items_)
    {
        if (!item.checked)
        {
            unchecked_ids.insert(item.id);
        }
    }

    // 清空现有数据
    clear();

    // 从数据集模型填充
    int row_count = datasets_model->rowCount();
    for (int i = 0; i < row_count; ++i)
    {
        QModelIndex idx = datasets_model->index(i, 0);

        // 获取数据集ID和名称
        QVariant id_variant   = datasets_model->data(idx, DatasetsListModel::DatasetIdRole);
        QVariant name_variant = datasets_model->data(idx, DatasetsListModel::NameRole);

        if (id_variant.isValid() && name_variant.isValid())
        {
            int64_t dataset_id   = id_variant.toLongLong();
            QString dataset_name = name_variant.toString();

            // 保留已有项的选中状态，新项默认选中
            bool checked = unchecked_ids.find(dataset_id) == unchecked_ids.end();
            append(dataset_id, dataset_name, checked);
        }
    }
}

// ============================================================================
// TagFilterItemsModel 实现
// ============================================================================

TagFilterItemsModel::TagFilterItemsModel(QObject *parent)
    : FilterItemsModel(parent)
{
}

void TagFilterItemsModel::populateFromTags(QAbstractItemModel *tags_model)
{
    if (!tags_model)
    {
        spdlog::warn("TagFilterItemsModel::populateFromTags: tags_model is null");
        return;
    }

    // 保存现有未选中项的ID，以便重新填充后恢复选中状态
    std::unordered_set<int64_t> unchecked_ids;
    for (const auto &item : items_)
    {
        if (!item.checked)
        {
            unchecked_ids.insert(item.id);
        }
    }

    // 清空现有数据
    clear();

    // 从标签模型填充
    int row_count = tags_model->rowCount();
    for (int i = 0; i < row_count; ++i)
    {
        QModelIndex idx = tags_model->index(i, 0);

        // 获取标签ID和名称
        QVariant id_variant   = tags_model->data(idx, ImageTagsListModel::TagIdRole);
        QVariant name_variant = tags_model->data(idx, ImageTagsListModel::NameRole);

        if (id_variant.isValid() && name_variant.isValid())
        {
            int64_t tag_id   = id_variant.toLongLong();
            QString tag_name = name_variant.toString();

            // 保留已有项的选中状态，新项默认选中
            bool checked = unchecked_ids.find(tag_id) == unchecked_ids.end();
            append(tag_id, tag_name, checked);
        }
    }
}

// ============================================================================
// LabelClassFilterItemsModel 实现
// ============================================================================

LabelClassFilterItemsModel::LabelClassFilterItemsModel(QObject *parent)
    : FilterItemsModel(parent)
{
}

void LabelClassFilterItemsModel::populateFromLabelClasses(QAbstractItemModel *label_classes_model)
{
    if (!label_classes_model)
    {
        spdlog::warn("LabelClassFilterItemsModel::populateFromLabelClasses: label_classes_model is null");
        return;
    }

    // 保存现有未选中项的ID，以便重新填充后恢复选中状态
    std::unordered_set<int64_t> unchecked_ids;
    for (const auto &item : items_)
    {
        if (!item.checked)
        {
            unchecked_ids.insert(item.id);
        }
    }

    clear();

    const int row_count = label_classes_model->rowCount();
    for (int i = 0; i < row_count; ++i)
    {
        const QModelIndex idx = label_classes_model->index(i, 0);

        QVariant id_variant   = label_classes_model->data(idx, LabelClassesListModel::LabelClassIdRole);
        QVariant name_variant = label_classes_model->data(idx, LabelClassesListModel::NameRole);

        if (id_variant.isValid() && name_variant.isValid())
        {
            const int64_t label_class_id = id_variant.toLongLong();
            const QString name           = name_variant.toString();

            // 保留已有项的选中状态，新项默认选中
            bool checked = unchecked_ids.find(label_class_id) == unchecked_ids.end();
            append(label_class_id, name, checked);
        }
    }
}

} // namespace dltool::data
