#pragma once

#include "FilterModule.h"

#include <unordered_set>

namespace dltool::data {

class DataManager;

class ImageLabelClassFilterModule : public FilterModule
{
    Q_OBJECT

public:
    explicit ImageLabelClassFilterModule(DataManager *data_manager, QObject *parent = nullptr);
    ~ImageLabelClassFilterModule() override = default;

    void                        setCriteria(const std::vector<int64_t> &label_class_ids) override;
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
    std::unordered_set<int64_t> selected_label_class_ids_;
    bool                        enabled_{false};
    bool                        inverted_{false};
};

} // namespace dltool::data
