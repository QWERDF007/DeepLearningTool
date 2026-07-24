#include "data/DataViewModels.h"

#include "data/GlobalFilter.h"

#include <algorithm>
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
    const auto append_ranges = [&ranges, model](const QItemSelection &selection)
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
                emit currentImageChanged();
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
                emit currentImageChanged();
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
                    rememberSelection();
                    if (bulk_depth_ > 0)
                    {
                        filter_dirty_ = true;
                        return;
                    }
                    invalidate();
                    restoreSelection();
                    emit countChanged();
                    emit currentImageChanged();
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
    const int order = QString::compare(left_name, right_name, Qt::CaseInsensitive);
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
    roles[SelectedRole] = "selected";
    roles[IsCurrentRole] = "isCurrent";
    return roles;
}

void ImageInstancesViewModel::rememberSelection()
{
    if (selection_remembered_)
    {
        return;
    }

    remembered_selection_ids_ = getSelectedImagesId();
    remembered_current_id_ = currentImageId();
    selection_remembered_ = true;
}

void ImageInstancesViewModel::restoreSelection()
{
    if (!selection_remembered_)
    {
        return;
    }

    selection_support_->restoreIds(remembered_selection_ids_, ImageIdRole, remembered_current_id_);
    if (!selection()->currentIndex().isValid() && rowCount() > 0)
    {
        const QModelIndex first = index(0, 0);
        selection()->setCurrentIndex(first, QItemSelectionModel::ClearAndSelect);
        selection_support_->setAnchorRow(0);
    }
    remembered_selection_ids_.clear();
    remembered_current_id_ = -1;
    selection_remembered_ = false;
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
    image_ids.erase(std::remove_if(image_ids.begin(), image_ids.end(),
                                   [](const int64_t image_id) { return image_id < 0; }),
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
    roles[SelectedRole] = "selected";
    return roles;
}

void LabelInstancesViewModel::rememberSelection()
{
    if (selection_remembered_)
    {
        return;
    }

    remembered_selection_ids_ = getSelectedLabelIds();
    selection_remembered_ = true;
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

} // namespace dltool::data
