#pragma once

#include "FilterModule.h"

#include <unordered_set>

namespace dltool::data {

class DataManager;

/**
 * @brief 标签过滤模块
 * 
 * 根据标签ID过滤图像
 * 使用OR逻辑：图像拥有任一选中的标签即通过过滤
 */
class TagFilterModule : public FilterModule
{
    Q_OBJECT

public:
    explicit TagFilterModule(DataManager *data_manager, QObject *parent = nullptr);
    ~TagFilterModule() override = default;

    void                        setCriteria(const std::vector<int64_t> &tag_ids) override;
    void                        clear() override;
    void                        setEnabled(bool enabled) override;
    bool                        isEnabled() const override;
    bool                        isActive() const override;
    std::unordered_set<int64_t> getActiveCriteria() const override;
    bool                        isInverted() const override;

    /**
     * @brief 检查图像是否拥有任一选中的标签
     * @param image_id 图像ID
     * @return 如果过滤器禁用或图像匹配条件返回true
     */
    bool passes(int64_t image_id) const override;

    /**
     * @brief 选择所有标签
     */
    void selectAll() override;

    /**
     * @brief 取消选择所有标签
     */
    void deselectAll() override;

private:
    std::unordered_set<int64_t> selected_tag_ids_; // 选中的标签ID（使用unordered_set提高查找性能）
    bool                        enabled_{false};   // 过滤模块启用状态
    bool                        inverted_{false};
};

} // namespace dltool::data
