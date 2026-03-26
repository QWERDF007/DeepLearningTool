#pragma once

#include "FilterModule.h"

#include <unordered_set>

namespace dltool::data {

class DataManager;

/**
 * @brief 数据集过滤模块
 * 
 * 根据数据集ID过滤图像
 * 使用OR逻辑：图像属于任一选中的数据集即通过过滤
 */
class DatasetFilterModule : public FilterModule
{
    Q_OBJECT

public:
    explicit DatasetFilterModule(DataManager *data_manager, QObject *parent = nullptr);
    ~DatasetFilterModule() override = default;

    void                        setCriteria(const std::vector<int64_t> &dataset_ids) override;
    void                        clear() override;
    void                        setEnabled(bool enabled) override;
    bool                        isEnabled() const override;
    bool                        isActive() const override;
    std::unordered_set<int64_t> getActiveCriteria() const override;

    /**
     * @brief 检查图像是否属于选中的数据集
     * @param image_id 图像ID
     * @return 如果过滤器禁用或图像匹配条件返回true
     */
    bool passes(int64_t image_id) const override;

    /**
     * @brief 选择所有数据集
     */
    void selectAll() override;

    /**
     * @brief 取消选择所有数据集
     */
    void deselectAll() override;

private:
    std::unordered_set<int64_t> selected_dataset_ids_; // 选中的数据集ID（使用unordered_set提高查找性能）
    bool                        enabled_{false};       // 过滤模块启用状态
};

} // namespace dltool::data
