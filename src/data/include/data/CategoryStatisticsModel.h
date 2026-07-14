#pragma once

#include "DataSelectionTreeModel.h"

#include <QAbstractListModel>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml>
#include <set>
#include <vector>


namespace dltool::data {
class DataManager;
}

namespace dltool::data {

class LabelInstancesListModel;
class LabelClassesListModel;
class ImageInstancesListModel;

class CategoryStatisticsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(CategoryStatisticsModel)
    QML_UNCREATABLE("Cannot create CategoryStatisticsModel directly!")

    Q_PROPERTY(int totalInstances READ totalInstances NOTIFY totalInstancesChanged)
    Q_PROPERTY(int totalImages READ totalImages NOTIFY totalImagesChanged)
    Q_PROPERTY(QVariantMap imageChartData READ imageChartData NOTIFY chartDataChanged FINAL)
    Q_PROPERTY(QVariantMap instanceChartData READ instanceChartData NOTIFY chartDataChanged FINAL)

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
                                     ImageInstancesListModel *imageInstances, QObject *parent = nullptr);
    ~CategoryStatisticsModel() = default;

    // QAbstractListModel interface
    int                    rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int                    columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant               data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // TODO: 将饼图所需的数据从本类中拆离或者基于本类重新设计，抽离公共基类
    // Data refresh methods
    Q_INVOKABLE void         refreshData(bool applyFilter);
    Q_INVOKABLE void         refreshForDatasets(const QVariantList &datasetIds);
    Q_INVOKABLE void         refreshForSelection(DataSelectionTreeModel *selectionModel);
    Q_INVOKABLE QVariantList pieChartData(bool imageDimension) const;
    Q_INVOKABLE QVariantMap  chartData(bool imageDimension) const;

    // Property accessors
    int totalInstances() const
    {
        return total_instances_;
    }

    int totalImages() const
    {
        return total_images_;
    }

    QVariantMap imageChartData() const
    {
        return chartData(true);
    }

    QVariantMap instanceChartData() const
    {
        return chartData(false);
    }

signals:
    void totalInstancesChanged();
    void totalImagesChanged();
    void chartDataChanged();

private:
    struct CategoryStatistics
    {
        int64_t id;
        QString name;
        QString color;
        int     instance_count;
        int     image_count;
        double  instance_percentage;
        double  image_percentage;
    };

    void calculateStatistics(bool applyFilter);
    void calculatePercentages();

    // Statistics calculation methods
    void calculateInstanceStatistics(bool applyFilter);
    void calculateImageStatistics(bool applyFilter);
    void refreshDataInternal(bool applyFilter);
    bool isImageIncluded(int64_t image_id, int64_t label_class_id = -1) const;

    LabelInstancesListModel        *label_instances_;
    LabelClassesListModel          *label_classes_;
    ImageInstancesListModel        *image_instances_;
    DataSelectionTreeModel         *selection_model_{nullptr};
    std::vector<CategoryStatistics> statistics_;
    std::set<int64_t>               selected_dataset_ids_;
    bool                            use_dataset_filter_{false};
    int                             total_instances_;
    int                             total_images_;
};

} // namespace dltool::data
