#pragma once

#include "FilterModule.h"

#include <set>

namespace dltool::data {

class ImageInstancesListModel;
class ImageTagsListModel;

class TagFilterModule : public FilterModule
{
    Q_OBJECT

public:
    explicit TagFilterModule(ImageInstancesListModel *image_model, ImageTagsListModel *tags_model,
                             QObject *parent = nullptr);
    ~TagFilterModule() override = default;

    void              setCriteria(const std::vector<int64_t> &tag_ids) override;
    void              clear() override;
    void              setEnabled(bool enabled) override;
    bool              isEnabled() const override;
    bool              isActive() const override;
    std::set<int64_t> getActiveCriteria() const override;

    // Check if image has any of the selected tags
    // Returns true if filter is disabled OR image matches criteria
    bool passes(int64_t image_id) const override;

    // Select all tags
    void selectAll() override;

    // Deselect all tags
    void deselectAll() override;

private:
    ImageInstancesListModel *image_model_{nullptr};
    ImageTagsListModel      *tags_model_{nullptr};
    std::set<int64_t>        selected_tag_ids_;
    bool                     enabled_{false}; // Filter module enabled state
};

} // namespace dltool::data
