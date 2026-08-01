#include "data/GlobalFilter.h"

#include "data/DataManager.h"
#include "data/Datasets.h"
#include "data/ImageTags.h"
#include "data/Images.h"
#include "data/LabelClasses.h"
#include "data/Labels.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>

#include <algorithm>
#include <set>
#include <unordered_map>

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

bool intersects(const std::set<int64_t> &values, const std::unordered_set<int64_t> &expected)
{
    for (const int64_t value : values)
    {
        if (expected.contains(value))
        {
            return true;
        }
    }
    return false;
}

} // namespace

GlobalFilter::GlobalFilter(DataManager *data_manager, QObject *parent)
    : QObject(parent)
    , data_manager_(data_manager)
{
    if (ImageInstancesListModel *images = imageSource())
    {
        connect(images, &QAbstractItemModel::rowsInserted, this, [this]() { invalidateDuplicateIndexes(); });
        connect(images, &QAbstractItemModel::rowsRemoved, this, [this]() { invalidateDuplicateIndexes(); });
        connect(images, &QAbstractItemModel::modelReset, this, [this]() { invalidateDuplicateIndexes(); });
        connect(images, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles)
                {
                    if (roles.isEmpty() || roles.contains(ImageInstancesListModel::NameRole)
                        || roles.contains(ImageInstancesListModel::PathRole))
                    {
                        invalidateDuplicateIndexes();
                    }
                });
    }
}

std::vector<GlobalFilter::CustomConditionSpec> GlobalFilter::customConditions()
{
    return {
        {static_cast<int64_t>(CustomCondition::DuplicateFileName), QString("重复文件名")},
        {static_cast<int64_t>(CustomCondition::DuplicatePath), QString("重复路径")},
        {static_cast<int64_t>(CustomCondition::UniqueFileName), QString("不重复文件名")},
        {static_cast<int64_t>(CustomCondition::ImageSearchResult), QString("图像搜索结果")},
        {static_cast<int64_t>(CustomCondition::LabelSearchResult), QString("标注搜索结果")},
    };
}

void GlobalFilter::setFilter(const FilterType type, const std::vector<int64_t> &ids)
{
    IdFilter &state = filter(type);
    const bool was_enabled = state.enabled;
    std::unordered_set<int64_t> normalized_ids;
    normalized_ids.reserve(ids.size());
    for (const int64_t id : ids)
    {
        if (type != FilterType::Custom || customConditionAvailable(id))
        {
            normalized_ids.insert(id);
        }
    }

    const bool changed = state.ids != normalized_ids || state.inverted;
    if (!changed)
    {
        return;
    }

    state.ids      = std::move(normalized_ids);
    state.inverted = false;
    if (type == FilterType::Custom)
    {
        custom_empty_selection_enabled_ = false;
        if (state.ids.empty())
        {
            state.enabled = false;
        }
    }

    if (state.enabled || was_enabled)
    {
        notifyFilterChanged();
    }
    else
    {
        notifyStateChanged(false);
    }
}

void GlobalFilter::setFilterEnabled(const FilterType type, const bool enabled)
{
    IdFilter &state = filter(type);
    if (state.enabled == enabled)
    {
        return;
    }

    state.enabled = enabled;
    if (type == FilterType::Custom && !enabled)
    {
        custom_empty_selection_enabled_ = false;
    }
    notifyFilterChanged();
}

bool GlobalFilter::isFilterEnabled(const FilterType type) const
{
    return filter(type).enabled;
}

bool GlobalFilter::isFilterInverted(const FilterType type) const
{
    return filter(type).inverted;
}

void GlobalFilter::clearFilter(const FilterType type)
{
    IdFilter &state = filter(type);
    const bool search_results_changed = type == FilterType::Custom
        && (!image_search_result_ids_.empty() || !label_search_result_ids_.empty());
    const bool changed = !state.ids.empty() || state.inverted || search_results_changed
        || (type == FilterType::Custom && custom_empty_selection_enabled_);
    if (!changed)
    {
        return;
    }

    state.ids.clear();
    state.inverted = false;
    if (type == FilterType::Custom)
    {
        image_search_result_ids_.clear();
        label_search_result_ids_.clear();
        custom_empty_selection_enabled_ = false;
        state.enabled                  = false;
    }

    if (state.enabled || type == FilterType::Custom)
    {
        notifyFilterChanged();
        if (search_results_changed)
        {
            emit customFilterSearchResultsChanged(false, false);
        }
    }
    else
    {
        notifyStateChanged(search_results_changed);
    }
}

void GlobalFilter::selectAll(const FilterType type)
{
    IdFilter &state = filter(type);
    std::unordered_set<int64_t> selected_ids;
    collectAvailableIds(type, selected_ids);
    const bool changed = state.ids != selected_ids || state.inverted
        || (type == FilterType::Custom && custom_empty_selection_enabled_);
    if (!changed)
    {
        return;
    }

    state.ids      = std::move(selected_ids);
    state.inverted = false;
    if (type == FilterType::Custom)
    {
        state.enabled                  = true;
        custom_empty_selection_enabled_ = false;
    }

    if (state.enabled)
    {
        notifyFilterChanged();
    }
    else
    {
        notifyStateChanged(false);
    }
}

void GlobalFilter::deselectAll(const FilterType type)
{
    IdFilter &state = filter(type);
    if (type == FilterType::Custom)
    {
        const bool changed = !state.ids.empty() || !state.enabled || state.inverted || !custom_empty_selection_enabled_;
        if (!changed)
        {
            return;
        }

        state.ids.clear();
        state.inverted                   = false;
        state.enabled                    = true;
        custom_empty_selection_enabled_ = true;
        notifyFilterChanged();
        return;
    }

    std::unordered_set<int64_t> selected_ids;
    collectAvailableIds(type, selected_ids);
    const bool changed = state.ids != selected_ids || !state.inverted;
    if (!changed)
    {
        return;
    }

    state.ids      = std::move(selected_ids);
    state.inverted = true;
    if (state.enabled)
    {
        notifyFilterChanged();
    }
    else
    {
        notifyStateChanged(false);
    }
}

std::vector<int64_t> GlobalFilter::getActiveIds(const FilterType type) const
{
    const IdFilter &state = filter(type);
    if (!state.enabled)
    {
        return {};
    }

    std::vector<int64_t> ids;
    ids.reserve(state.ids.size());
    for (const int64_t id : state.ids)
    {
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

void GlobalFilter::clearAllFilters()
{
    bool changed = !file_name_filter_text_.isEmpty() || !image_search_result_ids_.empty() || !label_search_result_ids_.empty()
        || custom_empty_selection_enabled_;
    for (IdFilter &state : filters_)
    {
        changed |= state.enabled || state.inverted || !state.ids.empty();
        state.ids.clear();
        state.enabled  = false;
        state.inverted = false;
    }
    if (!changed)
    {
        return;
    }

    file_name_filter_text_.clear();
    image_search_result_ids_.clear();
    label_search_result_ids_.clear();
    custom_empty_selection_enabled_ = false;
    notifyFilterChanged();
    emit customFilterSearchResultsChanged(false, false);
}

void GlobalFilter::refresh()
{
    invalidateDuplicateIndexes();
    if (isActive())
    {
        notifyFilterChanged();
    }
}

void GlobalFilter::clearImageSearchResults()
{
    const bool was_available = !image_search_result_ids_.empty();
    const int64_t condition_id = static_cast<int64_t>(CustomCondition::ImageSearchResult);
    IdFilter &custom = filter(FilterType::Custom);
    const bool was_enabled = custom.enabled;
    const bool removed = custom.ids.erase(condition_id) > 0;
    if (!was_available && !removed)
    {
        return;
    }

    image_search_result_ids_.clear();
    updateCustomEnabledAfterSearchRemoval();
    if (was_enabled && removed)
    {
        notifyFilterChanged();
    }
    else
    {
        notifyStateChanged(false);
    }
    emit customFilterSearchResultsChanged(false, !label_search_result_ids_.empty());
}

void GlobalFilter::setImageSearchResults(const std::vector<int64_t> &image_ids, const bool enable_filter)
{
    std::unordered_set<int64_t> result_ids(image_ids.begin(), image_ids.end());
    const bool availability_changed = image_search_result_ids_.empty() != result_ids.empty();
    const bool ids_changed = image_search_result_ids_ != result_ids;
    image_search_result_ids_ = std::move(result_ids);

    IdFilter &custom = filter(FilterType::Custom);
    const int64_t condition_id = static_cast<int64_t>(CustomCondition::ImageSearchResult);
    const bool was_enabled = custom.enabled;
    const bool had_condition = custom.ids.contains(condition_id);
    bool conditions_changed = false;
    if (enable_filter && !image_search_result_ids_.empty())
    {
        conditions_changed = custom.ids.insert(condition_id).second;
        custom.enabled = true;
        custom_empty_selection_enabled_ = false;
    }
    else
    {
        conditions_changed = custom.ids.erase(condition_id) > 0;
        updateCustomEnabledAfterSearchRemoval();
    }

    if (!ids_changed && !conditions_changed)
    {
        return;
    }

    if ((was_enabled && had_condition) || (custom.enabled && custom.ids.contains(condition_id)))
    {
        notifyFilterChanged();
    }
    else
    {
        notifyStateChanged(false);
    }
    if (availability_changed)
    {
        emit customFilterSearchResultsChanged(!image_search_result_ids_.empty(), !label_search_result_ids_.empty());
    }
}

void GlobalFilter::clearLabelSearchResults()
{
    const bool was_available = !label_search_result_ids_.empty();
    const int64_t condition_id = static_cast<int64_t>(CustomCondition::LabelSearchResult);
    IdFilter &custom = filter(FilterType::Custom);
    const bool was_enabled = custom.enabled;
    const bool removed = custom.ids.erase(condition_id) > 0;
    if (!was_available && !removed)
    {
        return;
    }

    label_search_result_ids_.clear();
    updateCustomEnabledAfterSearchRemoval();
    if (was_enabled && removed)
    {
        notifyFilterChanged();
    }
    else
    {
        notifyStateChanged(false);
    }
    emit customFilterSearchResultsChanged(!image_search_result_ids_.empty(), false);
}

void GlobalFilter::setLabelSearchResults(const std::vector<int64_t> &label_ids, const bool enable_filter)
{
    std::unordered_set<int64_t> result_ids(label_ids.begin(), label_ids.end());
    const bool availability_changed = label_search_result_ids_.empty() != result_ids.empty();
    const bool ids_changed = label_search_result_ids_ != result_ids;
    label_search_result_ids_ = std::move(result_ids);

    IdFilter &custom = filter(FilterType::Custom);
    const int64_t condition_id = static_cast<int64_t>(CustomCondition::LabelSearchResult);
    const bool was_enabled = custom.enabled;
    const bool had_condition = custom.ids.contains(condition_id);
    bool conditions_changed = false;
    if (enable_filter && !label_search_result_ids_.empty())
    {
        conditions_changed = custom.ids.insert(condition_id).second;
        custom.enabled = true;
        custom_empty_selection_enabled_ = false;
    }
    else
    {
        conditions_changed = custom.ids.erase(condition_id) > 0;
        updateCustomEnabledAfterSearchRemoval();
    }

    if (!ids_changed && !conditions_changed)
    {
        return;
    }

    if ((was_enabled && had_condition) || (custom.enabled && custom.ids.contains(condition_id)))
    {
        notifyFilterChanged();
    }
    else
    {
        notifyStateChanged(false);
    }
    if (availability_changed)
    {
        emit customFilterSearchResultsChanged(!image_search_result_ids_.empty(), !label_search_result_ids_.empty());
    }
}

QString GlobalFilter::fileNameFilterText() const
{
    return file_name_filter_text_;
}

void GlobalFilter::setFileNameFilterText(const QString &text)
{
    const QString normalized_text = text.trimmed();
    if (file_name_filter_text_ == normalized_text)
    {
        return;
    }

    file_name_filter_text_ = normalized_text;
    notifyFilterChanged();
}

bool GlobalFilter::acceptsImage(const int64_t image_id) const
{
    return acceptsImageWithoutCustom(image_id) && acceptsCustomImage(image_id);
}

bool GlobalFilter::acceptsLabel(const int64_t label_id) const
{
    LabelInstancesListModel *labels = labelSource();
    if (labels == nullptr)
    {
        return false;
    }

    const int64_t image_id = labels->getImageId(label_id);
    if (image_id < 0 || !acceptsImageWithoutCustom(image_id))
    {
        return false;
    }

    const IdFilter &classes = filter(FilterType::LabelClass);
    if (!passesIdFilter(classes, labels->getLabelClassId(label_id)))
    {
        return false;
    }
    return acceptsCustomLabel(label_id, image_id);
}

bool GlobalFilter::acceptsLabelClassId(const int64_t label_class_id) const
{
    return passesIdFilter(filter(FilterType::LabelClass), label_class_id);
}

bool GlobalFilter::isLabelClassFilterEnabled() const
{
    return filter(FilterType::LabelClass).enabled;
}

bool GlobalFilter::isLabelClassFilterInverted() const
{
    return filter(FilterType::LabelClass).inverted;
}

bool GlobalFilter::isActive() const
{
    return std::any_of(filters_.cbegin(), filters_.cend(), [](const IdFilter &state) { return state.enabled; })
        || !file_name_filter_text_.isEmpty();
}

int GlobalFilter::activeFilterCount() const
{
    const int filter_count = static_cast<int>(std::count_if(
        filters_.cbegin(), filters_.cend(), [](const IdFilter &state) { return state.enabled; }));
    return filter_count + (file_name_filter_text_.isEmpty() ? 0 : 1);
}

QString GlobalFilter::description() const
{
    if (!isActive())
        return QString("全部测试样本");

    const auto namesFor = [](const std::vector<int64_t> &ids, const auto &nameFor)
    {
        QStringList names;
        for (const int64_t id : ids)
        {
            const QString name = nameFor(id);
            names.push_back(name.isEmpty() ? QString("#%1").arg(id) : name);
        }
        return names.join(QString("/"));
    };
    const auto idsFor = [this](const FilterType type) { return getActiveIds(type); };
    const auto prefix = [this](const FilterType type, const QString &label)
    {
        const IdFilter &state = filter(type);
        return state.inverted ? QString("排除%1").arg(label) : label;
    };

    QStringList parts;
    const IdFilter &datasets = filter(FilterType::Dataset);
    if (datasets.enabled)
    {
        const QString values = namesFor(idsFor(FilterType::Dataset), [this](const int64_t id)
        {
            return data_manager_ != nullptr && data_manager_->datasets() != nullptr
                ? data_manager_->datasets()->getDatasetName(id) : QString();
        });
        parts.push_back(QString("%1=%2").arg(prefix(FilterType::Dataset, QString("数据集")),
                                              values.isEmpty() ? QString("无") : values));
    }
    const IdFilter &label_classes = filter(FilterType::LabelClass);
    if (label_classes.enabled)
    {
        const QString values = namesFor(idsFor(FilterType::LabelClass), [this](const int64_t id)
        {
            return data_manager_ != nullptr && data_manager_->labelClasses() != nullptr
                ? data_manager_->labelClasses()->getLabelClassName(id) : QString();
        });
        parts.push_back(QString("%1=%2").arg(prefix(FilterType::LabelClass, QString("标签")),
                                              values.isEmpty() ? QString("无") : values));
    }
    const IdFilter &image_classes = filter(FilterType::ImageLabelClass);
    if (image_classes.enabled)
    {
        const QString values = namesFor(idsFor(FilterType::ImageLabelClass), [this](const int64_t id)
        {
            return data_manager_ != nullptr && data_manager_->labelClasses() != nullptr
                ? data_manager_->labelClasses()->getLabelClassName(id) : QString();
        });
        parts.push_back(QString("%1=%2").arg(prefix(FilterType::ImageLabelClass, QString("图像标签")),
                                              values.isEmpty() ? QString("无") : values));
    }
    const IdFilter &tags = filter(FilterType::Tag);
    if (tags.enabled)
    {
        const QString values = namesFor(idsFor(FilterType::Tag), [this](const int64_t id)
        {
            return data_manager_ != nullptr && data_manager_->imageTags() != nullptr
                ? data_manager_->imageTags()->getTagClassName(id) : QString();
        });
        parts.push_back(QString("%1=%2").arg(prefix(FilterType::Tag, QString("Tag")),
                                              values.isEmpty() ? QString("无") : values));
    }
    if (!file_name_filter_text_.isEmpty())
        parts.push_back(QString("文件名=%1").arg(file_name_filter_text_));

    const IdFilter &custom = filter(FilterType::Custom);
    if (custom.enabled)
    {
        const auto conditions = customConditions();
        QStringList values;
        for (const int64_t id : getActiveIds(FilterType::Custom))
        {
            const auto found = std::find_if(conditions.cbegin(), conditions.cend(),
                                            [id](const CustomConditionSpec &condition) { return condition.id == id; });
            values.push_back(found == conditions.cend() ? QString("#%1").arg(id) : found->text);
        }
        parts.push_back(QString("自定义=%1").arg(values.isEmpty() ? QString("无") : values.join(QString("/"))));
    }
    return parts.isEmpty() ? QString("全部测试样本") : QString("当前过滤：%1").arg(parts.join(QString("，")));
}

GlobalFilter::IdFilter &GlobalFilter::filter(const FilterType type)
{
    return filters_[static_cast<size_t>(type)];
}

const GlobalFilter::IdFilter &GlobalFilter::filter(const FilterType type) const
{
    return filters_[static_cast<size_t>(type)];
}

ImageInstancesListModel *GlobalFilter::imageSource() const
{
    return data_manager_ != nullptr ? data_manager_->imageSource() : nullptr;
}

LabelInstancesListModel *GlobalFilter::labelSource() const
{
    return data_manager_ != nullptr ? data_manager_->labelSource() : nullptr;
}

void GlobalFilter::collectAvailableIds(const FilterType type, std::unordered_set<int64_t> &ids) const
{
    ids.clear();
    QAbstractItemModel *model = nullptr;
    int id_role = -1;
    if (data_manager_ == nullptr)
    {
        return;
    }

    switch (type)
    {
    case FilterType::Dataset:
        model = data_manager_->datasets();
        id_role = DatasetsListModel::DatasetIdRole;
        break;
    case FilterType::Tag:
        model = data_manager_->imageTags();
        id_role = ImageTagsListModel::TagIdRole;
        break;
    case FilterType::LabelClass:
    case FilterType::ImageLabelClass:
        model = data_manager_->labelClasses();
        id_role = LabelClassesListModel::LabelClassIdRole;
        break;
    case FilterType::Custom:
        for (const CustomConditionSpec &condition : customConditions())
        {
            if (customConditionAvailable(condition.id))
            {
                ids.insert(condition.id);
            }
        }
        return;
    }

    if (model == nullptr)
    {
        return;
    }
    ids.reserve(static_cast<size_t>(model->rowCount()));
    for (int row = 0; row < model->rowCount(); ++row)
    {
        ids.insert(model->data(model->index(row, 0), id_role).toLongLong());
    }
}

bool GlobalFilter::passesIdFilter(const IdFilter &state, const int64_t id) const
{
    if (!state.enabled)
    {
        return true;
    }
    if (state.ids.empty())
    {
        return state.inverted;
    }
    const bool matches = state.ids.contains(id);
    return state.inverted ? !matches : matches;
}

bool GlobalFilter::acceptsImageWithoutCustom(const int64_t image_id) const
{
    ImageInstancesListModel *images = imageSource();
    if (images == nullptr)
    {
        return false;
    }
    const ImageInstance *image = images->getImageInstance(image_id);
    if (image == nullptr)
    {
        return false;
    }

    if (!passesIdFilter(filter(FilterType::Dataset), image->datasetId()))
    {
        return false;
    }
    const IdFilter &tags = filter(FilterType::Tag);
    if (tags.enabled)
    {
        const bool matches = !tags.ids.empty() && matchesTags(image_id, tags.ids);
        if (tags.inverted ? matches : !matches)
        {
            return false;
        }
    }
    const IdFilter &image_classes = filter(FilterType::ImageLabelClass);
    if (image_classes.enabled)
    {
        const bool matches = image_classes.ids.empty() ? false : matchesImageLabelClasses(image_id, image_classes.ids);
        if (image_classes.inverted ? matches : !matches)
        {
            return false;
        }
    }
    if (!file_name_filter_text_.isEmpty()
        && !image->path().contains(file_name_filter_text_, Qt::CaseInsensitive))
    {
        return false;
    }
    return true;
}

bool GlobalFilter::acceptsCustomImage(const int64_t image_id) const
{
    const IdFilter &custom = filter(FilterType::Custom);
    if (!custom.enabled)
    {
        return true;
    }
    if (custom.ids.empty())
    {
        return false;
    }
    for (const int64_t condition_id : custom.ids)
    {
        if (matchesCustomImageCondition(image_id, condition_id))
        {
            return true;
        }
    }
    return false;
}

bool GlobalFilter::acceptsCustomLabel(const int64_t label_id, const int64_t image_id) const
{
    const IdFilter &custom = filter(FilterType::Custom);
    if (!custom.enabled)
    {
        return true;
    }
    if (custom.ids.empty())
    {
        return false;
    }

    const int64_t label_condition = static_cast<int64_t>(CustomCondition::LabelSearchResult);
    if (custom.ids.contains(label_condition) && label_search_result_ids_.contains(label_id))
    {
        return true;
    }
    for (const int64_t condition_id : custom.ids)
    {
        if (condition_id != label_condition && matchesCustomImageCondition(image_id, condition_id))
        {
            return true;
        }
    }
    return false;
}

bool GlobalFilter::matchesTags(const int64_t image_id, const std::unordered_set<int64_t> &tag_ids) const
{
    if (tag_ids.empty())
    {
        return false;
    }

    ImageInstancesListModel *images = imageSource();
    LabelInstancesListModel *labels = labelSource();
    const ImageInstance *image = images != nullptr ? images->getImageInstance(image_id) : nullptr;
    if (image == nullptr)
    {
        return false;
    }
    if (intersects(image->tagIds(), tag_ids))
    {
        return true;
    }
    if (labels == nullptr)
    {
        return false;
    }
    for (const int64_t label_id : image->labelIds())
    {
        const LabelInstance *label = labels->getLabelInstance(label_id);
        if (label != nullptr && intersects(label->tagIds(), tag_ids))
        {
            return true;
        }
    }
    return false;
}

bool GlobalFilter::matchesImageLabelClasses(const int64_t image_id,
                                             const std::unordered_set<int64_t> &label_class_ids) const
{
    if (label_class_ids.empty())
    {
        return false;
    }
    ImageInstancesListModel *images = imageSource();
    LabelInstancesListModel *labels = labelSource();
    const ImageInstance *image = images != nullptr ? images->getImageInstance(image_id) : nullptr;
    if (image == nullptr || labels == nullptr)
    {
        return false;
    }
    for (const int64_t label_id : image->labelIds())
    {
        if (label_class_ids.contains(labels->getLabelClassId(label_id)))
        {
            return true;
        }
    }
    return false;
}

bool GlobalFilter::matchesCustomImageCondition(const int64_t image_id, const int64_t condition_id) const
{
    switch (static_cast<CustomCondition>(condition_id))
    {
    case CustomCondition::DuplicateFileName:
        rebuildDuplicateIndexes();
        return duplicate_file_name_image_ids_.contains(image_id);
    case CustomCondition::DuplicatePath:
        rebuildDuplicateIndexes();
        return duplicate_path_image_ids_.contains(image_id);
    case CustomCondition::UniqueFileName:
        rebuildDuplicateIndexes();
        return unique_file_name_image_ids_.contains(image_id);
    case CustomCondition::ImageSearchResult:
        return image_search_result_ids_.contains(image_id);
    case CustomCondition::LabelSearchResult:
    {
        ImageInstancesListModel *images = imageSource();
        const ImageInstance *image = images != nullptr ? images->getImageInstance(image_id) : nullptr;
        if (image == nullptr)
        {
            return false;
        }
        for (const int64_t label_id : image->labelIds())
        {
            if (label_search_result_ids_.contains(label_id))
            {
                return true;
            }
        }
        return false;
    }
    }
    return false;
}

bool GlobalFilter::customConditionAvailable(const int64_t condition_id) const
{
    switch (static_cast<CustomCondition>(condition_id))
    {
    case CustomCondition::DuplicateFileName:
    case CustomCondition::DuplicatePath:
    case CustomCondition::UniqueFileName:
        return true;
    case CustomCondition::ImageSearchResult:
        return !image_search_result_ids_.empty();
    case CustomCondition::LabelSearchResult:
        return !label_search_result_ids_.empty();
    }
    return false;
}

bool GlobalFilter::usesRegularCustomCondition() const
{
    const IdFilter &custom = filter(FilterType::Custom);
    return custom.ids.contains(static_cast<int64_t>(CustomCondition::DuplicateFileName))
        || custom.ids.contains(static_cast<int64_t>(CustomCondition::DuplicatePath))
        || custom.ids.contains(static_cast<int64_t>(CustomCondition::UniqueFileName));
}

void GlobalFilter::rebuildDuplicateIndexes() const
{
    if (duplicate_indexes_valid_ || !usesRegularCustomCondition())
    {
        return;
    }

    duplicate_file_name_image_ids_.clear();
    duplicate_path_image_ids_.clear();
    unique_file_name_image_ids_.clear();

    ImageInstancesListModel *images = imageSource();
    if (images == nullptr)
    {
        duplicate_indexes_valid_ = true;
        return;
    }

    QHash<QString, int> file_name_counts;
    QHash<QString, int> path_counts;
    for (int row = 0; row < images->rowCount(); ++row)
    {
        const int64_t image_id = images->data(images->index(row, 0), ImageInstancesListModel::ImageIdRole).toLongLong();
        if (!acceptsImageWithoutCustom(image_id))
        {
            continue;
        }
        const QString file_name = images->getImageName(image_id).toCaseFolded();
        const QString path = normalizedPath(images->getImagePath(image_id));
        if (!file_name.isEmpty())
        {
            ++file_name_counts[file_name];
        }
        if (!path.isEmpty())
        {
            ++path_counts[path];
        }
    }

    for (int row = 0; row < images->rowCount(); ++row)
    {
        const int64_t image_id = images->data(images->index(row, 0), ImageInstancesListModel::ImageIdRole).toLongLong();
        if (!acceptsImageWithoutCustom(image_id))
        {
            continue;
        }
        const QString file_name = images->getImageName(image_id).toCaseFolded();
        const QString path = normalizedPath(images->getImagePath(image_id));
        if (!file_name.isEmpty() && file_name_counts[file_name] > 1)
        {
            duplicate_file_name_image_ids_.insert(image_id);
        }
        if (!file_name.isEmpty() && file_name_counts[file_name] == 1)
        {
            unique_file_name_image_ids_.insert(image_id);
        }
        if (!path.isEmpty() && path_counts[path] > 1)
        {
            duplicate_path_image_ids_.insert(image_id);
        }
    }
    duplicate_indexes_valid_ = true;
}

void GlobalFilter::invalidateDuplicateIndexes()
{
    duplicate_indexes_valid_ = false;
}

void GlobalFilter::notifyFilterChanged()
{
    invalidateDuplicateIndexes();
    emit filterChanged();
    emit filterApplied();
    emit filterStateChanged();
}

void GlobalFilter::notifyStateChanged(const bool notify_search_results)
{
    emit filterStateChanged();
    if (notify_search_results)
    {
        emit customFilterSearchResultsChanged(!image_search_result_ids_.empty(), !label_search_result_ids_.empty());
    }
}

void GlobalFilter::updateCustomEnabledAfterSearchRemoval()
{
    IdFilter &custom = filter(FilterType::Custom);
    if (custom.ids.empty() && !custom_empty_selection_enabled_)
    {
        custom.enabled = false;
    }
}

} // namespace dltool::data
