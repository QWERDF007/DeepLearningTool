#include "data/GlobalFilter.h"

#include "data/DatasetFilterModule.h"
#include "data/Datasets.h"
#include "data/ImageTags.h"
#include "data/Images.h"
#include "data/Labels.h"
#include "data/TagFilterModule.h"

namespace dltool::data {

GlobalFilter::GlobalFilter(ImageInstancesListModel *image_model, LabelInstancesListModel *label_model, QObject *parent)
    : QObject(parent)
    , image_model_(image_model)
    , label_model_(label_model)
{
    // 过滤模块将在DataManager完全构造后通过initializeFilterModules()初始化
}

GlobalFilter::~GlobalFilter()
{
    // unique_ptr会自动清理资源
}

void GlobalFilter::initializeFilterModules(DatasetsListModel *datasets_model, ImageTagsListModel *tags_model)
{
    // 初始化数据集过滤模块
    dataset_filter_ = std::make_unique<DatasetFilterModule>(image_model_, datasets_model, this);

    // 初始化标签过滤模块
    tag_filter_ = std::make_unique<TagFilterModule>(image_model_, tags_model, this);

    // 连接过滤模块信号到applyFilters槽
    connect(dataset_filter_.get(), &FilterModule::criteriaChanged, this, &GlobalFilter::applyFilters);
    connect(dataset_filter_.get(), &FilterModule::enabledChanged, this, &GlobalFilter::applyFilters);

    connect(tag_filter_.get(), &FilterModule::criteriaChanged, this, &GlobalFilter::applyFilters);
    connect(tag_filter_.get(), &FilterModule::enabledChanged, this, &GlobalFilter::applyFilters);
}

bool GlobalFilter::isActive() const
{
    return (dataset_filter_ && dataset_filter_->isActive()) || (tag_filter_ && tag_filter_->isActive());
}

int GlobalFilter::activeFilterCount() const
{
    int count = 0;
    if (dataset_filter_ && dataset_filter_->isActive())
    {
        count++;
    }
    if (tag_filter_ && tag_filter_->isActive())
    {
        count++;
    }
    return count;
}

QString GlobalFilter::filterSummary() const
{
    QStringList summary_parts;

    if (dataset_filter_ && dataset_filter_->isActive())
    {
        auto criteria = dataset_filter_->getActiveCriteria();
        summary_parts.append(QString("数据集: %1").arg(criteria.size()));
    }

    if (tag_filter_ && tag_filter_->isActive())
    {
        auto criteria = tag_filter_->getActiveCriteria();
        summary_parts.append(QString("标签: %1").arg(criteria.size()));
    }

    if (summary_parts.isEmpty())
    {
        return "无过滤";
    }

    return summary_parts.join(", ");
}

void GlobalFilter::setDatasetFilter(const std::vector<int64_t> &dataset_ids)
{
    if (dataset_filter_)
    {
        dataset_filter_->setCriteria(dataset_ids);
        updateFilterCriteria();
        applyFilters();
        emit filterStateChanged();
    }
}

void GlobalFilter::setTagFilter(const std::vector<int64_t> &tag_ids)
{
    if (tag_filter_)
    {
        tag_filter_->setCriteria(tag_ids);
        updateFilterCriteria();
        applyFilters();
        emit filterStateChanged();
    }
}

void GlobalFilter::setDatasetFilterEnabled(bool enabled)
{
    if (dataset_filter_)
    {
        bool was_enabled = dataset_filter_->isEnabled();
        dataset_filter_->setEnabled(enabled);

        // 如果启用状态发生变化，强制更新过滤条件并重新应用
        if (was_enabled != enabled)
        {
            updateFilterCriteria();
            // 强制重新应用过滤，即使条件看起来没变
            // 因为启用状态的变化本身就需要重新过滤
            previous_criteria_ = FilterCriteria(); // 重置以强制重新过滤
            applyFilters();
        }

        emit filterStateChanged();
    }
}

void GlobalFilter::setTagFilterEnabled(bool enabled)
{
    if (tag_filter_)
    {
        bool was_enabled = tag_filter_->isEnabled();
        tag_filter_->setEnabled(enabled);

        // 如果启用状态发生变化，强制更新过滤条件并重新应用
        if (was_enabled != enabled)
        {
            updateFilterCriteria();
            // 强制重新应用过滤，即使条件看起来没变
            // 因为启用状态的变化本身就需要重新过滤
            previous_criteria_ = FilterCriteria(); // 重置以强制重新过滤
            applyFilters();
        }

        emit filterStateChanged();
    }
}

void GlobalFilter::clearAllFilters()
{
    bool changed = false;

    if (dataset_filter_)
    {
        dataset_filter_->clear();
        dataset_filter_->setEnabled(false);
        changed = true;
    }

    if (tag_filter_)
    {
        tag_filter_->clear();
        tag_filter_->setEnabled(false);
        changed = true;
    }

    if (changed)
    {
        updateFilterCriteria();
        applyFilters();
        emit filterStateChanged();
    }
}

void GlobalFilter::clearDatasetFilter()
{
    if (dataset_filter_)
    {
        dataset_filter_->clear();
        updateFilterCriteria();
        applyFilters();
        emit filterStateChanged();
    }
}

void GlobalFilter::clearTagFilter()
{
    if (tag_filter_)
    {
        tag_filter_->clear();
        updateFilterCriteria();
        applyFilters();
        emit filterStateChanged();
    }
}

std::vector<int64_t> GlobalFilter::getActiveDatasetIds() const
{
    if (dataset_filter_ && dataset_filter_->isActive())
    {
        auto criteria = dataset_filter_->getActiveCriteria();
        return std::vector<int64_t>(criteria.begin(), criteria.end());
    }
    return std::vector<int64_t>();
}

std::vector<int64_t> GlobalFilter::getActiveTagIds() const
{
    if (tag_filter_ && tag_filter_->isActive())
    {
        auto criteria = tag_filter_->getActiveCriteria();
        return std::vector<int64_t>(criteria.begin(), criteria.end());
    }
    return std::vector<int64_t>();
}

void GlobalFilter::applyFilters()
{
    // 检查是否有激活的过滤器
    if (!isActive())
    {
        // 没有激活的过滤器，清除模型上的现有过滤
        if (image_model_)
        {
            image_model_->clearFilter();
        }
        if (label_model_)
        {
            label_model_->clearFilter();
        }

        // 重置上一次的过滤条件，因为没有激活的过滤器
        previous_criteria_ = FilterCriteria();

        emit filterApplied();
        return;
    }

    // 检查过滤条件是否改变 - 如果没有改变，跳过过滤操作（性能优化）
    if (!hasFilterCriteriaChanged())
    {
        // 条件未改变，无需重新应用过滤
        return;
    }

    // 应用过滤到图像模型
    if (image_model_)
    {
        // 创建lambda函数，捕获this并检查图像是否应该被包含
        auto image_filter_func = [this](int64_t image_id) -> bool
        {
            return shouldIncludeImage(image_id);
        };

        image_model_->applyFilter(image_filter_func);
    }

    // 应用过滤到标注模型
    if (label_model_)
    {
        // 创建lambda函数检查图像是否应该被包含
        // 注意：LabelInstancesListModel期望接收image_id参数，而不是label_id
        auto image_filter_func = [this](int64_t image_id) -> bool
        {
            return shouldIncludeImage(image_id);
        };

        label_model_->applyFilter(image_filter_func);
    }

    // 成功过滤后更新上一次的过滤条件
    previous_criteria_ = current_criteria_;

    emit filterApplied();
}

void GlobalFilter::updateFilterCriteria()
{
    // 从各个过滤模块更新current_criteria_结构
    current_criteria_.dataset_ids.clear();
    current_criteria_.tag_ids.clear();

    if (dataset_filter_ && dataset_filter_->isActive())
    {
        current_criteria_.dataset_ids = dataset_filter_->getActiveCriteria();
    }

    if (tag_filter_ && tag_filter_->isActive())
    {
        current_criteria_.tag_ids = tag_filter_->getActiveCriteria();
    }
}

bool GlobalFilter::shouldIncludeImage(int64_t image_id) const
{
    // 图像被包含当且仅当它通过所有启用的过滤模块（AND逻辑）

    // 检查数据集过滤器（如果启用）
    if (dataset_filter_ && dataset_filter_->isEnabled())
    {
        if (!dataset_filter_->passes(image_id))
        {
            return false; // 未通过数据集过滤
        }
    }

    // 检查标签过滤器（如果启用）
    if (tag_filter_ && tag_filter_->isEnabled())
    {
        if (!tag_filter_->passes(image_id))
        {
            return false; // 未通过标签过滤
        }
    }

    // 通过所有启用的过滤器（或没有启用的过滤器）
    return true;
}

bool GlobalFilter::shouldIncludeLabel(int64_t label_id) const
{
    // 标注被包含当且仅当其关联的图像通过图像过滤器
    if (label_model_)
    {
        int64_t image_id = label_model_->getImageId(label_id);
        return shouldIncludeImage(image_id);
    }

    return false;
}

bool GlobalFilter::hasFilterCriteriaChanged() const
{
    // 比较当前条件与上一次条件
    // 检查数据集ID是否改变
    if (current_criteria_.dataset_ids != previous_criteria_.dataset_ids)
    {
        return true;
    }

    // 检查标签ID是否改变
    if (current_criteria_.tag_ids != previous_criteria_.tag_ids)
    {
        return true;
    }

    // 未检测到变化
    return false;
}

} // namespace dltool::data
