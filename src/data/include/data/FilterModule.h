#pragma once

#include <QObject>
#include <set>
#include <vector>

namespace dltool::data {

class FilterModule : public QObject
{
    Q_OBJECT

public:
    explicit FilterModule(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    virtual ~FilterModule() = default;

    // Set filter criteria
    virtual void setCriteria(const std::vector<int64_t> &ids) = 0;

    // Clear filter
    virtual void clear() = 0;

    // Enable/disable this filter module
    virtual void setEnabled(bool enabled) = 0;

    // Check if filter module is enabled
    virtual bool isEnabled() const = 0;

    // Check if filter is active (enabled AND has criteria)
    virtual bool isActive() const = 0;

    // Get active criteria
    virtual std::set<int64_t> getActiveCriteria() const = 0;

    // Check if an item passes this filter
    // Returns true if filter is disabled OR item matches criteria
    virtual bool passes(int64_t item_id) const = 0;

    // Select all items
    virtual void selectAll() = 0;

    // Deselect all items
    virtual void deselectAll() = 0;

signals:
    void criteriaChanged();
    void enabledChanged(bool enabled);
};

} // namespace dltool::data
