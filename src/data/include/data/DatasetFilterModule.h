#pragma once

#include "FilterModule.h"

#include <set>


namespace dltool::data {

class ImageInstancesListModel;
class DatasetsListModel;

class DatasetFilterModule : public FilterModule
{
    Q_OBJECT

public:
    explicit DatasetFilterModule(ImageInstancesListModel *image_model, DatasetsListModel *datasets_model,
                                 QObject *parent = nullptr);
    ~DatasetFilterModule() override = default;

    void              setCriteria(const std::vector<int64_t> &dataset_ids) override;
    void              clear() override;
    void              setEnabled(bool enabled) override;
    bool              isEnabled() const override;
    bool              isActive() const override;
    std::set<int64_t> getActiveCriteria() const override;

    // Check if image belongs to selected datasets
    // Returns true if filter is disabled OR image matches criteria
    bool passes(int64_t image_id) const override;

    // Select all datasets
    void selectAll() override;

    // Deselect all datasets
    void deselectAll() override;

private:
    ImageInstancesListModel *image_model_{nullptr};
    DatasetsListModel       *datasets_model_{nullptr};
    std::set<int64_t>        selected_dataset_ids_;
    bool                     enabled_{false}; // Filter module enabled state
};

} // namespace dltool::data
