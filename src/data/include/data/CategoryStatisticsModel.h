#pragma once

#include <QAbstractListModel>
#include <QtQml>
#include <vector>

namespace dltool::data {
class DataManager;
}

namespace dltool::data {

class LabelInstancesListModel;
class LabelClassesListModel;

class CategoryStatisticsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(CategoryStatisticsModel)
    QML_UNCREATABLE("Cannot create CategoryStatisticsModel directly!")

    Q_PROPERTY(int totalInstances READ totalInstances NOTIFY totalInstancesChanged)
    Q_PROPERTY(int totalImages READ totalImages NOTIFY totalImagesChanged)

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
                                     QObject *parent = nullptr);
    ~CategoryStatisticsModel() = default;

    // QAbstractListModel interface
    int                    rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int                    columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant               data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Data refresh method
    Q_INVOKABLE void refreshData(bool applyFilter);

    // Property accessors
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

    LabelInstancesListModel        *label_instances_;
    LabelClassesListModel          *label_classes_;
    std::vector<CategoryStatistics> statistics_;
    int                             total_instances_;
    int                             total_images_;
};

} // namespace dltool::data
