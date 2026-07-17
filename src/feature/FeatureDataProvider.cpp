#include "feature/FeatureDataProvider.h"

#include "data/DataManager.h"

namespace dltool::feature {

FeatureDataProvider::FeatureDataProvider(dltool::data::DataManager *data_manager)
    : data_manager_(data_manager)
{
}

FeatureDataProvider::~FeatureDataProvider() = default;

std::vector<int64_t> FeatureDataProvider::allImageIds() const
{
    return data_manager_ ? data_manager_->allImageIds() : std::vector<int64_t>{};
}

QString FeatureDataProvider::imagePath(int64_t image_id) const
{
    return data_manager_ ? data_manager_->imagePath(image_id) : QString();
}

int64_t FeatureDataProvider::imageDatasetId(int64_t image_id) const
{
    return data_manager_ ? data_manager_->imageDatasetId(image_id) : -1;
}

int64_t FeatureDataProvider::imageLabelClassId(int64_t image_id) const
{
    return data_manager_ ? data_manager_->imageLabelClassId(image_id) : -1;
}

std::vector<int64_t> FeatureDataProvider::imageLabelIds(int64_t image_id) const
{
    return data_manager_ ? data_manager_->imageLabelIds(image_id) : std::vector<int64_t>{};
}

QString FeatureDataProvider::projectDir() const
{
    return data_manager_ ? data_manager_->projectDir() : QString();
}

std::vector<int64_t> FeatureDataProvider::allLabelIds() const
{
    return data_manager_ ? data_manager_->allLabelIds() : std::vector<int64_t>{};
}

int64_t FeatureDataProvider::labelImageId(int64_t label_id) const
{
    return data_manager_ ? data_manager_->labelImageId(label_id) : -1;
}

int64_t FeatureDataProvider::labelClassId(int64_t label_id) const
{
    return data_manager_ ? data_manager_->labelClassId(label_id) : -1;
}

QVariantMap FeatureDataProvider::labelData(int64_t label_id) const
{
    return data_manager_ ? data_manager_->labelData(label_id) : QVariantMap();
}

QString FeatureDataProvider::labelClassName(int64_t label_class_id) const
{
    return data_manager_ ? data_manager_->labelClassName(label_class_id) : QString();
}

QString FeatureDataProvider::datasetName(int64_t dataset_id) const
{
    return data_manager_ ? data_manager_->datasetName(dataset_id) : QString();
}

dltool::data::DataManager *FeatureDataProvider::dataManager() const
{
    return data_manager_;
}

} // namespace dltool::feature
