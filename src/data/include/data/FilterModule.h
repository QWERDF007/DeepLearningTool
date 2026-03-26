#pragma once

#include <QObject>
#include <unordered_set>
#include <vector>

namespace dltool::data {

class DataManager;

/**
 * @brief 过滤模块基类
 * 
 * 定义所有过滤模块的通用接口
 * 每个具体的过滤模块（如数据集过滤、标签过滤）都继承此基类
 */
class FilterModule : public QObject
{
    Q_OBJECT

public:
    explicit FilterModule(DataManager *data_manager, QObject *parent = nullptr)
        : QObject(parent)
        , data_manager_(data_manager)
    {
    }

    virtual ~FilterModule() = default;

    /**
     * @brief 设置过滤条件
     * @param ids 要过滤的ID列表
     */
    virtual void setCriteria(const std::vector<int64_t> &ids) = 0;

    /**
     * @brief 清除过滤条件
     */
    virtual void clear() = 0;

    /**
     * @brief 启用/禁用此过滤模块
     * @param enabled 是否启用
     */
    virtual void setEnabled(bool enabled) = 0;

    /**
     * @brief 检查过滤模块是否启用
     * @return 如果启用返回true
     */
    virtual bool isEnabled() const = 0;

    /**
     * @brief 检查过滤器是否激活（启用且有条件）
     * @return 如果过滤器激活返回true
     */
    virtual bool isActive() const = 0;

    /**
     * @brief 获取激活的过滤条件
     * @return 过滤条件ID集合
     */
    virtual std::unordered_set<int64_t> getActiveCriteria() const = 0;

    /**
     * @brief 检查某个项目是否通过此过滤器
     * @param item_id 项目ID
     * @return 如果过滤器禁用或项目匹配条件返回true
     */
    virtual bool passes(int64_t item_id) const = 0;

    /**
     * @brief 选择所有项目
     */
    virtual void selectAll() = 0;

    /**
     * @brief 取消选择所有项目
     */
    virtual void deselectAll() = 0;

protected:
    DataManager *dataManager() const
    {
        return data_manager_;
    }

private:
    DataManager *data_manager_{nullptr};

signals:
    void criteriaChanged();            // 过滤条件改变信号
    void enabledChanged(bool enabled); // 启用状态改变信号
};

} // namespace dltool::data
