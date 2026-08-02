#pragma once

#include <QObject>
#include <QStringList>
#include <QtGlobal>
#include <QtQml>

#include <array>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace dltool::data {

class DataManager;
class ImageInstancesListModel;
class LabelInstancesListModel;

/**
 * @brief 全局筛选条件。
 *
 * 该对象只保存用户选择的筛选条件，不保存图像或标注的可见 ID 列表。
 * 可见行由 ImageInstancesViewModel 和 LabelInstancesViewModel 两个代理模型
 * 根据这些条件即时映射。
 */
class GlobalFilter final : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(GlobalFilter)
    QML_UNCREATABLE("Can not create GlobalFilter directly!")

    Q_PROPERTY(bool isActive READ isActive NOTIFY filterStateChanged)
    Q_PROPERTY(int activeFilterCount READ activeFilterCount NOTIFY filterStateChanged)
    Q_PROPERTY(QString fileNameFilterText READ fileNameFilterText WRITE setFileNameFilterText NOTIFY filterStateChanged)

public:
    /**
     * @brief 筛选维度。
     */
    enum class FilterType
    {
        Dataset,
        Tag,
        LabelClass,
        ImageLabelClass,
        Custom,
    };
    Q_ENUM(FilterType)

    /**
     * @brief 自定义筛选条件。
     */
    enum class CustomCondition : qint64
    {
        DuplicateFileName = 1,
        DuplicatePath     = 2,
        UniqueFileName    = 3,
        ImageSearchResult = 4,
        LabelSearchResult = 5,
    };
    Q_ENUM(CustomCondition)

    struct CustomConditionSpec
    {
        int64_t id{-1};  ///< 条件 ID。
        QString text;    ///< 条件显示文本。
    };

    /**
     * @brief 构造全局筛选器。
     * @param data_manager 数据管理器。
     * @param parent 父对象。
     */
    explicit GlobalFilter(DataManager *data_manager, QObject *parent = nullptr);

    /**
     * @brief 返回可供 UI 展示的自定义筛选条件。
     * @return 自定义条件列表。
     */
    static std::vector<CustomConditionSpec> customConditions();

    /**
     * @brief 设置一个筛选维度的已选 ID。
     * @param type 筛选维度。
     * @param ids 已选 ID。
     */
    Q_INVOKABLE void setFilter(FilterType type, const std::vector<int64_t> &ids);

    /**
     * @brief 启用或停用一个筛选维度。
     * @param type 筛选维度。
     * @param enabled 是否启用。
     */
    Q_INVOKABLE void setFilterEnabled(FilterType type, bool enabled);

    /**
     * @brief 查询一个筛选维度是否启用。
     * @param type 筛选维度。
     * @return 是否启用。
     */
    Q_INVOKABLE bool isFilterEnabled(FilterType type) const;

    /**
     * @brief 查询一个筛选维度是否为反选。
     * @param type 筛选维度。
     * @return 是否反选。
     */
    Q_INVOKABLE bool isFilterInverted(FilterType type) const;

    /**
     * @brief 清空一个筛选维度的条件。
     * @param type 筛选维度。
     */
    Q_INVOKABLE void clearFilter(FilterType type);

    /**
     * @brief 选择一个筛选维度的全部可用项。
     * @param type 筛选维度。
     */
    Q_INVOKABLE void selectAll(FilterType type);

    /**
     * @brief 取消选择一个筛选维度的全部可用项。
     * @param type 筛选维度。
     */
    Q_INVOKABLE void deselectAll(FilterType type);

    /**
     * @brief 返回一个筛选维度当前保存的 ID。
     * @param type 筛选维度。
     * @return 已选 ID。
     */
    Q_INVOKABLE std::vector<int64_t> getActiveIds(FilterType type) const;

    /**
     * @brief 清空并停用所有筛选条件。
     */
    Q_INVOKABLE void clearAllFilters();

    /**
     * @brief 使代理模型重新计算筛选结果。
     *
     * 数据关系发生变化后调用；不创建完整可见列表快照。
     */
    void refresh();

    /**
     * @brief 清除图像搜索结果。
     */
    Q_INVOKABLE void clearImageSearchResults();

    /**
     * @brief 设置图像搜索结果。
     * @param image_ids 命中的图像 ID。
     * @param enable_filter 是否同时启用该自定义条件。
     */
    void setImageSearchResults(const std::vector<int64_t> &image_ids, bool enable_filter);

    /**
     * @brief 清除标注搜索结果。
     */
    Q_INVOKABLE void clearLabelSearchResults();

    /**
     * @brief 设置标注搜索结果。
     * @param label_ids 命中的标注 ID。
     * @param enable_filter 是否同时启用该自定义条件。
     */
    void setLabelSearchResults(const std::vector<int64_t> &label_ids, bool enable_filter);

    /**
     * @brief 返回文件名筛选文本。
     * @return 当前筛选文本。
     */
    QString fileNameFilterText() const;

    /**
     * @brief 设置文件名筛选文本。
     * @param text 筛选文本。
     */
    Q_INVOKABLE void setFileNameFilterText(const QString &text);

    /**
     * @brief 判断图像是否应显示。
     * @param image_id 图像 ID。
     * @return 图像是否通过当前筛选。
     */
    Q_INVOKABLE bool acceptsImage(int64_t image_id) const;

    /**
     * @brief 判断标注是否应显示。
     * @param label_id 标注 ID。
     * @return 标注是否通过当前筛选。
     */
    Q_INVOKABLE bool acceptsLabel(int64_t label_id) const;

    /**
     * @brief 判断一个标签类别 ID 是否通过当前 LabelClass 过滤。
     *
     * 评估结果中的实例记录可能只保存类别 ID，无法反查项目数据库中的
     * label ID；该只读查询复用 GlobalFilter 原有的反选、空选择语义。
     */
    Q_INVOKABLE bool acceptsLabelClassId(int64_t label_class_id) const;
    Q_INVOKABLE bool isLabelClassFilterEnabled() const;
    Q_INVOKABLE bool isLabelClassFilterInverted() const;

    /**
     * @brief 是否存在启用的筛选条件。
     * @return 是否存在筛选条件。
     */
    Q_INVOKABLE bool isActive() const;

    /**
     * @brief 返回启用的筛选条件数量。
     * @return 条件数量。
     */
    int activeFilterCount() const;

    /**
     * @brief 返回供派生页面展示的当前过滤条件摘要。
     *
     * 该摘要只读取过滤器状态和现有数据模型，不复制过滤条件或可见 ID 列表。
     */
    Q_INVOKABLE QString description() const;

signals:
    /**
     * @brief 筛选状态变化。
     */
    void filterStateChanged();

    /**
     * @brief 条件变化，需要代理模型重新筛选。
     */
    void filterChanged();

    /**
     * @brief 代理模型筛选通知已发出。
     */
    void filterApplied();

    /**
     * @brief 搜索结果可用性变化。
     * @param has_image_search_results 是否存在图像搜索结果。
     * @param has_label_search_results 是否存在标注搜索结果。
     */
    void customFilterSearchResultsChanged(bool has_image_search_results, bool has_label_search_results);

private:
    struct IdFilter
    {
        std::unordered_set<int64_t> ids; ///< 已选 ID。
        bool enabled{false};             ///< 是否启用。
        bool inverted{false};            ///< 是否反选。
    };

    static constexpr size_t kFilterCount = static_cast<size_t>(FilterType::Custom) + 1;

    IdFilter       &filter(FilterType type);
    const IdFilter &filter(FilterType type) const;
    ImageInstancesListModel *imageSource() const;
    LabelInstancesListModel *labelSource() const;

    /**
     * @brief 直接收集一个筛选维度可用的 ID。
     * @param type 筛选维度。
     * @param ids 输出 ID 集合。
     */
    void collectAvailableIds(FilterType type, std::unordered_set<int64_t> &ids) const;
    bool passesIdFilter(const IdFilter &filter, int64_t id) const;
    bool acceptsImageWithoutCustom(int64_t image_id) const;
    bool acceptsCustomImage(int64_t image_id) const;
    bool acceptsCustomLabel(int64_t label_id, int64_t image_id) const;
    bool matchesTags(int64_t image_id, const std::unordered_set<int64_t> &tag_ids) const;
    bool matchesImageLabelClasses(int64_t image_id, const std::unordered_set<int64_t> &label_class_ids) const;
    bool matchesCustomImageCondition(int64_t image_id, int64_t condition_id) const;
    bool customConditionAvailable(int64_t condition_id) const;
    bool usesRegularCustomCondition() const;
    void rebuildDuplicateIndexes() const;
    void invalidateDuplicateIndexes();
    void notifyFilterChanged();
    void notifyStateChanged(bool notify_search_results);
    void updateCustomEnabledAfterSearchRemoval();

    DataManager *data_manager_{nullptr}; ///< 数据管理器。
    std::array<IdFilter, kFilterCount> filters_; ///< 各筛选维度的最小状态。
    QString file_name_filter_text_; ///< 文件名筛选文本。
    std::unordered_set<int64_t> image_search_result_ids_; ///< 图像搜索命中 ID。
    std::unordered_set<int64_t> label_search_result_ids_; ///< 标注搜索命中 ID。
    bool custom_empty_selection_enabled_{false}; ///< 自定义条件的“全不选且启用”状态。

    mutable bool duplicate_indexes_valid_{false}; ///< 重复文件条件索引是否有效。
    mutable std::unordered_set<int64_t> duplicate_file_name_image_ids_; ///< 重名图像 ID。
    mutable std::unordered_set<int64_t> duplicate_path_image_ids_; ///< 重路径图像 ID。
    mutable std::unordered_set<int64_t> unique_file_name_image_ids_; ///< 唯一文件名图像 ID。
};

} // namespace dltool::data
