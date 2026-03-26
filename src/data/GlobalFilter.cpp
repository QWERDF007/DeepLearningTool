#include "data/GlobalFilter.h"

#include "data/DatasetFilterModule.h"
#include "data/Datasets.h"
#include "data/ImageLabelClassFilterModule.h"
#include "data/ImageTags.h"
#include "data/Images.h"
#include "data/LabelClassFilterModule.h"
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

GlobalFilter::~GlobalFilter() {}

void GlobalFilter::initializeFilterModules(DatasetsListModel *datasets_model, ImageTagsListModel *tags_model,
                                           LabelClassesListModel *label_classes_model)
{
    // 初始化数据集过滤模块
    dataset_filter_ = std::make_unique<DatasetFilterModule>(image_model_, datasets_model, this);

    // 初始化标签过滤模块
    tag_filter_ = std::make_unique<TagFilterModule>(image_model_, tags_model, this);

    // 初始化标注类别过滤模块
    label_class_filter_ = std::make_unique<LabelClassFilterModule>(this);

    // 初始化图像级标注类别过滤模块
    image_label_class_filter_
        = std::make_unique<ImageLabelClassFilterModule>(image_model_, label_model_, label_classes_model, this);

    // 注册过滤模块到map中
    filter_modules_[FilterType::Dataset]         = dataset_filter_.get();
    filter_modules_[FilterType::Tag]             = tag_filter_.get();
    filter_modules_[FilterType::LabelClass]      = label_class_filter_.get();
    filter_modules_[FilterType::ImageLabelClass] = image_label_class_filter_.get();

    // 连接过滤模块信号到applyFilters槽（使用循环遍历map）
    for (auto &[type, module] : filter_modules_)
    {
        connect(module, &FilterModule::criteriaChanged, this, &GlobalFilter::applyFilters);
        connect(module, &FilterModule::enabledChanged, this, &GlobalFilter::applyFilters);
    }
}

FilterModule *GlobalFilter::getFilterModule(FilterType type) const
{
    auto it = filter_modules_.find(type);
    if (it == filter_modules_.end())
    {
        qWarning() << "GlobalFilter: Invalid filter type requested:" << static_cast<int>(type);
        return nullptr;
    }
    return it->second;
}

void GlobalFilter::setFilter(FilterType type, const std::vector<int64_t> &ids)
{
    FilterModule *module = getFilterModule(type);
    if (!module)
    {
        qWarning() << "GlobalFilter: Cannot set filter for invalid type:" << static_cast<int>(type);
        return;
    }

    module->setCriteria(ids);
    updateFilterCriteria();
    applyFilters();
    emit filterStateChanged();
}

void GlobalFilter::setFilterEnabled(FilterType type, bool enabled)
{
    FilterModule *module = getFilterModule(type);
    if (!module)
    {
        qWarning() << "GlobalFilter: Cannot set filter enabled for invalid type:" << static_cast<int>(type);
        return;
    }

    bool was_enabled = module->isEnabled();
    module->setEnabled(enabled);

    // 如果启用状态发生变化，强制更新过滤条件并重新应用
    if (was_enabled != enabled)
    {
        updateFilterCriteria();
        // 强制重新应用过滤，即使条件看起来没变
        // 因为启用状态的变化本身就需要重新过滤
        force_apply_ = true;
        applyFilters();
    }

    emit filterStateChanged();
}

void GlobalFilter::clearFilter(FilterType type)
{
    FilterModule *module = getFilterModule(type);
    if (!module)
    {
        qWarning() << "GlobalFilter: Cannot clear filter for invalid type:" << static_cast<int>(type);
        return;
    }

    module->clear();
    updateFilterCriteria();
    applyFilters();
    emit filterStateChanged();
}

std::vector<int64_t> GlobalFilter::getActiveIds(FilterType type) const
{
    FilterModule *module = getFilterModule(type);
    if (!module)
    {
        return std::vector<int64_t>();
    }

    if (!module->isActive())
    {
        return std::vector<int64_t>();
    }

    auto criteria = module->getActiveCriteria();
    return std::vector<int64_t>(criteria.begin(), criteria.end());
}

bool GlobalFilter::isActive() const
{
    // 遍历 filter_modules_ map，检查任一模块是否激活
    for (const auto &[type, module] : filter_modules_)
    {
        if (module && module->isActive())
        {
            return true;
        }
    }
    return false;
}

int GlobalFilter::activeFilterCount() const
{
    // 遍历 filter_modules_ map，计数激活的模块数量
    int count = 0;
    for (const auto &[type, module] : filter_modules_)
    {
        if (module && module->isActive())
        {
            count++;
        }
    }
    return count;
}

QString GlobalFilter::filterSummary() const
{
    QStringList summary_parts;

    // 遍历 filter_modules_ map，为每个激活的模块生成摘要
    for (const auto &[type, module] : filter_modules_)
    {
        if (module && module->isActive())
        {
            auto criteria = module->getActiveCriteria();

            // 使用 FilterType 枚举确定显示文本
            QString type_name;
            switch (type)
            {
            case FilterType::Dataset:
                type_name = "数据集";
                break;
            case FilterType::Tag:
                type_name = "标签";
                break;
            case FilterType::LabelClass:
                type_name = "类别";
                break;
            case FilterType::ImageLabelClass:
                type_name = "图像类别";
                break;
            default:
                type_name = "未知";
                break;
            }

            summary_parts.append(QString("%1: %2").arg(type_name).arg(criteria.size()));
        }
    }

    if (summary_parts.isEmpty())
    {
        return "无过滤";
    }

    return summary_parts.join(", ");
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

    if (label_class_filter_)
    {
        label_class_filter_->clear();
        label_class_filter_->setEnabled(false);
        changed = true;
    }

    if (image_label_class_filter_)
    {
        image_label_class_filter_->clear();
        image_label_class_filter_->setEnabled(false);
        changed = true;
    }

    if (changed)
    {
        updateFilterCriteria();
        applyFilters();
        emit filterStateChanged();
    }
}

void GlobalFilter::applyFilters()
{
    // 如果需要强制应用过滤（例如启用状态变化），跳过“条件未变则不重新过滤”的优化
    const bool should_force_apply = force_apply_;
    force_apply_                  = false;

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
    if (!should_force_apply && !hasFilterCriteriaChanged())
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

        auto label_class_filter_func = [this](int64_t label_class_id) -> bool
        {
            if (label_class_filter_ && label_class_filter_->isEnabled())
            {
                return label_class_filter_->passes(label_class_id);
            }
            return true;
        };

        label_model_->applyFilter(image_filter_func, label_class_filter_func);
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
    current_criteria_.label_class_ids.clear();
    current_criteria_.image_label_class_ids.clear();

    if (dataset_filter_ && dataset_filter_->isActive())
    {
        current_criteria_.dataset_ids = dataset_filter_->getActiveCriteria();
    }

    if (tag_filter_ && tag_filter_->isActive())
    {
        current_criteria_.tag_ids = tag_filter_->getActiveCriteria();
    }

    if (label_class_filter_ && label_class_filter_->isActive())
    {
        current_criteria_.label_class_ids = label_class_filter_->getActiveCriteria();
    }

    if (image_label_class_filter_ && image_label_class_filter_->isActive())
    {
        current_criteria_.image_label_class_ids = image_label_class_filter_->getActiveCriteria();
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

    // 注意：LabelClass 过滤仅作用于 label instances，不作用于 image instances

    // 图像级标注类别过滤（如果启用）
    if (image_label_class_filter_ && image_label_class_filter_->isEnabled())
    {
        if (!image_label_class_filter_->passes(image_id))
        {
            return false;
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

    // 检查标注类别ID是否改变
    if (current_criteria_.label_class_ids != previous_criteria_.label_class_ids)
    {
        return true;
    }

    // 检查图像级标注类别ID是否改变
    if (current_criteria_.image_label_class_ids != previous_criteria_.image_label_class_ids)
    {
        return true;
    }

    // 未检测到变化
    return false;
}

} // namespace dltool::data
