#pragma once

#include "dltool/data/Export.h"

#include <QString>
#include <cstdint>
#include <vector>

namespace dltool::data {

/**
 * @brief 图像级数据集划分输入。
 *
 * image_label_class_id 用于分类和异常检测；label_class_ids 用于目标检测和
 * 语义分割。所有类别信息都以图像为粒度提供，避免把同一图像的多个实例拆到
 * 不同子集中。
 */
struct DATA_API DatasetSplitItem
{
    int64_t              image_id{-1};
    int64_t              image_label_class_id{-1};
    std::vector<int64_t> label_class_ids;
};

struct DATA_API DatasetSplitRatios
{
    double train{0.8};
    double validation{0.0};
    double test{0.2};
    bool   use_validation{false};
};

struct DATA_API DatasetSplitResult
{
    bool                 success{false};
    QString              error;
    std::vector<int64_t> train_image_ids;
    std::vector<int64_t> validation_image_ids;
    std::vector<int64_t> test_image_ids;
};

/**
 * @brief 按任务类型对图像进行可重复、尽量分层的数据集划分。
 *
 * 该类只负责内存中的划分，不创建数据库数据集，也不修改图像归属；数据库
 * 写入由 DataManager 的数据操作接口负责，因此可以独立进行模块测试。
 */
class DATA_API DatasetSplitter final
{
public:
    static bool validateRatios(const DatasetSplitRatios &ratios, QString *error = nullptr);

    static DatasetSplitResult split(const std::vector<DatasetSplitItem> &items, int method,
                                    const DatasetSplitRatios &ratios, uint32_t random_seed = 0);

private:
    DatasetSplitter() = delete;
};

} // namespace dltool::data
