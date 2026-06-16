#pragma once

#include "FilterModule.h"

#include <unordered_set>

namespace dltool::data {

class DataManager;

/**
 * @brief 标注搜索过滤模块
 *
 * 保存标注搜索返回的标注 ID 集合。启用后只显示命中的标注，同时只保留这些标注所属的图像。
 */
class LabelSearchFilterModule : public FilterModule
{
    Q_OBJECT

public:
    explicit LabelSearchFilterModule(DataManager *data_manager, QObject *parent = nullptr);
    ~LabelSearchFilterModule() override = default;

    void setCriteria(const std::vector<int64_t> &label_ids) override;
    void clear() override;
    void setEnabled(bool enabled) override;
    bool isEnabled() const override;
    bool isActive() const override;
    std::unordered_set<int64_t> getActiveCriteria() const override;
    bool isInverted() const override;
    bool passes(int64_t image_id) const override;
    bool passesLabel(int64_t label_id) const;
    void selectAll() override;
    void deselectAll() override;

    int  resultCount() const;
    bool hasResults() const;

private:
    void rebuildImageIds();

    std::unordered_set<int64_t> result_label_ids_;
    std::unordered_set<int64_t> result_image_ids_;

    bool enabled_{false};
    bool inverted_{false};
};

} // namespace dltool::data
