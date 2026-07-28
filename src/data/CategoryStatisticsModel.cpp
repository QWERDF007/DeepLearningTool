#include "data/CategoryStatisticsModel.h"

#include "data/GlobalFilter.h"
#include "data/Images.h"
#include "data/LabelClasses.h"
#include "data/Labels.h"

#include <spdlog/spdlog.h>

namespace dltool::data {

CategoryStatisticsModel::CategoryStatisticsModel(LabelInstancesListModel *labelInstances,
                                                 LabelClassesListModel *labelClasses,
                                                 ImageInstancesListModel *imageInstances, QObject *parent,
                                                 GlobalFilter *filter)
    : QAbstractListModel(parent)
    , label_instances_(labelInstances)
    , label_classes_(labelClasses)
    , image_instances_(imageInstances)
    , filter_(filter)
{
}

int CategoryStatisticsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(statistics_.size());
}

int CategoryStatisticsModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return 3;
}

QHash<int, QByteArray> CategoryStatisticsModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[CategoryIdRole]         = "categoryId";
    roles[CategoryNameRole]       = "categoryName";
    roles[CategoryColorRole]      = "categoryColor";
    roles[InstanceCountRole]      = "instanceCount";
    roles[ImageCountRole]         = "imageCount";
    roles[InstancePercentageRole] = "instancePercentage";
    roles[ImagePercentageRole]    = "imagePercentage";
    return roles;
}

QVariant CategoryStatisticsModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(statistics_.size()))
        return {};

    const CategoryStatisticsItem &stat = statistics_[static_cast<size_t>(index.row())];
    switch (role)
    {
    case CategoryIdRole:
        return QVariant::fromValue(stat.id);
    case CategoryNameRole:
        return stat.name;
    case CategoryColorRole:
        return stat.color;
    case InstanceCountRole:
        return stat.instance_count;
    case ImageCountRole:
        return stat.image_count;
    case InstancePercentageRole:
        return stat.instance_percentage;
    case ImagePercentageRole:
        return stat.image_percentage;
    default:
        return {};
    }
}

void CategoryStatisticsModel::refreshData(const bool applyFilter)
{
    try
    {
        CategoryStatisticsPredicate      image_predicate;
        CategoryStatisticsLabelPredicate label_predicate;
        if (applyFilter && filter_ != nullptr)
        {
            image_predicate = [this](const int64_t image_id, const int64_t)
            {
                return filter_->acceptsImage(image_id);
            };
            label_predicate = [this](const int64_t label_id, const int64_t, const int64_t)
            {
                return filter_->acceptsLabel(label_id);
            };
        }

        const CategoryStatisticsResult result
            = calculateCategoryStatistics(label_instances_, label_classes_, image_instances_,
                                          applyFilter ? CategoryStatisticsSource::FilteredData
                                                      : CategoryStatisticsSource::AllData,
                                          image_predicate, label_predicate);

        beginResetModel();
        statistics_      = result.items;
        total_instances_ = result.total_instances;
        total_images_    = result.total_images;
        endResetModel();

        emit totalInstancesChanged();
        emit totalImagesChanged();

        spdlog::debug("CategoryStatisticsModel 刷新完成: applyFilter={}, {} 个类别, {} 个实例, {} 个图像",
                      applyFilter, statistics_.size(), total_instances_, total_images_);
    }
    catch (const std::exception &e)
    {
        spdlog::error("刷新 CategoryStatisticsModel 失败: {}", e.what());
    }
}

} // namespace dltool::data
