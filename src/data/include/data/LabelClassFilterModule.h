#pragma once

#include "FilterModule.h"

#include <unordered_set>

namespace dltool::data {

class DataManager;

/**
 * @brief 标注类别过滤模块
 * 
 * 根据标注类别ID过滤标注实例
 * 单选策略：内部只保留一个类别ID
 */
class LabelClassFilterModule : public FilterModule
{
    Q_OBJECT

public:
    explicit LabelClassFilterModule(DataManager *data_manager, QObject *parent = nullptr);
    ~LabelClassFilterModule() override = default;

    void setCriteria(const std::vector<int64_t> &label_class_ids) override;
    void clear() override;
    void setEnabled(bool enabled) override;
    bool isEnabled() const override;
    bool isActive() const override;

    std::unordered_set<int64_t> getActiveCriteria() const override;

    bool isInverted() const override;

    /**
     * @brief 检查标注类别是否匹配
     * @param label_class_id 类别ID
     * @return 如果过滤器禁用或类别匹配条件返回true
     */
    bool passes(int64_t label_class_id) const override;

    void selectAll() override;
    void deselectAll() override;

private:
    std::unordered_set<int64_t> selected_label_class_ids_;
    bool                        enabled_{false};
    bool                        inverted_{false};
};

} // namespace dltool::data
