#pragma once

#include <QObject>
#include <QtQml>
#include <memory>
#include <unordered_set>
#include <vector>

namespace dltool::data {

class ImageInstancesListModel;
class LabelInstancesListModel;
class DatasetsListModel;
class ImageTagsListModel;
class DatasetFilterModule;
class TagFilterModule;

/**
 * @brief 过滤条件结构体
 * 
 * 存储当前激活的过滤条件，包括选中的数据集ID和标签ID
 */
struct FilterCriteria
{
    std::unordered_set<int64_t> dataset_ids; // 选中的数据集ID（空表示全部）
    std::unordered_set<int64_t> tag_ids;     // 选中的标签ID（空表示全部）

    /**
     * @brief 检查过滤条件是否为空
     * @return 如果没有任何过滤条件返回true
     */
    bool isEmpty() const
    {
        return dataset_ids.empty() && tag_ids.empty();
    }
};

/**
 * @brief 全局过滤器类
 * 
 * 管理所有数据模型的过滤逻辑，协调多个过滤模块（数据集过滤、标签过滤等）
 * 使用AND逻辑组合多个过滤模块，使用OR逻辑组合模块内的条件
 */
class GlobalFilter : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(GlobalFilter)
    QML_UNCREATABLE("Can not create GlobalFilter directly!")

    Q_PROPERTY(bool isActive READ isActive NOTIFY filterStateChanged)
    Q_PROPERTY(int activeFilterCount READ activeFilterCount NOTIFY filterStateChanged)
    Q_PROPERTY(QString filterSummary READ filterSummary NOTIFY filterStateChanged)

public:
    GlobalFilter(ImageInstancesListModel *image_model, LabelInstancesListModel *label_model, QObject *parent = nullptr);
    ~GlobalFilter();

    /**
     * @brief 初始化过滤模块
     * @param datasets_model 数据集列表模型
     * @param tags_model 标签列表模型
     * @note 必须在DataManager完全构造后调用
     */
    void initializeFilterModules(DatasetsListModel *datasets_model, ImageTagsListModel *tags_model);

    // 过滤器状态查询
    /**
     * @brief 检查是否有激活的过滤器
     * @return 如果有任何过滤器激活返回true
     */
    bool isActive() const;

    /**
     * @brief 获取激活的过滤器数量
     * @return 激活的过滤器数量
     */
    int activeFilterCount() const;

    /**
     * @brief 获取过滤器摘要信息
     * @return 过滤器摘要字符串
     */
    QString filterSummary() const;

    // 过滤条件管理
    /**
     * @brief 设置数据集过滤条件
     * @param dataset_ids 选中的数据集ID列表
     */
    Q_INVOKABLE void setDatasetFilter(const std::vector<int64_t> &dataset_ids);

    /**
     * @brief 设置标签过滤条件
     * @param tag_ids 选中的标签ID列表
     */
    Q_INVOKABLE void setTagFilter(const std::vector<int64_t> &tag_ids);

    /**
     * @brief 启用/禁用数据集过滤器
     * @param enabled 是否启用
     */
    Q_INVOKABLE void setDatasetFilterEnabled(bool enabled);

    /**
     * @brief 启用/禁用标签过滤器
     * @param enabled 是否启用
     */
    Q_INVOKABLE void setTagFilterEnabled(bool enabled);

    /**
     * @brief 清除所有过滤器
     */
    Q_INVOKABLE void clearAllFilters();

    /**
     * @brief 清除数据集过滤器
     */
    Q_INVOKABLE void clearDatasetFilter();

    /**
     * @brief 清除标签过滤器
     */
    Q_INVOKABLE void clearTagFilter();

    // 获取当前过滤状态
    /**
     * @brief 获取激活的数据集ID列表
     * @return 数据集ID列表
     */
    Q_INVOKABLE std::vector<int64_t> getActiveDatasetIds() const;

    /**
     * @brief 获取激活的标签ID列表
     * @return 标签ID列表
     */
    Q_INVOKABLE std::vector<int64_t> getActiveTagIds() const;

    /**
     * @brief 应用过滤器到数据模型
     * 
     * 根据当前过滤条件更新图像和标注模型的显示内容
     * 如果条件未改变，则跳过过滤操作以提高性能
     */
    void applyFilters();

signals:
    void filterStateChanged(); // 过滤器状态改变信号
    void filterApplied();      // 过滤器应用完成信号

private:
    /**
     * @brief 更新当前过滤条件
     * 
     * 从各个过滤模块收集激活的条件并更新current_criteria_
     */
    void updateFilterCriteria();

    /**
     * @brief 判断图像是否应该被包含在过滤结果中
     * @param image_id 图像ID
     * @return 如果图像通过所有激活的过滤器返回true
     */
    bool shouldIncludeImage(int64_t image_id) const;

    /**
     * @brief 判断标注是否应该被包含在过滤结果中
     * @param label_id 标注ID
     * @return 如果标注关联的图像通过过滤器返回true
     */
    bool shouldIncludeLabel(int64_t label_id) const;

    /**
     * @brief 检查过滤条件是否发生变化
     * @return 如果条件改变返回true
     * 
     * 通过比较current_criteria_和previous_criteria_来避免不必要的过滤操作
     */
    bool hasFilterCriteriaChanged() const;

    ImageInstancesListModel *image_model_{nullptr}; // 图像实例列表模型
    LabelInstancesListModel *label_model_{nullptr}; // 标注实例列表模型

    std::unique_ptr<DatasetFilterModule> dataset_filter_; // 数据集过滤模块
    std::unique_ptr<TagFilterModule>     tag_filter_;     // 标签过滤模块

    FilterCriteria current_criteria_;  // 当前过滤条件
    FilterCriteria previous_criteria_; // 上一次过滤条件（用于避免不必要的过滤）
};

} // namespace dltool::data
