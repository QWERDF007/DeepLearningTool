#include "data/SelectionSupport.h"

#include <algorithm>
#include <unordered_set>

namespace dltool::data {

SelectionSupport::SelectionSupport(QAbstractItemModel *model, QObject *parent)
    : QObject(parent)
    , selection_(new QItemSelectionModel(model, this))
{
    connect(selection_, &QItemSelectionModel::selectionChanged, this, &SelectionSupport::selectionChanged);
    connect(selection_, &QItemSelectionModel::currentChanged, this, &SelectionSupport::currentChanged);
}

void SelectionSupport::setAnchorRow(const int row)
{
    if (anchor_row_ == row)
    {
        return;
    }

    anchor_row_ = row;
    emit anchorRowChanged();
}

void SelectionSupport::selectAllRows()
{
    if (selection_ == nullptr || selection_->model() == nullptr || selection_->model()->rowCount() <= 0)
    {
        return;
    }

    QItemSelection selection;
    selection.select(selection_->model()->index(0, 0),
                     selection_->model()->index(selection_->model()->rowCount() - 1, 0));
    selection_->select(selection, QItemSelectionModel::Select | QItemSelectionModel::Rows);
}

void SelectionSupport::selectRange(const int current_row, const int previous_row,
                                   const QItemSelectionModel::SelectionFlags command)
{
    if (selection_ == nullptr || selection_->model() == nullptr || selection_->model()->rowCount() <= 0)
    {
        return;
    }

    const int top    = std::max(0, std::min(current_row, previous_row));
    const int bottom = std::min(selection_->model()->rowCount() - 1, std::max(current_row, previous_row));
    if (top > bottom)
    {
        return;
    }

    QItemSelection selection;
    selection.select(selection_->model()->index(top, 0), selection_->model()->index(bottom, 0));
    selection_->select(selection, command | QItemSelectionModel::Rows);
}

std::vector<int64_t> SelectionSupport::selectedIds(const int id_role) const
{
    std::vector<int64_t> ids;
    if (selection_ == nullptr || selection_->model() == nullptr)
    {
        return ids;
    }

    const QItemSelection ranges = selection_->selection();
    for (const QItemSelectionRange &range : ranges)
    {
        if (!range.isValid())
        {
            continue;
        }
        for (int row = range.top(); row <= range.bottom(); ++row)
        {
            const QModelIndex index = selection_->model()->index(row, 0);
            const qint64 id = selection_->model()->data(index, id_role).toLongLong();
            if (id >= 0)
            {
                ids.push_back(id);
            }
        }
    }
    return ids;
}

void SelectionSupport::restoreIds(const std::vector<int64_t> &ids, const int id_role, const int current_id)
{
    if (selection_ == nullptr || selection_->model() == nullptr)
    {
        return;
    }

    std::unordered_set<int64_t> expected(ids.begin(), ids.end());
    std::vector<int>             rows;
    rows.reserve(expected.size());
    int current_row = -1;
    for (int row = 0; row < selection_->model()->rowCount(); ++row)
    {
        const int64_t id = selection_->model()->data(selection_->model()->index(row, 0), id_role).toLongLong();
        if (expected.contains(id))
        {
            rows.push_back(row);
        }
        if (id == current_id)
        {
            current_row = row;
        }
    }

    selection_->clear();
    if (!rows.empty())
    {
        QItemSelection ranges;
        int            first = rows.front();
        int            last  = first;
        for (size_t i = 1; i < rows.size(); ++i)
        {
            if (rows[i] == last + 1)
            {
                last = rows[i];
                continue;
            }
            ranges.select(selection_->model()->index(first, 0), selection_->model()->index(last, 0));
            first = rows[i];
            last  = first;
        }
        ranges.select(selection_->model()->index(first, 0), selection_->model()->index(last, 0));
        selection_->select(ranges, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }

    if (current_row >= 0)
    {
        selection_->setCurrentIndex(selection_->model()->index(current_row, 0), QItemSelectionModel::NoUpdate);
        setAnchorRow(current_row);
    }
    else
    {
        setAnchorRow(-1);
    }
}

} // namespace dltool::data
