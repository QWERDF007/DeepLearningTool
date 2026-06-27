#pragma once

#include "dltool/feature/Export.h"

#include <QString>
#include <QVariantMap>
#include <cstdint>
#include <vector>

namespace dltool::feature {

class FEATURE_API ImageSearchDataProvider
{
public:
    virtual ~ImageSearchDataProvider() = default;

    /**
     * @brief 获取当前选中的图像 ID 列表
     * @return 选中的图像 ID 列表
     */
    virtual std::vector<int64_t> selectedImageIds() const = 0;

    /**
     * @brief 获取所有图像 ID
     * @return 图像 ID 列表
     */
    virtual std::vector<int64_t> allImageIds() const = 0;

    /**
     * @brief 获取指定图像的路径
     * @param image_id 图像 ID
     * @return 图像文件路径
     */
    virtual QString imagePath(int64_t image_id) const = 0;

    /**
     * @brief 获取图像所属的数据集 ID
     * @param image_id 图像 ID
     * @return 数据集 ID
     */
    virtual int64_t imageDatasetId(int64_t image_id) const = 0;

    /**
     * @brief 获取数据库路径
     * @return 数据库文件路径
     */
    virtual QString databasePath() const = 0;

    /**
     * @brief 获取所有标注 ID
     * @return 标注 ID 列表
     */
    virtual std::vector<int64_t> allLabelIds() const = 0;

    /**
     * @brief 获取标注所属的图像 ID
     * @param label_id 标注 ID
     * @return 图像 ID
     */
    virtual int64_t labelImageId(int64_t label_id) const = 0;

    /**
     * @brief 获取标注数据
     * @param label_id 标注 ID
     * @return 包含标注信息的 QVariantMap
     */
    virtual QVariantMap labelData(int64_t label_id) const = 0;

    /// 清除图像搜索结果
    virtual void clearImageSearchResults() = 0;

    /**
     * @brief 设置图像搜索结果
     * @param image_ids 结果图像 ID 列表
     * @param enable_filter 是否启用过滤
     */
    virtual void setImageSearchResults(const std::vector<int64_t> &image_ids, bool enable_filter) = 0;

    /// 清除标注搜索结果
    virtual void clearLabelSearchResults() = 0;

    /**
     * @brief 设置标注搜索结果
     * @param label_ids 结果标注 ID 列表
     * @param enable_filter 是否启用过滤
     */
    virtual void setLabelSearchResults(const std::vector<int64_t> &label_ids, bool enable_filter) = 0;
};

} // namespace dltool::feature
