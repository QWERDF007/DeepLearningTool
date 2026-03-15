#pragma once

#include <QObject>
#include <QtQml>
#include <memory>
#include <set>
#include <vector>

namespace dltool::data {

class ImageInstancesListModel;
class LabelInstancesListModel;
class DatasetsListModel;
class ImageTagsListModel;
class DatasetFilterModule;
class TagFilterModule;

struct FilterCriteria
{
    std::set<int64_t> dataset_ids; // Selected dataset IDs (empty = all)
    std::set<int64_t> tag_ids;     // Selected tag IDs (empty = all)

    bool isEmpty() const
    {
        return dataset_ids.empty() && tag_ids.empty();
    }
};

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

    // Initialize filter modules (must be called after DataManager is fully constructed)
    void initializeFilterModules(DatasetsListModel *datasets_model, ImageTagsListModel *tags_model);

    // Filter state queries
    bool    isActive() const;
    int     activeFilterCount() const;
    QString filterSummary() const;

    // Filter criteria management
    Q_INVOKABLE void setDatasetFilter(const std::vector<int64_t> &dataset_ids);
    Q_INVOKABLE void setTagFilter(const std::vector<int64_t> &tag_ids);
    Q_INVOKABLE void setDatasetFilterEnabled(bool enabled);
    Q_INVOKABLE void setTagFilterEnabled(bool enabled);
    Q_INVOKABLE void clearAllFilters();
    Q_INVOKABLE void clearDatasetFilter();
    Q_INVOKABLE void clearTagFilter();

    // Get current filter state
    Q_INVOKABLE std::vector<int64_t> getActiveDatasetIds() const;
    Q_INVOKABLE std::vector<int64_t> getActiveTagIds() const;

    // Apply filters to models
    void applyFilters();

signals:
    void filterStateChanged();
    void filterApplied();

private:
    void updateFilterCriteria();
    bool shouldIncludeImage(int64_t image_id) const;
    bool shouldIncludeLabel(int64_t label_id) const;

    ImageInstancesListModel *image_model_{nullptr};
    LabelInstancesListModel *label_model_{nullptr};

    std::unique_ptr<DatasetFilterModule> dataset_filter_;
    std::unique_ptr<TagFilterModule>     tag_filter_;

    FilterCriteria current_criteria_;
};

} // namespace dltool::data
