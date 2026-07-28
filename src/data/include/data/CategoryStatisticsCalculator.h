#pragma once

#include <QString>

#include <cstdint>
#include <functional>
#include <vector>

namespace dltool::data {

class ImageInstancesListModel;
class LabelClassesListModel;
class LabelInstancesListModel;

enum class CategoryStatisticsSource
{
    AllData,
    FilteredData,
};

struct CategoryStatisticsItem
{
    int64_t id{-1};
    QString name;
    QString color;
    int     instance_count{0};
    int     image_count{0};
    double  instance_percentage{0.0};
    double  image_percentage{0.0};
};

struct CategoryStatisticsResult
{
    std::vector<CategoryStatisticsItem> items;
    int                                 total_instances{0};
    int                                 total_images{0};
};

using CategoryStatisticsPredicate = std::function<bool(int64_t image_id, int64_t label_class_id)>;

// Label-level filtering needs the label ID as well.  In particular, the
// global filter's "label search result" condition cannot be evaluated from an
// image ID and class ID alone.
using CategoryStatisticsLabelPredicate
    = std::function<bool(int64_t label_id, int64_t image_id, int64_t label_class_id)>;

/**
 * @brief 计算类别的实例和图像统计。
 *
 * 该类只负责统计数据，不负责 QAbstractItemModel 或 QML 图表格式。
 * predicate 用于限制统计范围；未提供时统计所有数据。
 */
CategoryStatisticsResult calculateCategoryStatistics(LabelInstancesListModel *label_instances,
                                                      LabelClassesListModel *label_classes,
                                                      ImageInstancesListModel *image_instances,
                                                      CategoryStatisticsSource source,
                                                      const CategoryStatisticsPredicate &predicate = {},
                                                      const CategoryStatisticsLabelPredicate &label_predicate = {});

} // namespace dltool::data
