#pragma once

#include <QObject>
#include <QtQml>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dltool::data {

class DataManager;
class ImageInstancesListModel;
class LabelInstancesListModel;
class DatasetsListModel;
class ImageTagsListModel;
class LabelClassesListModel;
class DatasetFilterModule;
class TagFilterModule;
class LabelClassFilterModule;
class ImageLabelClassFilterModule;
class ImageSearchFilterModule;
class LabelSearchFilterModule;
class FilterModule;

struct FilterCriteria
{
    std::unordered_set<int64_t> dataset_ids;
    std::unordered_set<int64_t> tag_ids;
    std::unordered_set<int64_t> label_class_ids;
    std::unordered_set<int64_t> image_label_class_ids;
    std::unordered_set<int64_t> image_search_ids;
    std::unordered_set<int64_t> label_search_ids;
    bool                        dataset_inverted{false};
    bool                        tag_inverted{false};
    bool                        label_class_inverted{false};
    bool                        image_label_class_inverted{false};
    bool                        image_search_inverted{false};
    bool                        label_search_inverted{false};

    /**
     * @brief 检查过滤条件是否为空
     * @return 如果没有任何过滤条件返回true
     */
    bool isEmpty() const
    {
        return dataset_ids.empty() && tag_ids.empty() && label_class_ids.empty() && image_label_class_ids.empty()
            && image_search_ids.empty() && label_search_ids.empty() && !dataset_inverted && !tag_inverted
            && !label_class_inverted && !image_label_class_inverted && !image_search_inverted && !label_search_inverted;
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
    Q_PROPERTY(bool hasImageSearchResults READ hasImageSearchResults NOTIFY filterStateChanged)
    Q_PROPERTY(bool imageSearchFilterEnabled READ imageSearchFilterEnabled NOTIFY filterStateChanged)
    Q_PROPERTY(int imageSearchResultCount READ imageSearchResultCount NOTIFY filterStateChanged)
    Q_PROPERTY(bool hasLabelSearchResults READ hasLabelSearchResults NOTIFY filterStateChanged)
    Q_PROPERTY(bool labelSearchFilterEnabled READ labelSearchFilterEnabled NOTIFY filterStateChanged)
    Q_PROPERTY(int labelSearchResultCount READ labelSearchResultCount NOTIFY filterStateChanged)

public:
    /**
     * @brief 过滤器类型枚举
     * 
     * 用于标识不同的过滤模块类型，提供类型安全的过滤器标识
     */
    enum class FilterType
    {
        Dataset,         // 数据集过滤器
        Tag,             // 标签过滤器
        LabelClass,      // 标注类别过滤器（仅作用于标注实例）
        ImageLabelClass, // 标注类别过滤器（作用于图像：图像中包含选中类别实例则保留）
        ImageSearch,
        LabelSearch,
    };
    Q_ENUM(FilterType)

    GlobalFilter(DataManager *data_manager, QObject *parent = nullptr);
    ~GlobalFilter();

    /**
     * @brief 初始化过滤模块
     * @param data_manager DataManager实例
     * @note 必须在DataManager完全构造后调用
     */
    void initializeFilterModules(DataManager *data_manager);

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

    // 通用过滤器接口方法

    /**
     * @brief 设置过滤条件（通用方法）
     * @param type 过滤器类型
     * @param ids 选中的ID列表
     */
    Q_INVOKABLE void setFilter(FilterType type, const std::vector<int64_t> &ids);

    /**
     * @brief 启用/禁用过滤器（通用方法）
     * @param type 过滤器类型
     * @param enabled 是否启用
     */
    Q_INVOKABLE void setFilterEnabled(FilterType type, bool enabled);

    /**
     * @brief 清除过滤器（通用方法）
     * @param type 过滤器类型
     */
    Q_INVOKABLE void clearFilter(FilterType type);

    Q_INVOKABLE void selectAll(FilterType type);

    Q_INVOKABLE void deselectAll(FilterType type);
    /**
     * @brief 获取激活的ID列表（通用方法）
     * @param type 过滤器类型
     * @return ID列表
     */
    Q_INVOKABLE std::vector<int64_t> getActiveIds(FilterType type) const;

    /**
     * @brief 清除所有过滤器
     */
    Q_INVOKABLE void clearAllFilters();

    void refresh();

    /**
     * @brief 启用或禁用图像搜索过滤器
     * @param enabled 是否启用；无搜索结果时会被强制设为 false
     */
    Q_INVOKABLE void setImageSearchFilterEnabled(bool enabled);

    /**
     * @brief 清除图像搜索结果并关闭搜索过滤
     */
    Q_INVOKABLE void clearImageSearchResults();

    /**
     * @brief 设置图像搜索结果并更新过滤条件
     * @param image_ids 搜索命中的图像 ID 列表
     * @param enable_filter 是否在设置结果后启用搜索过滤；列表为空时不会启用
     */
    void setImageSearchResults(const std::vector<int64_t> &image_ids, bool enable_filter);

    Q_INVOKABLE void setLabelSearchFilterEnabled(bool enabled);
    Q_INVOKABLE void clearLabelSearchResults();
    void             setLabelSearchResults(const std::vector<int64_t> &label_ids, bool enable_filter);

    /**
     * @brief 是否存在图像搜索结果
     * @return 有非空搜索结果时返回 true
     */
    bool hasImageSearchResults() const;

    /**
     * @brief 图像搜索过滤器是否已启用
     * @return 过滤器处于启用状态时返回 true
     */
    bool imageSearchFilterEnabled() const;

    /**
     * @brief 获取图像搜索结果数量
     * @return 当前搜索结果中的图像数量
     */
    int imageSearchResultCount() const;

    bool hasLabelSearchResults() const;
    bool labelSearchFilterEnabled() const;
    int  labelSearchResultCount() const;

    /**
     * @brief 应用过滤器到数据模型
     * 
     * 根据当前过滤条件更新图像和标注模型的显示内容
     * 如果条件未改变，通常会跳过过滤操作以提高性能
     */
    void applyFilters();

    /**
     * @brief 获取当前过滤条件
     * @return 当前的过滤条件结构
     */
    const FilterCriteria &getCurrentCriteria() const
    {
        return current_criteria_;
    }

signals:
    void filterStateChanged(); // 过滤器状态改变信号
    void filterApplied();      // 过滤器应用完成信号

private:
    /**
     * @brief 获取指定类型的过滤模块
     * @param type 过滤器类型
     * @return 过滤模块指针，如果类型无效返回nullptr
     */
    FilterModule *getFilterModule(FilterType type) const;

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

    DataManager *data_manager_{nullptr}; // 数据管理器

    std::unique_ptr<DatasetFilterModule>         dataset_filter_;           // 数据集过滤模块
    std::unique_ptr<TagFilterModule>             tag_filter_;               // 标签过滤模块
    std::unique_ptr<LabelClassFilterModule>      label_class_filter_;       // 标注类别过滤模块
    std::unique_ptr<ImageLabelClassFilterModule> image_label_class_filter_; // 图像级标注类别过滤模块
    std::unique_ptr<ImageSearchFilterModule>     image_search_filter_;
    std::unique_ptr<LabelSearchFilterModule>     label_search_filter_;

    std::unordered_map<FilterType, FilterModule *> filter_modules_; // 过滤模块映射表

    FilterCriteria current_criteria_;  // 当前过滤条件
    FilterCriteria previous_criteria_; // 上一次过滤条件（用于避免不必要的过滤）

    bool force_apply_{false}; // 强制重新应用过滤（跳过条件未变的优化）
};

} // namespace dltool::data
