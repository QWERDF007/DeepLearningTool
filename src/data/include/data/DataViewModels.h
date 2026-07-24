#pragma once

#include "Images.h"
#include "Labels.h"
#include "SelectionSupport.h"

#include <QSortFilterProxyModel>
#include <QtQml>

namespace dltool::data {

class GlobalFilter;

/**
 * @brief 图像的可见列表模型。
 *
 * 原始图像模型只保存全部图像；本模型负责筛选、排序和视图选择状态，
 * 因此不再需要在原始模型中维护 filtered_image_ids_。
 */
class ImageInstancesViewModel final : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ImageInstancesModel)
    QML_UNCREATABLE("Can not create ImageInstancesModel directly!")

    Q_PROPERTY(QItemSelectionModel *selection READ selection CONSTANT)
    Q_PROPERTY(qint64 currentImageId READ currentImageId NOTIFY currentImageChanged)
    Q_PROPERTY(QString currentImageName READ currentImageName NOTIFY currentImageChanged)
    Q_PROPERTY(QString currentImagePath READ currentImagePath NOTIFY currentImageChanged)
    Q_PROPERTY(qint64 currentImageLabelClassId READ currentImageLabelClassId NOTIFY currentImageChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int lastIndex READ lastIndex WRITE setLastIndex NOTIFY lastSelectedIndexChanged)

public:
    enum class ImageSortOrder
    {
        AddedTime = 0,
        FileName  = 1,
    };
    Q_ENUM(ImageSortOrder)

    enum Role
    {
        ImageIdRole         = ImageInstancesListModel::ImageIdRole,
        NameRole            = ImageInstancesListModel::NameRole,
        PathRole            = ImageInstancesListModel::PathRole,
        SelectedRole        = ImageInstancesListModel::SelectedRole,
        IsCurrentRole       = ImageInstancesListModel::IsCurrentRole,
        HasLabelsRole       = ImageInstancesListModel::HasLabelsRole,
        ImageLabelClassIdRole = ImageInstancesListModel::ImageLabelClassIdRole,
        DatasetIdRole       = ImageInstancesListModel::DatasetIdRole,
    };
    Q_ENUM(Role)

    explicit ImageInstancesViewModel(ImageInstancesListModel *source_model, GlobalFilter *filter,
                                     QObject *parent = nullptr);

    ImageInstancesListModel *source() const;
    QItemSelectionModel     *selection() const;

    int     count() const;
    int64_t currentImageId() const;
    QString currentImageName() const;
    QString currentImagePath() const;
    int64_t currentImageLabelClassId() const;
    int lastIndex() const;
    void setLastIndex(int row);

    Q_INVOKABLE void setImageSortOrder(int sort_order);
    Q_INVOKABLE int  findRowByImageId(qint64 image_id) const;
    Q_INVOKABLE void shiftSelect(int current_index, int previous_index,
                                 QItemSelectionModel::SelectionFlags command);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE std::vector<int64_t> getSelectedImagesId() const;

    void beginBulkUpdate();
    void endBulkUpdate();

signals:
    void currentImageChanged();
    void lastSelectedIndexChanged();
    void countChanged();

protected:
    bool     filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override;
    bool     lessThan(const QModelIndex &left, const QModelIndex &right) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    void rememberSelection();
    void restoreSelection();
    void notifySelectionRows(const QItemSelection &selected, const QItemSelection &deselected);

    GlobalFilter     *filter_{nullptr};
    SelectionSupport *selection_support_{nullptr};
    ImageSortOrder    sort_order_{ImageSortOrder::AddedTime};
    std::vector<int64_t> remembered_selection_ids_;
    int64_t              remembered_current_id_{-1};
    int                  bulk_depth_{0};
    bool                 dynamic_sort_before_bulk_{true};
    bool                 selection_remembered_{false};
    bool                 filter_dirty_{false};
};

/**
 * @brief 标注的可见列表模型。
 *
 * 原始标注模型只保存全部标注；筛选和选择状态由该代理模型维护。
 */
class LabelInstancesViewModel final : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(LabelInstancesModel)
    QML_UNCREATABLE("Can not create LabelInstancesModel directly!")

    Q_PROPERTY(QItemSelectionModel *selection READ selection CONSTANT)
    Q_PROPERTY(int lastIndex READ lastIndex WRITE setLastIndex NOTIFY lastIndexChanged)

public:
    enum Role
    {
        LabelIdRole         = LabelInstancesListModel::LabelIdRole,
        ImageIdRole         = LabelInstancesListModel::ImageIdRole,
        LabelClassIdRole    = LabelInstancesListModel::LabelClassIdRole,
        LabelClassNameRole  = LabelInstancesListModel::LabelClassNameRole,
        LabelClassColorRole = LabelInstancesListModel::LabelClassColorRole,
        DataRole            = LabelInstancesListModel::DataRole,
        SelectedRole        = LabelInstancesListModel::SelectedRole,
    };
    Q_ENUM(Role)

    explicit LabelInstancesViewModel(LabelInstancesListModel *source_model, GlobalFilter *filter,
                                     QObject *parent = nullptr);

    LabelInstancesListModel *source() const;
    QItemSelectionModel     *selection() const;
    int                      lastIndex() const;
    void                     setLastIndex(int row);

    Q_INVOKABLE void shiftSelect(int current_index, int previous_index,
                                 QItemSelectionModel::SelectionFlags command);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE std::vector<int64_t> getSelectedLabelIds() const;
    Q_INVOKABLE std::vector<int64_t> getSelectedImageIds() const;

    void beginBulkUpdate();
    void endBulkUpdate();

signals:
    void lastIndexChanged();

protected:
    bool     filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    void rememberSelection();
    void restoreSelection();
    void notifySelectionRows(const QItemSelection &selected, const QItemSelection &deselected);

    GlobalFilter          *filter_{nullptr};
    SelectionSupport      *selection_support_{nullptr};
    std::vector<int64_t>   remembered_selection_ids_;
    int                    bulk_depth_{0};
    bool                   dynamic_sort_before_bulk_{true};
    bool                   selection_remembered_{false};
    bool                   filter_dirty_{false};
};

} // namespace dltool::data
