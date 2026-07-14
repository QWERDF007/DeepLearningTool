#include "data/CategoryStatisticsModel.h"

#include "data/Images.h"
#include "data/LabelClasses.h"
#include "data/Labels.h"
#include "data/DataSelectionTreeModel.h"

#include <spdlog/spdlog.h>

#include <map>
#include <set>

namespace dltool::data {

CategoryStatisticsModel::CategoryStatisticsModel(LabelInstancesListModel *labelInstances,
                                                 LabelClassesListModel *labelClasses,
                                                 ImageInstancesListModel *imageInstances, QObject *parent)
    : QAbstractListModel(parent)
    , label_instances_(labelInstances)
    , label_classes_(labelClasses)
    , image_instances_(imageInstances)
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
    std::set<int64_t>      images_with_label_instances;
    total_instances_ = 0;

    if (applyFilter)
    {
        // 使用当前筛选后的数据（遍历 rowCount）
        for (int i = 0; i < label_instances_->rowCount(); ++i)
        {
            QModelIndex index    = label_instances_->index(i, 0);
            int64_t     label_id = label_instances_->data(index, LabelInstancesListModel::LabelIdRole).toLongLong();
            int64_t     image_id = label_instances_->data(index, LabelInstancesListModel::ImageIdRole).toLongLong();
            int64_t     class_id = label_instances_->getLabelClassId(label_id);
            if (!isImageIncluded(image_id, class_id))
                continue;

            category_counts[class_id]++;
            images_with_label_instances.insert(image_id);
            total_instances_++;
        }
    }
    else
    {
        // 使用未筛选的完整数据
        const auto &all_instances = label_instances_->getAllLabelInstances();
        for (const auto &[label_id, instance] : all_instances)
        {
            if (instance == nullptr || !isImageIncluded(instance->imageId(), instance->labelClassId()))
                continue;
            int64_t class_id = instance->labelClassId();
            category_counts[class_id]++;
            images_with_label_instances.insert(instance->imageId());
            total_instances_++;
        }
    }

    if (image_instances_ != nullptr)
    {
        const int image_count = applyFilter ? image_instances_->rowCount() : image_instances_->totalCount();
        const std::vector<int64_t> all_image_ids = applyFilter ? std::vector<int64_t>{}
                                                               : image_instances_->getAllImageIds();
        for (int i = 0; i < image_count; ++i)
        {
            int64_t image_id       = -1;
            int64_t label_class_id = -1;
            if (applyFilter)
            {
                const QModelIndex index = image_instances_->index(i, 0);
                image_id = image_instances_->data(index, ImageInstancesListModel::ImageIdRole).toLongLong();
                label_class_id
                    = image_instances_->data(index, ImageInstancesListModel::ImageLabelClassIdRole).toLongLong();
            }
            else
            {
                if (i >= static_cast<int>(all_image_ids.size()))
                    break;
                image_id       = all_image_ids[static_cast<size_t>(i)];
                label_class_id = image_instances_->getImageLabelClassId(image_id);
            }

            if (label_class_id < 0 || images_with_label_instances.find(image_id) != images_with_label_instances.end())
                continue;
            if (!isImageIncluded(image_id, label_class_id))
                continue;

            category_counts[label_class_id]++;
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
            if (!isImageIncluded(image_id, class_id))
                continue;

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
            if (instance == nullptr)
                continue;
            int64_t class_id = instance->labelClassId();
            int64_t image_id = instance->imageId();
            if (!isImageIncluded(image_id, class_id))
                continue;

            category_images[class_id].insert(image_id);
            all_images.insert(image_id);
        }
    }

    if (image_instances_ != nullptr)
    {
        const int image_count = applyFilter ? image_instances_->rowCount() : image_instances_->totalCount();
        const std::vector<int64_t> all_image_ids = applyFilter ? std::vector<int64_t>{}
                                                               : image_instances_->getAllImageIds();
        for (int i = 0; i < image_count; ++i)
        {
            int64_t image_id       = -1;
            int64_t label_class_id = -1;
            if (applyFilter)
            {
                const QModelIndex index = image_instances_->index(i, 0);
                image_id = image_instances_->data(index, ImageInstancesListModel::ImageIdRole).toLongLong();
                label_class_id
                    = image_instances_->data(index, ImageInstancesListModel::ImageLabelClassIdRole).toLongLong();
            }
            else
            {
                if (i >= static_cast<int>(all_image_ids.size()))
                    break;
                image_id       = all_image_ids[static_cast<size_t>(i)];
                label_class_id = image_instances_->getImageLabelClassId(image_id);
            }

            if (label_class_id < 0 || !isImageIncluded(image_id, label_class_id))
                continue;

            category_images[label_class_id].insert(image_id);
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

bool CategoryStatisticsModel::isImageIncluded(const int64_t image_id, const int64_t label_class_id) const
{
    if (image_instances_ == nullptr)
        return false;

    const int64_t dataset_id = image_instances_->getImageDatasetId(image_id);

    if (selection_model_ != nullptr)
    {
        if (label_class_id >= 0)
            return selection_model_->isNodeSelected(dataset_id, label_class_id);
        return selection_model_->isNodeSelected(dataset_id, -1);
    }

    if (!use_dataset_filter_)
        return true;

    return selected_dataset_ids_.find(dataset_id) != selected_dataset_ids_.end();
}

void CategoryStatisticsModel::refreshData(bool applyFilter)
{
    selection_model_ = nullptr;
    selected_dataset_ids_.clear();
    use_dataset_filter_ = false;
    refreshDataInternal(applyFilter);
}

void CategoryStatisticsModel::refreshForDatasets(const QVariantList &datasetIds)
{
    selection_model_ = nullptr;
    selected_dataset_ids_.clear();
    for (const QVariant &value : datasetIds)
    {
        bool          ok         = false;
        const int64_t dataset_id = value.toLongLong(&ok);
        if (ok && dataset_id >= 0)
            selected_dataset_ids_.insert(dataset_id);
    }

    use_dataset_filter_ = true;
    refreshDataInternal(false);
}

void CategoryStatisticsModel::refreshForSelection(DataSelectionTreeModel *selectionModel)
{
    selection_model_ = selectionModel;
    selected_dataset_ids_.clear();
    use_dataset_filter_ = false;
    refreshDataInternal(false);
}

QVariantList CategoryStatisticsModel::pieChartData(const bool imageDimension) const
{
    QVariantList result;
    result.reserve(static_cast<int>(statistics_.size()));

    for (const CategoryStatistics &stat : statistics_)
    {
        const int count = imageDimension ? stat.image_count : stat.instance_count;
        if (count <= 0)
            continue;

        QVariantMap item;
        item.insert(QStringLiteral("label"), stat.name);
        item.insert(QStringLiteral("value"), count);
        item.insert(QStringLiteral("count"), count);
        item.insert(QStringLiteral("color"), stat.color);
        item.insert(QStringLiteral("percentage"),
                    imageDimension ? stat.image_percentage : stat.instance_percentage);
        result.push_back(item);
    }

    return result;
}

QVariantMap CategoryStatisticsModel::chartData(const bool imageDimension) const
{
    QVariantList labels;
    QVariantList values;
    QVariantList colors;

    for (const CategoryStatistics &stat : statistics_)
    {
        const int count = imageDimension ? stat.image_count : stat.instance_count;
        if (count <= 0)
            continue;

        labels.push_back(stat.name);
        values.push_back(count);
        colors.push_back(stat.color);
    }

    QVariantMap dataset;
    dataset.insert(QStringLiteral("label"), imageDimension ? QStringLiteral("图像") : QStringLiteral("实例"));
    dataset.insert(QStringLiteral("data"), values);
    dataset.insert(QStringLiteral("backgroundColor"), colors);
    dataset.insert(QStringLiteral("hoverOffset"), 4);

    QVariantList datasets;
    datasets.push_back(dataset);

    QVariantMap result;
    result.insert(QStringLiteral("labels"), labels);
    result.insert(QStringLiteral("datasets"), datasets);
    return result;
}

void CategoryStatisticsModel::refreshDataInternal(bool applyFilter)
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
        emit chartDataChanged();

        spdlog::debug("CategoryStatisticsModel 刷新完成: applyFilter={}, {} 个类别, {} 个实例, {} 个图像", applyFilter,
                      statistics_.size(), total_instances_, total_images_);
    }
    catch (const std::exception &e)
    {
        spdlog::error("刷新 CategoryStatisticsModel 失败: {}", e.what());
        endResetModel();
    }
}

} // namespace dltool::data
