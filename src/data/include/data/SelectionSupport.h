#pragma once

#include <QItemSelectionModel>
#include <QObject>

#include <vector>

namespace dltool::data {

/**
 * @brief 为可见模型提供统一的行选择、范围选择和锚点管理。
 *
 * 选择状态属于视图模型，不属于原始数据模型。该类只封装 Qt 的
 * QItemSelectionModel，不保存任何业务数据或业务 ID。
 */
class SelectionSupport final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QItemSelectionModel *selection READ selection CONSTANT)
    Q_PROPERTY(int anchorRow READ anchorRow WRITE setAnchorRow NOTIFY anchorRowChanged)

public:
    explicit SelectionSupport(QAbstractItemModel *model, QObject *parent = nullptr);

    QItemSelectionModel *selection() const
    {
        return selection_;
    }

    int anchorRow() const
    {
        return anchor_row_;
    }

    void setAnchorRow(int row);

    void selectAllRows();
    void selectRange(int current_row, int previous_row, QItemSelectionModel::SelectionFlags command);
    std::vector<int64_t> selectedIds(int id_role) const;
    void restoreIds(const std::vector<int64_t> &ids, int id_role, int current_id = -1);

signals:
    void selectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
    void currentChanged(const QModelIndex &current, const QModelIndex &previous);
    void anchorRowChanged();

private:
    QItemSelectionModel *selection_{nullptr}; ///< Qt 选择模型。
    int                  anchor_row_{-1};    ///< Shift 多选的起始行。
};

} // namespace dltool::data
