#pragma once

#include "dltool/data/Export.h"

#include <QString>
#include <QVariantMap>
#include <cstdint>
#include <map>
#include <vector>

namespace dltool::data {

/**
 * @brief 供后台数据导出使用的只读数据源。
 *
 * 实现由 data 模块在提交后台工作前从当前内存数据创建，仅在工作回调生命周期内有效。
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
 * @brief DatasetExportSource 的不可变值实现。
 *
 * 快照在提交后台任务前由 GUI 线程构造，工作线程只读取其中的值，不再访问
 * DataManager、Qt Model 或项目数据库。SnapshotData 只包含导出所需的数据，
 * 因此其生命周期独立于项目对象。
 */
class DATA_API DatasetExportSnapshot final : public DatasetExportSource
{
public:
    struct Dataset
    {
        qint64  id{-1};
        QString name;
    };

    struct Image
    {
        qint64              id{-1};
        qint64              dataset_id{-1};
        QString             path;
        QVariantMap         image_level_label_data;
        std::vector<qint64> label_ids;
    };

    struct Label
    {
        qint64      id{-1};
        qint64      class_id{-1};
        QVariantMap data;
    };

    struct LabelClass
    {
        qint64  id{-1};
        QString name;
        QString color;
        QString group;
    };

    struct SnapshotData
    {
        std::map<qint64, Dataset>    datasets;
        std::map<qint64, Image>      images;
        std::map<qint64, Label>      labels;
        std::map<qint64, LabelClass> label_classes;
    };

    explicit DatasetExportSnapshot(SnapshotData data);

    std::vector<int64_t> allImageIds() const override;
    qint64              imageDatasetId(qint64 image_id) const override;
    QString             imagePath(qint64 image_id) const override;
    QVariantMap         imageLevelLabelData(qint64 image_id) const override;
    std::vector<int64_t> imageLabelIds(qint64 image_id) const override;
    qint64              labelClassId(qint64 label_id) const override;
    QVariantMap         labelData(qint64 label_id) const override;
    QString             labelClassName(qint64 label_class_id) const override;
    QString             labelClassColor(qint64 label_class_id) const override;
    QString             labelClassGroup(qint64 label_class_id) const override;
    QString             datasetName(qint64 dataset_id) const override;

private:
    SnapshotData data_;
};

/**
 * @brief 创建后台导出数据源的请求。
 */
struct DATA_API DatasetExportRequest
{
    std::vector<int64_t> dataset_ids;
};

} // namespace dltool::data
