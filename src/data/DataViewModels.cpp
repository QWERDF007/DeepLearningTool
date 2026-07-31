#include "data/DataViewModels.h"

#include "data/DataManager.h"
#include "data/GlobalFilter.h"

#include <algorithm>
#include <iterator>
#include <set>
#include <utility>
#include <vector>

namespace dltool::data {

namespace {

/**
 * @brief 仅按选择范围刷新选择角色，避免为整块选择展开 QModelIndex 列表。
 */
void notifySelectionRows(QAbstractItemModel *model, const QItemSelection &selected, const QItemSelection &deselected,
                         const QList<int> &roles)
{
    if (model == nullptr)
    {
        return;
    }

    std::vector<std::pair<int, int>> ranges;
    const auto                       append_ranges = [&ranges, model](const QItemSelection &selection)
    {
        for (const QItemSelectionRange &range : selection)
        {
            if (!range.isValid())
            {
                continue;
            }

            const int first = std::max(0, range.top());
            const int last  = std::min(model->rowCount() - 1, range.bottom());
            if (first <= last)
            {
                ranges.emplace_back(first, last);
            }
        }
    };
    append_ranges(selected);
    append_ranges(deselected);
    if (ranges.empty())
    {
        return;
    }

    std::sort(ranges.begin(), ranges.end());
    size_t output_index = 0;
    for (size_t index = 1; index < ranges.size(); ++index)
    {
        if (ranges[index].first <= ranges[output_index].second + 1)
        {
            ranges[output_index].second = std::max(ranges[output_index].second, ranges[index].second);
            continue;
        }
        ranges[++output_index] = ranges[index];
    }
    ranges.resize(output_index + 1);

    for (const auto &[first, last] : ranges)
    {
        emit model->dataChanged(model->index(first, 0), model->index(last, 0), roles);
    }
}

} // namespace

ImageInstancesViewModel::ImageInstancesViewModel(ImageInstancesListModel *source_model, GlobalFilter *filter,
                                                 QObject *parent)
    : QSortFilterProxyModel(parent)
    , filter_(filter)
    , selection_support_(new SelectionSupport(this, this))
{
    setDynamicSortFilter(true);
    setSourceModel(source_model);
    sort(0, Qt::AscendingOrder);

    connect(selection_support_, &SelectionSupport::selectionChanged, this,
            [this](const QItemSelection &selected, const QItemSelection &deselected)
            { notifySelectionRows(selected, deselected); });
    connect(selection_support_, &SelectionSupport::currentChanged, this,
            [this](const QModelIndex &current, const QModelIndex &previous)
            {
                QItemSelection selected;
                QItemSelection deselected;
                if (current.isValid())
                {
                    selected.select(current, current);
                }
                if (previous.isValid())
                {
                    deselected.select(previous, previous);
                }
                notifySelectionRows(selected, deselected);
                if (!suppress_current_image_changed_)
                {
                    emit currentImageChanged();
                }
            });
    connect(this, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &top_left, const QModelIndex &bottom_right, const QList<int> &roles)
            {
                if (!roles.isEmpty() && !roles.contains(ImageLabelClassIdRole))
                {
                    return;
                }

                const QModelIndex current = selection()->currentIndex();
                if (current.isValid() && current.row() >= top_left.row() && current.row() <= bottom_right.row())
                {
                    emit currentImageChanged();
                }
            });
    connect(this, &QAbstractItemModel::modelAboutToBeReset, this, &ImageInstancesViewModel::rememberSelection);
    connect(this, &QAbstractItemModel::modelReset, this,
            [this]()
            {
                if (bulk_depth_ == 0)
                {
                    restoreSelection();
                }
                emit countChanged();
                if (!suppress_current_image_changed_)
                {
                    emit currentImageChanged();
                }
            });
    connect(this, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex &, int, int) { emit countChanged(); });
    connect(this, &QAbstractItemModel::rowsRemoved, this,
            [this](const QModelIndex &, int, int) { emit countChanged(); });
    if (filter_ != nullptr)
    {
        connect(filter_, &GlobalFilter::filterChanged, this,
                [this]()
                {
                    const int64_t previous_current_image_id = currentImageId();
                    suppress_current_image_changed_         = true;
                    rememberSelection();
                    if (bulk_depth_ > 0)
                    {
                        filter_dirty_                   = true;
                        suppress_current_image_changed_ = false;
                        return;
                    }
                    invalidate();
                    restoreSelection();
                    suppress_current_image_changed_ = false;
                    emit countChanged();
                    if (currentImageId() != previous_current_image_id)
                    {
                        emit currentImageChanged();
                    }
                });
    }
}

ImageInstancesListModel *ImageInstancesViewModel::source() const
{
    return qobject_cast<ImageInstancesListModel *>(sourceModel());
}

QItemSelectionModel *ImageInstancesViewModel::selection() const
{
    return selection_support_->selection();
}

int ImageInstancesViewModel::count() const
{
    return rowCount();
}

int64_t ImageInstancesViewModel::currentImageId() const
{
    const QModelIndex current = selection()->currentIndex();
    return current.isValid() ? data(current, ImageIdRole).toLongLong() : -1;
}

QString ImageInstancesViewModel::currentImageName() const
{
    const QModelIndex current = selection()->currentIndex();
    return current.isValid() ? data(current, NameRole).toString() : QString();
}

QString ImageInstancesViewModel::currentImagePath() const
{
    const QModelIndex current = selection()->currentIndex();
    return current.isValid() ? data(current, PathRole).toString() : QString();
}

int64_t ImageInstancesViewModel::currentImageLabelClassId() const
{
    const QModelIndex current = selection()->currentIndex();
    return current.isValid() ? data(current, ImageLabelClassIdRole).toLongLong() : -1;
}

int ImageInstancesViewModel::lastIndex() const
{
    return selection_support_->anchorRow();
}

void ImageInstancesViewModel::setLastIndex(const int row)
{
    selection_support_->setAnchorRow(row);
    emit lastSelectedIndexChanged();
}

void ImageInstancesViewModel::setImageSortOrder(const int sort_order)
{
    const ImageSortOrder requested = sort_order == static_cast<int>(ImageSortOrder::FileName)
                                       ? ImageSortOrder::FileName
                                       : ImageSortOrder::AddedTime;
    if (sort_order_ == requested)
    {
        return;
    }

    rememberSelection();
    sort_order_ = requested;
    invalidate();
    sort(0, Qt::AscendingOrder);
    restoreSelection();
}

int ImageInstancesViewModel::findRowByImageId(const qint64 image_id) const
{
    for (int row = 0; row < rowCount(); ++row)
    {
        if (data(index(row, 0), ImageIdRole).toLongLong() == image_id)
        {
            return row;
        }
    }
    return -1;
}

void ImageInstancesViewModel::shiftSelect(const int current_index, const int previous_index,
                                          const QItemSelectionModel::SelectionFlags command)
{
    selection_support_->selectRange(current_index, previous_index, command);
}

void ImageInstancesViewModel::selectAll()
{
    selection_support_->selectAllRows();
}

std::vector<int64_t> ImageInstancesViewModel::getSelectedImagesId() const
{
    return selection_support_->selectedIds(ImageIdRole);
}

void ImageInstancesViewModel::beginBulkUpdate()
{
    if (bulk_depth_++ != 0)
    {
        return;
    }

    dynamic_sort_before_bulk_ = dynamicSortFilter();
    setDynamicSortFilter(false);
    rememberSelection();
}

void ImageInstancesViewModel::endBulkUpdate()
{
    if (bulk_depth_ == 0 || --bulk_depth_ != 0)
    {
        return;
    }

    setDynamicSortFilter(dynamic_sort_before_bulk_);
    invalidate();
    sort(0, Qt::AscendingOrder);
    filter_dirty_ = false;
    restoreSelection();
    emit countChanged();
    emit currentImageChanged();
}

bool ImageInstancesViewModel::filterAcceptsRow(const int source_row, const QModelIndex &source_parent) const
{
    if (filter_ == nullptr || sourceModel() == nullptr)
    {
        return true;
    }
    const QModelIndex source_index = sourceModel()->index(source_row, 0, source_parent);
    return filter_->acceptsImage(sourceModel()->data(source_index, ImageIdRole).toLongLong());
}

bool ImageInstancesViewModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    if (sourceModel() == nullptr)
    {
        return false;
    }
    const int64_t left_id  = sourceModel()->data(left, ImageIdRole).toLongLong();
    const int64_t right_id = sourceModel()->data(right, ImageIdRole).toLongLong();
    if (sort_order_ == ImageSortOrder::AddedTime)
    {
        return left_id > right_id;
    }

    const QString left_name  = sourceModel()->data(left, NameRole).toString();
    const QString right_name = sourceModel()->data(right, NameRole).toString();
    const int     order      = QString::compare(left_name, right_name, Qt::CaseInsensitive);
    return order == 0 ? left_id > right_id : order < 0;
}

QVariant ImageInstancesViewModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid())
    {
        return {};
    }
    if (role == SelectedRole)
    {
        return selection()->isSelected(index);
    }
    if (role == IsCurrentRole)
    {
        return selection()->currentIndex() == index;
    }
    return QSortFilterProxyModel::data(index, role);
}

QHash<int, QByteArray> ImageInstancesViewModel::roleNames() const
{
    QHash<int, QByteArray> roles = QSortFilterProxyModel::roleNames();
    roles[SelectedRole]          = "selected";
    roles[IsCurrentRole]         = "isCurrent";
    return roles;
}

void ImageInstancesViewModel::rememberSelection()
{
    if (selection_remembered_)
    {
        return;
    }

    remembered_selection_ids_ = getSelectedImagesId();
    remembered_current_id_    = currentImageId();
    remembered_image_ids_.clear();
    remembered_image_ids_.reserve(static_cast<size_t>(rowCount()));
    for (int row = 0; row < rowCount(); ++row)
    {
        remembered_image_ids_.push_back(data(index(row, 0), ImageIdRole).toLongLong());
    }
    selection_remembered_     = true;
}

void ImageInstancesViewModel::selectNextAfterCurrentRemoval()
{
    if (remembered_current_id_ < 0 || rowCount() <= 0)
    {
        return;
    }

    const auto current_it
        = std::find(remembered_image_ids_.cbegin(), remembered_image_ids_.cend(), remembered_current_id_);
    if (current_it == remembered_image_ids_.cend())
    {
        return;
    }

    const auto selectImage = [this](const int64_t image_id)
    {
        const int row = findRowByImageId(image_id);
        if (row < 0)
        {
            return false;
        }

        const QModelIndex next_index = index(row, 0);
        selection()->select(next_index, QItemSelectionModel::ClearAndSelect);
        selection()->setCurrentIndex(next_index, QItemSelectionModel::Select);
        selection_support_->setAnchorRow(row);
        return true;
    };

    for (auto it = std::next(current_it); it != remembered_image_ids_.cend(); ++it)
    {
        if (selectImage(*it))
        {
            return;
        }
    }
    for (auto it = current_it; it != remembered_image_ids_.cbegin();)
    {
        --it;
        if (selectImage(*it))
        {
            return;
        }
    }
}

void ImageInstancesViewModel::restoreSelection()
{
    if (!selection_remembered_)
    {
        return;
    }

    const bool current_removed = remembered_current_id_ >= 0 && findRowByImageId(remembered_current_id_) < 0;
    selection_support_->restoreIds(remembered_selection_ids_, ImageIdRole, remembered_current_id_);
    if (current_removed)
    {
        selection()->clear();
        selectNextAfterCurrentRemoval();
        if (!selection()->currentIndex().isValid() && rowCount() > 0)
        {
            const QModelIndex first = index(0, 0);
            selection()->setCurrentIndex(first, QItemSelectionModel::ClearAndSelect);
            selection_support_->setAnchorRow(0);
        }
    }
    else if (!selection()->currentIndex().isValid() && rowCount() > 0)
    {
        const QModelIndex first = index(0, 0);
        selection()->setCurrentIndex(first, QItemSelectionModel::ClearAndSelect);
        selection_support_->setAnchorRow(0);
    }
    remembered_selection_ids_.clear();
    remembered_image_ids_.clear();
    remembered_current_id_ = -1;
    selection_remembered_  = false;
}

void ImageInstancesViewModel::notifySelectionRows(const QItemSelection &selected, const QItemSelection &deselected)
{
    ::dltool::data::notifySelectionRows(this, selected, deselected, {SelectedRole, IsCurrentRole});
}

LabelInstancesViewModel::LabelInstancesViewModel(LabelInstancesListModel *source_model, GlobalFilter *filter,
                                                 QObject *parent)
    : QSortFilterProxyModel(parent)
    , filter_(filter)
    , selection_support_(new SelectionSupport(this, this))
{
    setDynamicSortFilter(false);
    setSourceModel(source_model);

    connect(selection_support_, &SelectionSupport::selectionChanged, this,
            [this](const QItemSelection &selected, const QItemSelection &deselected)
            { notifySelectionRows(selected, deselected); });
    connect(this, &QAbstractItemModel::modelAboutToBeReset, this, &LabelInstancesViewModel::rememberSelection);
    connect(this, &QAbstractItemModel::modelReset, this,
            [this]()
            {
                if (bulk_depth_ == 0)
                {
                    restoreSelection();
                }
            });
    if (filter_ != nullptr)
    {
        connect(filter_, &GlobalFilter::filterChanged, this,
                [this]()
                {
                    rememberSelection();
                    if (bulk_depth_ > 0)
                    {
                        filter_dirty_ = true;
                        return;
                    }
                    invalidate();
                    restoreSelection();
                });
    }
}

LabelInstancesListModel *LabelInstancesViewModel::source() const
{
    return qobject_cast<LabelInstancesListModel *>(sourceModel());
}

QItemSelectionModel *LabelInstancesViewModel::selection() const
{
    return selection_support_->selection();
}

int LabelInstancesViewModel::lastIndex() const
{
    return selection_support_->anchorRow();
}

void LabelInstancesViewModel::setLastIndex(const int row)
{
    selection_support_->setAnchorRow(row);
    emit lastIndexChanged();
}

void LabelInstancesViewModel::shiftSelect(const int current_index, const int previous_index,
                                          const QItemSelectionModel::SelectionFlags command)
{
    selection_support_->selectRange(current_index, previous_index, command);
}

void LabelInstancesViewModel::selectAll()
{
    selection_support_->selectAllRows();
}

std::vector<int64_t> LabelInstancesViewModel::getSelectedLabelIds() const
{
    return selection_support_->selectedIds(LabelIdRole);
}

std::vector<int64_t> LabelInstancesViewModel::getSelectedImageIds() const
{
    LabelInstancesListModel *labels = source();
    if (labels == nullptr)
    {
        return {};
    }

    std::vector<int64_t> image_ids = labels->getImageIds(getSelectedLabelIds());
    image_ids.erase(
        std::remove_if(image_ids.begin(), image_ids.end(), [](const int64_t image_id) { return image_id < 0; }),
        image_ids.end());
    std::sort(image_ids.begin(), image_ids.end());
    image_ids.erase(std::unique(image_ids.begin(), image_ids.end()), image_ids.end());
    return image_ids;
}

void LabelInstancesViewModel::beginBulkUpdate()
{
    if (bulk_depth_++ != 0)
    {
        return;
    }

    dynamic_sort_before_bulk_ = dynamicSortFilter();
    setDynamicSortFilter(false);
    rememberSelection();
}

void LabelInstancesViewModel::endBulkUpdate()
{
    if (bulk_depth_ == 0 || --bulk_depth_ != 0)
    {
        return;
    }

    setDynamicSortFilter(dynamic_sort_before_bulk_);
    invalidate();
    filter_dirty_ = false;
    restoreSelection();
}

bool LabelInstancesViewModel::filterAcceptsRow(const int source_row, const QModelIndex &source_parent) const
{
    if (filter_ == nullptr || sourceModel() == nullptr)
    {
        return true;
    }
    const QModelIndex source_index = sourceModel()->index(source_row, 0, source_parent);
    return filter_->acceptsLabel(sourceModel()->data(source_index, LabelIdRole).toLongLong());
}

QVariant LabelInstancesViewModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid())
    {
        return {};
    }
    if (role == SelectedRole)
    {
        return selection()->isSelected(index);
    }
    return QSortFilterProxyModel::data(index, role);
}

QHash<int, QByteArray> LabelInstancesViewModel::roleNames() const
{
    QHash<int, QByteArray> roles = QSortFilterProxyModel::roleNames();
    roles[SelectedRole]          = "selected";
    return roles;
}

void LabelInstancesViewModel::rememberSelection()
{
    if (selection_remembered_)
    {
        return;
    }

    remembered_selection_ids_ = getSelectedLabelIds();
    selection_remembered_     = true;
}

void LabelInstancesViewModel::restoreSelection()
{
    if (!selection_remembered_)
    {
        return;
    }

    selection_support_->restoreIds(remembered_selection_ids_, LabelIdRole);
    remembered_selection_ids_.clear();
    selection_remembered_ = false;
}

void LabelInstancesViewModel::notifySelectionRows(const QItemSelection &selected, const QItemSelection &deselected)
{
    ::dltool::data::notifySelectionRows(this, selected, deselected, {SelectedRole});
}

SelectedLabelsInfoModel::SelectedLabelsInfoModel(DataManager *data_manager, LabelInstancesViewModel *label_instances,
                                                 QObject *parent)
    : QObject(parent)
    , data_manager_(data_manager)
    , label_instances_(label_instances)
{
    if (label_instances_ != nullptr)
    {
        connect(label_instances_->selection(), &QItemSelectionModel::selectionChanged, this,
                [this](const QItemSelection &, const QItemSelection &) { refresh(); });
        connect(label_instances_, &QAbstractItemModel::rowsInserted, this,
                [this](const QModelIndex &, int, int) { refresh(); });
        connect(label_instances_, &QAbstractItemModel::rowsRemoved, this,
                [this](const QModelIndex &, int, int) { refresh(); });
        connect(label_instances_, &QAbstractItemModel::modelReset, this, &SelectedLabelsInfoModel::refresh);
        connect(label_instances_, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex &, const QModelIndex &, const QList<int> &) { refresh(); });
    }
    refresh();
}

void SelectedLabelsInfoModel::clearSelection()
{
    if (label_instances_ != nullptr && label_instances_->selection() != nullptr)
    {
        label_instances_->selection()->clear();
    }
}

void SelectedLabelsInfoModel::changeSelectedLabelsClass(const qint64 label_class_id)
{
    if (data_manager_ == nullptr || label_instances_ == nullptr || label_class_id < 0)
    {
        return;
    }
    const std::vector<int64_t> label_ids = label_instances_->getSelectedLabelIds();
    if (label_ids.empty())
    {
        return;
    }
    data_manager_->updateLabelsClass(label_ids, std::vector<int64_t>(label_ids.size(), label_class_id));
}

void SelectedLabelsInfoModel::refresh()
{
    selected_count_ = 0;
    total_count_    = label_instances_ == nullptr ? 0 : label_instances_->rowCount();
    image_text_.clear();
    dataset_text_.clear();
    tag_text_.clear();
    current_class_id_ = -1;
    multiple_classes_ = false;

    LabelInstancesListModel *source = label_instances_ == nullptr ? nullptr : label_instances_->source();
    if (data_manager_ == nullptr || label_instances_ == nullptr || source == nullptr
        || label_instances_->selection() == nullptr)
    {
        emit infoChanged();
        return;
    }

    const QItemSelection ranges = label_instances_->selection()->selection();
    for (const QItemSelectionRange &range : ranges)
    {
        if (range.isValid())
        {
            selected_count_ += range.height();
        }
    }
    if (selected_count_ == 0)
    {
        emit infoChanged();
        return;
    }

    int64_t           first_image_id   = -1;
    int64_t           first_dataset_id = -1;
    std::set<int64_t> first_tag_ids;
    bool              initialized       = false;
    bool              multiple_images   = false;
    bool              multiple_datasets = false;
    bool              multiple_tags     = false;
    for (const QItemSelectionRange &range : ranges)
    {
        if (!range.isValid())
        {
            continue;
        }
        const int top    = std::max(0, range.top());
        const int bottom = std::min(total_count_ - 1, range.bottom());
        for (int row = top; row <= bottom; ++row)
        {
            const QModelIndex model_index  = label_instances_->index(row, 0);
            const QModelIndex source_index = label_instances_->mapToSource(model_index);
            const int64_t     image_id = source->data(source_index, LabelInstancesListModel::ImageIdRole).toLongLong();
            const int64_t class_id = source->data(source_index, LabelInstancesListModel::LabelClassIdRole).toLongLong();
            const int64_t dataset_id         = data_manager_->imageSource()->getImageDatasetId(image_id);
            const std::set<int64_t> &tag_ids = data_manager_->imageSource()->getImageTagIds(image_id);
            if (!initialized)
            {
                first_image_id    = image_id;
                first_dataset_id  = dataset_id;
                first_tag_ids     = tag_ids;
                current_class_id_ = class_id;
                initialized       = true;
            }
            else
            {
                multiple_images   = multiple_images || image_id != first_image_id;
                multiple_datasets = multiple_datasets || dataset_id != first_dataset_id;
                multiple_tags     = multiple_tags || tag_ids != first_tag_ids;
                multiple_classes_ = multiple_classes_ || class_id != current_class_id_;
            }
        }
    }

    image_text_ = multiple_images ? QStringLiteral("不同图像") : data_manager_->getImageName(first_image_id);
    dataset_text_
        = multiple_datasets ? QStringLiteral("不同数据集") : data_manager_->getImageDatasetName(first_image_id);
    tag_text_ = multiple_tags ? QStringLiteral("不同Tag") : data_manager_->getImageTagName(first_image_id);
    if (multiple_classes_)
    {
        current_class_id_ = -1;
    }
    emit infoChanged();
}

} // namespace dltool::data
