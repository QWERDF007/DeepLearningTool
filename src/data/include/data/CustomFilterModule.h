#pragma once

#include "FilterModule.h"

#include <QString>
#include <unordered_set>
#include <vector>

namespace dltool::data {

class DataManager;

class CustomFilterModule : public FilterModule
{
    Q_OBJECT

public:
    enum class Condition : int64_t
    {
        DuplicateFileName = 1,
        DuplicatePath     = 2,
        UniqueFileName    = 3,
        ImageSearchResult = 4,
        LabelSearchResult = 5,
    };

    struct ConditionSpec
    {
        int64_t id;
        QString text;
    };

    explicit CustomFilterModule(DataManager *data_manager, QObject *parent = nullptr);
    ~CustomFilterModule() override = default;

    static std::vector<ConditionSpec> availableConditions();

    /// 按已通过其他过滤条件的图像集合准备重复项缓存。
    void prepare(const std::vector<int64_t> &image_ids);

    void                        setCriteria(const std::vector<int64_t> &condition_ids) override;
    void                        clear() override;
    void                        setEnabled(bool enabled) override;
    bool                        isEnabled() const override;
    bool                        isActive() const override;
    std::unordered_set<int64_t> getActiveCriteria() const override;
    bool                        isInverted() const override;
    bool                        passes(int64_t image_id) const override;
    void                        selectAll() override;
    void                        deselectAll() override;

    /// 设置图像搜索命中的图像。图像搜索结果作为自定义过滤条件参与 OR 匹配。
    void setImageSearchResults(const std::vector<int64_t> &image_ids, bool enable_filter);
    void clearImageSearchResults();
    bool hasImageSearchResults() const;
    int  imageSearchResultCount() const;

    /// 设置标注搜索命中的标注。其所属图像及命中的标注均作为自定义过滤条件参与 OR 匹配。
    void setLabelSearchResults(const std::vector<int64_t> &label_ids, bool enable_filter);
    void clearLabelSearchResults();
    bool hasLabelSearchResults() const;
    int  labelSearchResultCount() const;

    /// 判断标注是否通过标注搜索条件。
    /// 图像级自定义条件由 GlobalFilter 的图像过滤函数统一处理。
    bool passesLabel(int64_t label_id) const;

    /// 是否需要准备重复文件名、重复路径等常规条件的缓存。
    bool usesRegularConditions() const;

private:
    static bool isRegularCondition(int64_t condition_id);
    static bool isImageSearchCondition(int64_t condition_id);
    static bool isLabelSearchCondition(int64_t condition_id);

    bool passesCondition(int64_t condition_id, int64_t image_id) const;
    bool passesImageLevelCondition(int64_t image_id) const;
    bool hasDuplicateFileName(int64_t image_id) const;
    bool hasDuplicatePath(int64_t image_id) const;
    bool hasUniqueFileName(int64_t image_id) const;
    void rebuildDuplicateCaches(const std::vector<int64_t> &image_ids) const;
    void rebuildLabelSearchImageIds();
    /// 仅使派生缓存失效；数据变更后的过滤刷新由 DataManager/GlobalFilter 统一调度。
    void invalidateCaches();
    void updateEnabledAfterRemovingSearchCondition();

    std::unordered_set<int64_t> selected_condition_ids_;
    std::unordered_set<int64_t> image_search_result_image_ids_;
    std::unordered_set<int64_t> label_search_result_label_ids_;
    std::unordered_set<int64_t> label_search_result_image_ids_;
    bool                        enabled_{false};
    bool                        empty_selection_enabled_{false};

    mutable bool                        duplicate_cache_valid_{false};
    mutable std::unordered_set<int64_t> duplicate_file_name_image_ids_;
    mutable std::unordered_set<int64_t> duplicate_path_image_ids_;
    mutable std::unordered_set<int64_t> unique_file_name_image_ids_;
};

} // namespace dltool::data
