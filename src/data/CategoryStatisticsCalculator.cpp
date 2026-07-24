#include "data/CategoryStatisticsCalculator.h"

#include "data/Images.h"
#include "data/LabelClasses.h"
#include "data/Labels.h"

#include <map>
#include <set>
#include <utility>

namespace dltool::data {

namespace {

bool included(const CategoryStatisticsPredicate &predicate, const int64_t image_id, const int64_t label_class_id)
{
    return !predicate || predicate(image_id, label_class_id);
}

} // namespace

CategoryStatisticsResult calculateCategoryStatistics(LabelInstancesListModel *label_instances,
                                                      LabelClassesListModel *label_classes,
                                                      ImageInstancesListModel *image_instances,
                                                      const CategoryStatisticsSource source,
                                                      const CategoryStatisticsPredicate &predicate)
{
    CategoryStatisticsResult result;

    if (label_classes != nullptr)
    {
        result.items.reserve(static_cast<size_t>(label_classes->rowCount()));
        for (int row = 0; row < label_classes->rowCount(); ++row)
        {
            const QModelIndex index = label_classes->index(row, 0);

            CategoryStatisticsItem item;
            item.id    = label_classes->data(index, LabelClassesListModel::LabelClassIdRole).toLongLong();
            item.name  = label_classes->getLabelClassName(item.id);
            item.color = label_classes->getLabelClassColor(item.id);
            result.items.push_back(std::move(item));
        }
    }

    // 与原 CategoryStatisticsModel 保持一致：没有图像模型时不能建立有效的图像/实例统计。
    if (image_instances == nullptr)
        return result;

    std::map<int64_t, int>            category_instance_counts;
    std::map<int64_t, std::set<int64_t>> category_images;
    std::set<int64_t>            images_with_label_instances;
    std::set<int64_t>            all_images;

    const bool use_filtered_data = source == CategoryStatisticsSource::FilteredData;

    if (label_instances != nullptr)
    {
        if (use_filtered_data)
        {
            for (int row = 0; row < label_instances->rowCount(); ++row)
            {
                const QModelIndex index   = label_instances->index(row, 0);
                const int64_t     label_id = label_instances->data(index, LabelInstancesListModel::LabelIdRole)
                                                .toLongLong();
                const int64_t image_id = label_instances->data(index, LabelInstancesListModel::ImageIdRole).toLongLong();
                const int64_t class_id = label_instances->getLabelClassId(label_id);
                if (class_id < 0 || !included(predicate, image_id, class_id))
                    continue;

                ++category_instance_counts[class_id];
                ++result.total_instances;
                images_with_label_instances.insert(image_id);
                category_images[class_id].insert(image_id);
                all_images.insert(image_id);
            }
        }
        else
        {
            for (const auto &[label_id, instance] : label_instances->getAllLabelInstances())
            {
                Q_UNUSED(label_id)
                if (instance == nullptr)
                    continue;

                const int64_t image_id = instance->imageId();
                const int64_t class_id = instance->labelClassId();
                if (class_id < 0 || !included(predicate, image_id, class_id))
                    continue;

                ++category_instance_counts[class_id];
                ++result.total_instances;
                images_with_label_instances.insert(image_id);
                category_images[class_id].insert(image_id);
                all_images.insert(image_id);
            }
        }
    }

    if (use_filtered_data)
    {
        for (int row = 0; row < image_instances->rowCount(); ++row)
        {
            const QModelIndex index = image_instances->index(row, 0);
            const int64_t image_id = image_instances->data(index, ImageInstancesListModel::ImageIdRole).toLongLong();
            const int64_t label_class_id
                = image_instances->data(index, ImageInstancesListModel::ImageLabelClassIdRole).toLongLong();
            if (label_class_id < 0 || !included(predicate, image_id, label_class_id))
                continue;

            // 图像级类别始终参与图像统计；只有实例总数需要避免与同一图像的标注实例重复计数。
            if (images_with_label_instances.find(image_id) == images_with_label_instances.end())
            {
                ++category_instance_counts[label_class_id];
                ++result.total_instances;
            }
            category_images[label_class_id].insert(image_id);
            all_images.insert(image_id);
        }
    }
    else
    {
        for (const auto &[image_id, image] : image_instances->getAllImageInstances())
        {
            if (image == nullptr)
                continue;

            const int64_t label_class_id = image->imageLabelClassId();
            if (label_class_id < 0 || !included(predicate, image_id, label_class_id))
                continue;

            if (images_with_label_instances.find(image_id) == images_with_label_instances.end())
            {
                ++category_instance_counts[label_class_id];
                ++result.total_instances;
            }
            category_images[label_class_id].insert(image_id);
            all_images.insert(image_id);
        }
    }

    result.total_images = static_cast<int>(all_images.size());
    for (CategoryStatisticsItem &item : result.items)
    {
        item.instance_count = category_instance_counts[item.id];
        item.image_count    = static_cast<int>(category_images[item.id].size());
        item.instance_percentage
            = result.total_instances > 0 ? static_cast<double>(item.instance_count) / result.total_instances : 0.0;
        item.image_percentage
            = result.total_images > 0 ? static_cast<double>(item.image_count) / result.total_images : 0.0;
    }

    return result;
}

} // namespace dltool::data
