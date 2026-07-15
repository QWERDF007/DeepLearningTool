#pragma once

#include <QAbstractItemModel>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml>

namespace dltool::data {

class DataManager;
class DataSelectionTreeModel;
class ImageInstancesListModel;
class LabelClassesListModel;
class LabelInstancesListModel;

class DatasetSelectionStatisticsModel : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DatasetSelectionStatisticsModel)

    Q_PROPERTY(DataManager *dataManager READ dataManager WRITE setDataManager NOTIFY dataManagerChanged FINAL)
    Q_PROPERTY(DataSelectionTreeModel *selectionModel READ selectionModel WRITE setSelectionModel NOTIFY
                   selectionModelChanged FINAL)
    Q_PROPERTY(QVariantMap imageChartData READ imageChartData NOTIFY chartDataChanged FINAL)
    Q_PROPERTY(QVariantMap instanceChartData READ instanceChartData NOTIFY chartDataChanged FINAL)
    Q_PROPERTY(int totalInstances READ totalInstances NOTIFY totalInstancesChanged FINAL)
    Q_PROPERTY(int totalImages READ totalImages NOTIFY totalImagesChanged FINAL)

public:
    explicit DatasetSelectionStatisticsModel(QObject *parent = nullptr);
    ~DatasetSelectionStatisticsModel() override = default;

    DataManager *dataManager() const
    {
        return data_manager_;
    }

    void setDataManager(DataManager *data_manager);

    DataSelectionTreeModel *selectionModel() const
    {
        return selection_model_;
    }

    void setSelectionModel(DataSelectionTreeModel *selection_model);

    QVariantMap imageChartData() const
    {
        return chartData(true);
    }

    QVariantMap instanceChartData() const
    {
        return chartData(false);
    }

    int totalInstances() const
    {
        return total_instances_;
    }

    int totalImages() const
    {
        return total_images_;
    }

    Q_INVOKABLE void refresh();

signals:
    void dataManagerChanged();
    void selectionModelChanged();
    void chartDataChanged();
    void totalInstancesChanged();
    void totalImagesChanged();

private:
    void connectDataSources();
    void disconnectDataSources();
    void connectSourceModel(QAbstractItemModel *model);
    void scheduleRefresh();
    QVariantMap chartData(bool image_dimension) const;

    DataManager               *data_manager_{nullptr};
    DataSelectionTreeModel    *selection_model_{nullptr};
    ImageInstancesListModel   *image_instances_{nullptr};
    LabelClassesListModel     *label_classes_{nullptr};
    LabelInstancesListModel   *label_instances_{nullptr};

    QVariantMap image_chart_data_;
    QVariantMap instance_chart_data_;
    int         total_instances_{0};
    int         total_images_{0};
    bool        refresh_pending_{false};
};

} // namespace dltool::data
