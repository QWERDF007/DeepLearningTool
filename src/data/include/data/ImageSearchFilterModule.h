#pragma once

#include "FilterModule.h"

#include <unordered_set>

namespace dltool::data {

class DataManager;

/**
 * @brief 图像搜索过滤模块
 *
 * 保存相似度搜索返回的图像 ID 集合，启用后仅显示命中结果中的图像。
 * 由 GlobalFilter 统一管理，与数据集、标签等过滤模块按 AND 逻辑组合。
 */
class ImageSearchFilterModule : public FilterModule
{
    Q_OBJECT

public:
    explicit ImageSearchFilterModule(DataManager *data_manager, QObject *parent = nullptr);
    ~ImageSearchFilterModule() override = default;

    /**
     * @brief 设置搜索命中的图像 ID 列表
     * @param image_ids 命中图像的 ID 列表
     */
    void setCriteria(const std::vector<int64_t> &image_ids) override;

    /**
     * @brief 清除所有搜索结果
     */
    void clear() override;

    /**
     * @brief 启用或禁用本过滤模块
     * @param enabled 是否启用
     */
    void setEnabled(bool enabled) override;

    /**
     * @brief 本过滤模块是否已启用
     * @return 启用时返回 true
     */
    bool isEnabled() const override;

    /**
     * @brief 过滤器是否处于激活状态
     * @return 已启用且存在非空搜索结果时返回 true
     */
    bool isActive() const override;

    /**
     * @brief 获取当前生效的过滤条件
     * @return 启用时返回命中图像 ID 集合，否则返回空集合
     */
    std::unordered_set<int64_t> getActiveCriteria() const override;
    bool                        isInverted() const override;

    /**
     * @brief 判断指定图像是否通过过滤
     * @param image_id 图像 ID
     * @return 未启用或 ID 在命中集合内时返回 true
     */
    bool passes(int64_t image_id) const override;

    /**
     * @brief 将条件设为当前项目的全部图像
     *
     * 用于“全选”场景：将图库中所有图像 ID 加入命中集合。
     */
    void selectAll() override;

    /**
     * @brief 清空所有命中条件
     */
    void deselectAll() override;

    /**
     * @brief 获取搜索结果中的图像数量
     * @return 命中图像 ID 的数量
     */
    int resultCount() const;

    /**
     * @brief 是否存在非空的搜索结果
     * @return 命中集合非空时返回 true
     */
    bool hasResults() const;

private:
    std::unordered_set<int64_t> result_image_ids_;

    bool enabled_{false};
    bool inverted_{false};
};

} // namespace dltool::data
