#include "data/CustomFilterModule.h"

#include "data/DataManager.h"
#include "data/Images.h"

#include <QAbstractItemModel>
#include <QHash>

namespace dltool::data {

CustomFilterModule::CustomFilterModule(DataManager *data_manager, QObject *parent)
    : FilterModule(data_manager, parent)
{
    ImageInstancesListModel *image_model = data_manager ? data_manager->imageInstances() : nullptr;
    if (!image_model)
    {
        return;
    }

    connect(image_model, &QAbstractItemModel::modelReset, this, [this]() { invalidateCaches(); });
    connect(image_model, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex &, int, int) { invalidateCaches(); });
    connect(image_model, &QAbstractItemModel::rowsRemoved, this,
            [this](const QModelIndex &, int, int) { invalidateCaches(); });
    connect(image_model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &, const QModelIndex &, const QList<int> &) { invalidateCaches(); });
}

std::vector<CustomFilterModule::ConditionSpec> CustomFilterModule::availableConditions()
{
    return {
        {static_cast<int64_t>(Condition::DuplicateFileName), QStringLiteral("重复文件名")},
    };
}

void CustomFilterModule::setCriteria(const std::vector<int64_t> &condition_ids)
{
    selected_condition_ids_.clear();
    selected_condition_ids_.insert(condition_ids.begin(), condition_ids.end());
    emit criteriaChanged();
}

void CustomFilterModule::clear()
{
    if (!selected_condition_ids_.empty())
    {
        selected_condition_ids_.clear();
        emit criteriaChanged();
    }
}

void CustomFilterModule::setEnabled(bool enabled)
{
    if (enabled_ != enabled)
    {
        enabled_ = enabled;
        emit enabledChanged(enabled);
    }
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
    selected_condition_ids_.clear();
    for (const ConditionSpec &condition : availableConditions())
    {
        selected_condition_ids_.insert(condition.id);
    }
    emit criteriaChanged();
}

void CustomFilterModule::deselectAll()
{
    if (!selected_condition_ids_.empty())
    {
        selected_condition_ids_.clear();
        emit criteriaChanged();
    }
}

bool CustomFilterModule::passesCondition(int64_t condition_id, int64_t image_id) const
{
    switch (static_cast<Condition>(condition_id))
    {
    case Condition::DuplicateFileName:
        return hasDuplicateFileName(image_id);
    }
    return false;
}

bool CustomFilterModule::hasDuplicateFileName(int64_t image_id) const
{
    if (!duplicate_file_name_cache_valid_)
    {
        rebuildDuplicateFileNameCache();
    }
    return duplicate_file_name_image_ids_.find(image_id) != duplicate_file_name_image_ids_.end();
}

void CustomFilterModule::rebuildDuplicateFileNameCache() const
{
    duplicate_file_name_image_ids_.clear();

    DataManager *dm = dataManager();
    if (!dm || !dm->imageInstances())
    {
        duplicate_file_name_cache_valid_ = true;
        return;
    }

    ImageInstancesListModel *image_model = dm->imageInstances();
    const std::vector<int64_t> image_ids = image_model->getAllImageIds();

    QHash<QString, int> file_name_counts;
    for (const int64_t image_id : image_ids)
    {
        const QString file_name = image_model->getImageName(image_id).toCaseFolded();
        if (!file_name.isEmpty())
        {
            file_name_counts[file_name]++;
        }
    }

    for (const int64_t image_id : image_ids)
    {
        const QString file_name = image_model->getImageName(image_id).toCaseFolded();
        if (!file_name.isEmpty() && file_name_counts.value(file_name) > 1)
        {
            duplicate_file_name_image_ids_.insert(image_id);
        }
    }

    duplicate_file_name_cache_valid_ = true;
}

void CustomFilterModule::invalidateCaches()
{
    duplicate_file_name_cache_valid_ = false;
    if (enabled_)
    {
        emit criteriaChanged();
    }
}

} // namespace dltool::data
