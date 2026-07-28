#pragma once

#include "CategoryStatisticsCalculator.h"

#include <QAbstractListModel>
#include <QtQml>

namespace dltool::data {

class LabelInstancesListModel;
class LabelClassesListModel;
class ImageInstancesListModel;
class GlobalFilter;

class CategoryStatisticsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(CategoryStatisticsModel)
    QML_UNCREATABLE("Cannot create CategoryStatisticsModel directly!")

    Q_PROPERTY(int totalInstances READ totalInstances NOTIFY totalInstancesChanged FINAL)
    Q_PROPERTY(int totalImages READ totalImages NOTIFY totalImagesChanged FINAL)

public:
    enum Role
    {
        CategoryIdRole = Qt::UserRole + 1,
        CategoryNameRole,
        CategoryColorRole,
        InstanceCountRole,
        ImageCountRole,
        InstancePercentageRole,
        ImagePercentageRole
    };
    Q_ENUM(Role)

    explicit CategoryStatisticsModel(LabelInstancesListModel *labelInstances, LabelClassesListModel *labelClasses,
                                     ImageInstancesListModel *imageInstances, QObject *parent = nullptr,
                                     GlobalFilter *filter = nullptr);
    ~CategoryStatisticsModel() override = default;

    int                    rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int                    columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant               data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refreshData(bool applyFilter);

    int totalInstances() const
    {
        return total_instances_;
    }

    int totalImages() const
    {
        return total_images_;
    }

signals:
    void totalInstancesChanged();
    void totalImagesChanged();

private:
    LabelInstancesListModel *label_instances_{nullptr};
    LabelClassesListModel  *label_classes_{nullptr};
    ImageInstancesListModel *image_instances_{nullptr};
    GlobalFilter             *filter_{nullptr};

    std::vector<CategoryStatisticsItem> statistics_;
    int                                  total_instances_{0};
    int                                  total_images_{0};
};

} // namespace dltool::data
