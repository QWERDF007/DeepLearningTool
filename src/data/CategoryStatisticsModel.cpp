#include "data/CategoryStatisticsModel.h"

#include "data/LabelClasses.h"
#include "data/Labels.h"

#include <spdlog/spdlog.h>

#include <map>
#include <set>

namespace dltool::data {

CategoryStatisticsModel::CategoryStatisticsModel(LabelInstancesListModel *labelInstances,
                                                 LabelClassesListModel *labelClasses, QObject *parent)
    : QAbstractListModel(parent)
    , label_instances_(labelInstances)
    , label_classes_(labelClasses)
    , total_instances_(0)
    , total_images_(0)
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

    return 3; // 3 columns: category name, distribution, count
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

QVariant CategoryStatisticsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(statistics_.size()))
        return QVariant();

    const CategoryStatistics &stat = statistics_[index.row()];

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
        return QVariant();
    }
}

void CategoryStatisticsModel::calculatePercentages()
{
    for (auto &stat : statistics_)
    {
        // Handle division by zero
        stat.instance_percentage
            = total_instances_ > 0 ? static_cast<double>(stat.instance_count) / total_instances_ : 0.0;

        stat.image_percentage = total_images_ > 0 ? static_cast<double>(stat.image_count) / total_images_ : 0.0;
    }
}

void CategoryStatisticsModel::calculateInstanceStatistics(bool applyFilter)
{
    std::map<int64_t, int> category_counts; // category_id -> instance_count
    total_instances_ = 0;

    if (applyFilter)
    {
        // 使用当前筛选后的数据（遍历 rowCount）
        for (int i = 0; i < label_instances_->rowCount(); ++i)
        {
            QModelIndex index    = label_instances_->index(i, 0);
            int64_t     label_id = label_instances_->data(index, LabelInstancesListModel::LabelIdRole).toLongLong();
            int64_t     class_id = label_instances_->getLabelClassId(label_id);

            category_counts[class_id]++;
            total_instances_++;
        }
    }
    else
    {
        // 使用未筛选的完整数据
        const auto &all_instances = label_instances_->getAllLabelInstances();
        for (const auto &[label_id, instance] : all_instances)
        {
            int64_t class_id = instance->labelClassId();
            category_counts[class_id]++;
            total_instances_++;
        }
    }

    // 更新 statistics_ 中的实例计数
    for (auto &stat : statistics_)
    {
        stat.instance_count = category_counts[stat.id];
    }
}

void CategoryStatisticsModel::calculateImageStatistics(bool applyFilter)
{
    std::map<int64_t, std::set<int64_t>> category_images; // category_id -> set of image_ids
    std::set<int64_t>                    all_images;      // 所有包含标签的图像
    total_images_ = 0;

    if (applyFilter)
    {
        // 使用当前筛选后的数据（遍历 rowCount）
        for (int i = 0; i < label_instances_->rowCount(); ++i)
        {
            QModelIndex index    = label_instances_->index(i, 0);
            int64_t     label_id = label_instances_->data(index, LabelInstancesListModel::LabelIdRole).toLongLong();
            int64_t     class_id = label_instances_->getLabelClassId(label_id);
            int64_t     image_id = label_instances_->data(index, LabelInstancesListModel::ImageIdRole).toLongLong();

            category_images[class_id].insert(image_id);
            all_images.insert(image_id);
        }
    }
    else
    {
        // 使用未筛选的完整数据
        const auto &all_instances = label_instances_->getAllLabelInstances();
        for (const auto &[label_id, instance] : all_instances)
        {
            int64_t class_id = instance->labelClassId();
            int64_t image_id = instance->imageId();

            category_images[class_id].insert(image_id);
            all_images.insert(image_id);
        }
    }

    total_images_ = static_cast<int>(all_images.size());

    // 更新 statistics_ 中的图像计数
    for (auto &stat : statistics_)
    {
        stat.image_count = static_cast<int>(category_images[stat.id].size());
    }
}

void CategoryStatisticsModel::refreshData(bool applyFilter)
{
    try
    {
        beginResetModel();

        statistics_.clear();
        total_instances_ = 0;
        total_images_    = 0;

        // 按标签类别顺序初始化 statistics_
        for (int i = 0; i < label_classes_->rowCount(); ++i)
        {
            QModelIndex class_index = label_classes_->index(i, 0);
            int64_t class_id = label_classes_->data(class_index, LabelClassesListModel::LabelClassIdRole).toLongLong();

            CategoryStatistics stat;
            stat.id                  = class_id;
            stat.name                = label_classes_->getLabelClassName(class_id);
            stat.color               = label_classes_->getLabelClassColor(class_id);
            stat.instance_count      = 0;
            stat.image_count         = 0;
            stat.instance_percentage = 0.0;
            stat.image_percentage    = 0.0;
            statistics_.push_back(stat);
        }

        // 计算实例统计
        calculateInstanceStatistics(applyFilter);

        // 计算图像统计
        calculateImageStatistics(applyFilter);

        // 计算百分比
        calculatePercentages();

        endResetModel();

        emit totalInstancesChanged();
        emit totalImagesChanged();

        spdlog::debug("CategoryStatisticsModel refreshed with applyFilter={}, {} categories, {} instances, {} images",
                      applyFilter, statistics_.size(), total_instances_, total_images_);
    }
    catch (const std::exception &e)
    {
        spdlog::error("Failed to refresh CategoryStatisticsModel: {}", e.what());
        endResetModel();
    }
}

} // namespace dltool::data
