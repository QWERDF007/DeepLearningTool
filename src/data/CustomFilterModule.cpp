#include "data/CustomFilterModule.h"

#include "data/DataManager.h"
#include "data/Images.h"

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
    };
}

void CustomFilterModule::prepare(const std::vector<int64_t> &image_ids)
{
    rebuildDuplicateCaches(image_ids);
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
    case Condition::DuplicatePath:
        return hasDuplicatePath(image_id);
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
    return duplicate_file_name_image_ids_.find(image_id) != duplicate_file_name_image_ids_.end();
}

bool CustomFilterModule::hasDuplicatePath(int64_t image_id) const
{
    if (!duplicate_cache_valid_)
    {
        DataManager *dm = dataManager();
        rebuildDuplicateCaches(dm && dm->imageInstances() ? dm->imageInstances()->getAllImageIds()
                                                          : std::vector<int64_t>{});
    }
    return duplicate_path_image_ids_.find(image_id) != duplicate_path_image_ids_.end();
}

void CustomFilterModule::rebuildDuplicateCaches(const std::vector<int64_t> &image_ids) const
{
    duplicate_file_name_image_ids_.clear();
    duplicate_path_image_ids_.clear();

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
        if (!path.isEmpty() && path_counts.value(path) > 1)
        {
            duplicate_path_image_ids_.insert(image_id);
        }
    }

    duplicate_cache_valid_ = true;
}

void CustomFilterModule::invalidateCaches()
{
    duplicate_cache_valid_ = false;
}

} // namespace dltool::data
