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
    };

    struct ConditionSpec
    {
        int64_t id;
        QString text;
    };

    explicit CustomFilterModule(DataManager *data_manager, QObject *parent = nullptr);
    ~CustomFilterModule() override = default;

    static std::vector<ConditionSpec> availableConditions();

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

private:
    bool passesCondition(int64_t condition_id, int64_t image_id) const;
    bool hasDuplicateFileName(int64_t image_id) const;
    void rebuildDuplicateFileNameCache() const;
    void invalidateCaches();

    std::unordered_set<int64_t> selected_condition_ids_;
    bool                        enabled_{false};

    mutable bool                        duplicate_file_name_cache_valid_{false};
    mutable std::unordered_set<int64_t> duplicate_file_name_image_ids_;
};

} // namespace dltool::data
