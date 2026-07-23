#include "data/CustomFilterModule.h"

#include "data/DataManager.h"
#include "data/Images.h"
#include "data/Labels.h"

#include <QAbstractItemModel>
#include <QDir>
#include <QFileInfo>
#include <QHash>

namespace dltool::data {

namespace {

QString normalizedPath(const QString &path)
{
    if (path.isEmpty())
    {
        return {};
    }
    return QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo(path).absoluteFilePath())).toCaseFolded();
}

} // namespace

CustomFilterModule::CustomFilterModule(DataManager *data_manager, QObject *parent)
    : FilterModule(data_manager, parent)
{
    ImageInstancesListModel *image_model = data_manager ? data_manager->imageInstances() : nullptr;
    if (!image_model)
    {
        return;
    }

    connect(image_model, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex &, int, int) { invalidateCaches(); });
    connect(image_model, &QAbstractItemModel::rowsRemoved, this,
            [this](const QModelIndex &, int, int) { invalidateCaches(); });
    connect(image_model, &QAbstractItemModel::modelReset, this, [this]() { invalidateCaches(); });
    connect(image_model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles)
            {
                if (roles.isEmpty() || roles.contains(ImageInstancesListModel::NameRole)
                    || roles.contains(ImageInstancesListModel::PathRole))
                {
                    invalidateCaches();
                }
            });
}

std::vector<CustomFilterModule::ConditionSpec> CustomFilterModule::availableConditions()
{
    return {
        {static_cast<int64_t>(Condition::DuplicateFileName), QStringLiteral("重复文件名")},
        {static_cast<int64_t>(Condition::DuplicatePath), QStringLiteral("重复路径")},
        {static_cast<int64_t>(Condition::UniqueFileName), QStringLiteral("不重复文件名")},
        {static_cast<int64_t>(Condition::ImageSearchResult), QStringLiteral("图像搜索结果")},
        {static_cast<int64_t>(Condition::LabelSearchResult), QStringLiteral("标注搜索结果")},
    };
}

void CustomFilterModule::prepare(const std::vector<int64_t> &image_ids)
{
    rebuildDuplicateCaches(image_ids);
}

void CustomFilterModule::setCriteria(const std::vector<int64_t> &condition_ids)
{
    std::unordered_set<int64_t> normalized_ids;
    for (const int64_t condition_id : condition_ids)
    {
        if (isRegularCondition(condition_id)
            || (isImageSearchCondition(condition_id) && hasImageSearchResults())
            || (isLabelSearchCondition(condition_id) && hasLabelSearchResults()))
        {
            normalized_ids.insert(condition_id);
        }
    }

    const bool was_enabled = enabled_;
    const bool changed     = selected_condition_ids_ != normalized_ids || empty_selection_enabled_;
    if (!changed)
    {
        return;
    }

    selected_condition_ids_    = std::move(normalized_ids);
    empty_selection_enabled_   = false;
    if (selected_condition_ids_.empty())
    {
        enabled_ = false;
    }

    emit criteriaChanged();
    if (was_enabled != enabled_)
    {
        emit enabledChanged(enabled_);
    }
}

void CustomFilterModule::clear()
{
    const bool was_enabled = enabled_;
    const bool changed     = !selected_condition_ids_.empty() || !image_search_result_image_ids_.empty()
        || !label_search_result_label_ids_.empty() || !label_search_result_image_ids_.empty() || enabled_
        || empty_selection_enabled_;
    if (!changed)
    {
        return;
    }

    selected_condition_ids_.clear();
    image_search_result_image_ids_.clear();
    label_search_result_label_ids_.clear();
    label_search_result_image_ids_.clear();
    enabled_                 = false;
    empty_selection_enabled_ = false;

    emit criteriaChanged();
    if (was_enabled != enabled_)
    {
        emit enabledChanged(enabled_);
    }
}

void CustomFilterModule::setEnabled(bool enabled)
{
    if (enabled_ == enabled)
    {
        return;
    }

    enabled_ = enabled;
    if (enabled_)
    {
        empty_selection_enabled_ = selected_condition_ids_.empty();
    }
    else
    {
        empty_selection_enabled_ = false;
    }
    emit enabledChanged(enabled_);
}

bool CustomFilterModule::isEnabled() const
{
    return enabled_;
}

bool CustomFilterModule::isActive() const
{
    return enabled_;
}

std::unordered_set<int64_t> CustomFilterModule::getActiveCriteria() const
{
    if (enabled_)
    {
        return selected_condition_ids_;
    }
    return {};
}

bool CustomFilterModule::isInverted() const
{
    return false;
}

bool CustomFilterModule::passes(int64_t image_id) const
{
    if (!enabled_)
    {
        return true;
    }

    if (selected_condition_ids_.empty())
    {
        return false;
    }

    for (const int64_t condition_id : selected_condition_ids_)
    {
        if (passesCondition(condition_id, image_id))
        {
            return true;
        }
    }
    return false;
}

void CustomFilterModule::selectAll()
{
    std::unordered_set<int64_t> all_condition_ids;
    for (const ConditionSpec &condition : availableConditions())
    {
        if (isRegularCondition(condition.id)
            || (isImageSearchCondition(condition.id) && hasImageSearchResults())
            || (isLabelSearchCondition(condition.id) && hasLabelSearchResults()))
        {
            all_condition_ids.insert(condition.id);
        }
    }

    const bool was_enabled = enabled_;
    const bool changed = selected_condition_ids_ != all_condition_ids || !enabled_ || empty_selection_enabled_;
    if (!changed)
    {
        return;
    }

    selected_condition_ids_  = std::move(all_condition_ids);
    enabled_                 = true;
    empty_selection_enabled_ = false;
    emit criteriaChanged();
    if (was_enabled != enabled_)
    {
        emit enabledChanged(enabled_);
    }
}

void CustomFilterModule::deselectAll()
{
    const bool was_enabled = enabled_;
    const bool changed     = !selected_condition_ids_.empty() || !enabled_ || !empty_selection_enabled_;
    if (!changed)
    {
        return;
    }

    selected_condition_ids_.clear();
    enabled_                 = true;
    empty_selection_enabled_ = true;
    emit criteriaChanged();
    if (was_enabled != enabled_)
    {
        emit enabledChanged(enabled_);
    }
}

void CustomFilterModule::setImageSearchResults(const std::vector<int64_t> &image_ids, bool enable_filter)
{
    const bool was_enabled = enabled_;
    const std::unordered_set<int64_t> result_ids(image_ids.begin(), image_ids.end());
    bool changed = image_search_result_image_ids_ != result_ids;
    image_search_result_image_ids_ = result_ids;

    const int64_t condition_id = static_cast<int64_t>(Condition::ImageSearchResult);
    if (enable_filter && !image_search_result_image_ids_.empty())
    {
        changed |= selected_condition_ids_.insert(condition_id).second;
        if (!enabled_)
        {
            enabled_ = true;
            changed  = true;
        }
        empty_selection_enabled_ = false;
    }
    else
    {
        changed |= selected_condition_ids_.erase(condition_id) > 0;
        updateEnabledAfterRemovingSearchCondition();
    }

    if (!changed)
    {
        return;
    }

    emit criteriaChanged();
    if (was_enabled != enabled_)
    {
        emit enabledChanged(enabled_);
    }
}

void CustomFilterModule::clearImageSearchResults()
{
    const bool was_enabled = enabled_;
    const bool removed_condition
        = selected_condition_ids_.erase(static_cast<int64_t>(Condition::ImageSearchResult)) > 0;
    const bool changed = !image_search_result_image_ids_.empty() || removed_condition;
    image_search_result_image_ids_.clear();
    updateEnabledAfterRemovingSearchCondition();

    if (!changed && was_enabled == enabled_)
    {
        return;
    }

    emit criteriaChanged();
    if (was_enabled != enabled_)
    {
        emit enabledChanged(enabled_);
    }
}

bool CustomFilterModule::hasImageSearchResults() const
{
    return !image_search_result_image_ids_.empty();
}

int CustomFilterModule::imageSearchResultCount() const
{
    return static_cast<int>(image_search_result_image_ids_.size());
}

void CustomFilterModule::setLabelSearchResults(const std::vector<int64_t> &label_ids, bool enable_filter)
{
    const bool was_enabled = enabled_;
    const std::unordered_set<int64_t> result_ids(label_ids.begin(), label_ids.end());
    bool changed = label_search_result_label_ids_ != result_ids;
    label_search_result_label_ids_ = result_ids;
    rebuildLabelSearchImageIds();

    const int64_t condition_id = static_cast<int64_t>(Condition::LabelSearchResult);
    if (enable_filter && !label_search_result_label_ids_.empty())
    {
        changed |= selected_condition_ids_.insert(condition_id).second;
        if (!enabled_)
        {
            enabled_ = true;
            changed  = true;
        }
        empty_selection_enabled_ = false;
    }
    else
    {
        changed |= selected_condition_ids_.erase(condition_id) > 0;
        updateEnabledAfterRemovingSearchCondition();
    }

    if (!changed)
    {
        return;
    }

    emit criteriaChanged();
    if (was_enabled != enabled_)
    {
        emit enabledChanged(enabled_);
    }
}

void CustomFilterModule::clearLabelSearchResults()
{
    const bool was_enabled = enabled_;
    const bool removed_condition
        = selected_condition_ids_.erase(static_cast<int64_t>(Condition::LabelSearchResult)) > 0;
    const bool changed
        = !label_search_result_label_ids_.empty() || !label_search_result_image_ids_.empty() || removed_condition;
    label_search_result_label_ids_.clear();
    label_search_result_image_ids_.clear();
    updateEnabledAfterRemovingSearchCondition();

    if (!changed && was_enabled == enabled_)
    {
        return;
    }

    emit criteriaChanged();
    if (was_enabled != enabled_)
    {
        emit enabledChanged(enabled_);
    }
}

bool CustomFilterModule::hasLabelSearchResults() const
{
    return !label_search_result_label_ids_.empty();
}

int CustomFilterModule::labelSearchResultCount() const
{
    return static_cast<int>(label_search_result_label_ids_.size());
}

bool CustomFilterModule::passesLabel(int64_t label_id) const
{
    if (!enabled_ || !selected_condition_ids_.contains(static_cast<int64_t>(Condition::LabelSearchResult)))
    {
        return true;
    }

    if (label_search_result_label_ids_.contains(label_id))
    {
        return true;
    }

    DataManager *manager = dataManager();
    if (manager == nullptr || manager->labelInstances() == nullptr)
    {
        return false;
    }
    return passesImageLevelCondition(manager->labelInstances()->getImageId(label_id));
}

bool CustomFilterModule::usesRegularConditions() const
{
    if (!enabled_)
    {
        return false;
    }

    for (const int64_t condition_id : selected_condition_ids_)
    {
        if (isRegularCondition(condition_id))
        {
            return true;
        }
    }
    return false;
}

bool CustomFilterModule::isRegularCondition(int64_t condition_id)
{
    switch (static_cast<Condition>(condition_id))
    {
    case Condition::DuplicateFileName:
    case Condition::DuplicatePath:
    case Condition::UniqueFileName:
        return true;
    case Condition::ImageSearchResult:
    case Condition::LabelSearchResult:
        return false;
    }
    return false;
}

bool CustomFilterModule::isImageSearchCondition(int64_t condition_id)
{
    return condition_id == static_cast<int64_t>(Condition::ImageSearchResult);
}

bool CustomFilterModule::isLabelSearchCondition(int64_t condition_id)
{
    return condition_id == static_cast<int64_t>(Condition::LabelSearchResult);
}

bool CustomFilterModule::passesCondition(int64_t condition_id, int64_t image_id) const
{
    switch (static_cast<Condition>(condition_id))
    {
    case Condition::DuplicateFileName:
        return hasDuplicateFileName(image_id);
    case Condition::DuplicatePath:
        return hasDuplicatePath(image_id);
    case Condition::UniqueFileName:
        return hasUniqueFileName(image_id);
    case Condition::ImageSearchResult:
        return image_search_result_image_ids_.contains(image_id);
    case Condition::LabelSearchResult:
        return label_search_result_image_ids_.contains(image_id);
    }
    return false;
}

bool CustomFilterModule::passesImageLevelCondition(int64_t image_id) const
{
    for (const int64_t condition_id : selected_condition_ids_)
    {
        if ((isRegularCondition(condition_id) || isImageSearchCondition(condition_id))
            && passesCondition(condition_id, image_id))
        {
            return true;
        }
    }
    return false;
}

bool CustomFilterModule::hasDuplicateFileName(int64_t image_id) const
{
    if (!duplicate_cache_valid_)
    {
        DataManager *dm = dataManager();
        rebuildDuplicateCaches(dm && dm->imageInstances() ? dm->imageInstances()->getAllImageIds()
                                                          : std::vector<int64_t>{});
    }
    return duplicate_file_name_image_ids_.contains(image_id);
}

bool CustomFilterModule::hasDuplicatePath(int64_t image_id) const
{
    if (!duplicate_cache_valid_)
    {
        DataManager *dm = dataManager();
        rebuildDuplicateCaches(dm && dm->imageInstances() ? dm->imageInstances()->getAllImageIds()
                                                          : std::vector<int64_t>{});
    }
    return duplicate_path_image_ids_.contains(image_id);
}

bool CustomFilterModule::hasUniqueFileName(int64_t image_id) const
{
    if (!duplicate_cache_valid_)
    {
        DataManager *dm = dataManager();
        rebuildDuplicateCaches(dm && dm->imageInstances() ? dm->imageInstances()->getAllImageIds()
                                                          : std::vector<int64_t>{});
    }
    return unique_file_name_image_ids_.contains(image_id);
}

void CustomFilterModule::rebuildDuplicateCaches(const std::vector<int64_t> &image_ids) const
{
    duplicate_file_name_image_ids_.clear();
    duplicate_path_image_ids_.clear();
    unique_file_name_image_ids_.clear();

    DataManager *dm = dataManager();
    ImageInstancesListModel *image_model = dm ? dm->imageInstances() : nullptr;
    if (!image_model)
    {
        duplicate_cache_valid_ = true;
        return;
    }

    QHash<QString, int> file_name_counts;
    QHash<QString, int> path_counts;
    for (const int64_t image_id : image_ids)
    {
        const QString file_name = image_model->getImageName(image_id).toCaseFolded();
        const QString path      = normalizedPath(image_model->getImagePath(image_id));
        if (!file_name.isEmpty())
        {
            file_name_counts[file_name]++;
        }
        if (!path.isEmpty())
        {
            path_counts[path]++;
        }
    }

    for (const int64_t image_id : image_ids)
    {
        const QString file_name = image_model->getImageName(image_id).toCaseFolded();
        const QString path      = normalizedPath(image_model->getImagePath(image_id));
        if (!file_name.isEmpty() && file_name_counts.value(file_name) > 1)
        {
            duplicate_file_name_image_ids_.insert(image_id);
        }
        if (!file_name.isEmpty() && file_name_counts.value(file_name) == 1)
        {
            unique_file_name_image_ids_.insert(image_id);
        }
        if (!path.isEmpty() && path_counts.value(path) > 1)
        {
            duplicate_path_image_ids_.insert(image_id);
        }
    }

    duplicate_cache_valid_ = true;
}

void CustomFilterModule::rebuildLabelSearchImageIds()
{
    label_search_result_image_ids_.clear();

    DataManager *manager = dataManager();
    if (manager == nullptr || manager->labelInstances() == nullptr)
    {
        return;
    }

    const std::vector<int64_t> label_ids(label_search_result_label_ids_.begin(), label_search_result_label_ids_.end());
    const auto image_ids = manager->labelInstances()->getImageIds(label_ids);
    label_search_result_image_ids_.insert(image_ids.begin(), image_ids.end());
}

void CustomFilterModule::invalidateCaches()
{
    duplicate_cache_valid_ = false;
}

void CustomFilterModule::updateEnabledAfterRemovingSearchCondition()
{
    if (selected_condition_ids_.empty() && !empty_selection_enabled_)
    {
        enabled_ = false;
    }
}

} // namespace dltool::data
