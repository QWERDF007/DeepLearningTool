#pragma once

#include "dltool/data/Export.h"

#include <QString>
#include <QVariantMap>
#include <cstdint>
#include <vector>

namespace dltool::data {

/**
 * @brief 供后台数据导出使用的只读数据源。
 *
 * 实现由 data 模块内部基于 DataManager 的内存数据创建，仅在工作回调生命周期内有效。
 * 调用方可以遍历选中数据集的图像和标注，但不需要访问 DataManager 或数据库。
 */
class DATA_API DatasetExportSource
{
public:
    virtual ~DatasetExportSource() = default;

    /**
     * @brief 按升序返回选中范围内的图像 ID，保证导出文件稳定。
     * @return 图像 ID 列表。
     */
    virtual std::vector<int64_t> allImageIds() const = 0;
    virtual qint64              imageDatasetId(qint64 image_id) const = 0;
    virtual QString             imagePath(qint64 image_id) const = 0;
    virtual QVariantMap         imageLevelLabelData(qint64 image_id) const = 0;
    /**
     * @brief 按升序返回图像的标注 ID，保证导出文件稳定。
     * @param image_id 图像 ID。
     * @return 标注 ID 列表。
     */
    virtual std::vector<int64_t> imageLabelIds(qint64 image_id) const = 0;
    virtual qint64              labelClassId(qint64 label_id) const = 0;
    virtual QVariantMap         labelData(qint64 label_id) const = 0;
    virtual QString             labelClassName(qint64 label_class_id) const = 0;
    virtual QString             labelClassColor(qint64 label_class_id) const = 0;
    virtual QString             labelClassGroup(qint64 label_class_id) const = 0;
    virtual QString             datasetName(qint64 dataset_id) const = 0;
};

/**
 * @brief 创建后台导出数据源的请求。
 */
struct DATA_API DatasetExportRequest
{
    std::vector<int64_t> dataset_ids;
};

} // namespace dltool::data
