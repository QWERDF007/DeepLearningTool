#include "data/DataManager.h"

#include "data/DatasetExportSource.h"

#include <algorithm>
#include <utility>

namespace dltool::data {

namespace {

class DataManagerDatasetExportSource final : public DatasetExportSource
{
public:
    DataManagerDatasetExportSource(const DataManager &data_manager, std::vector<int64_t> dataset_ids)
        : data_manager_(data_manager)
        , dataset_ids_(std::move(dataset_ids))
    {
    }

    std::vector<int64_t> allImageIds() const override
    {
        std::vector<int64_t> image_ids = data_manager_.imageIdsForDatasets(dataset_ids_);
        std::sort(image_ids.begin(), image_ids.end());
        return image_ids;
    }

    qint64 imageDatasetId(const qint64 image_id) const override
    {
        return data_manager_.imageDatasetId(image_id);
    }

    QString imagePath(const qint64 image_id) const override
    {
        return data_manager_.imagePath(image_id);
    }

    QVariantMap imageLevelLabelData(const qint64 image_id) const override
    {
        return data_manager_.getImageLevelLabelData(image_id);
    }

    std::vector<int64_t> imageLabelIds(const qint64 image_id) const override
    {
        std::vector<int64_t> label_ids = data_manager_.imageLabelIds(image_id);
        std::sort(label_ids.begin(), label_ids.end());
        return label_ids;
    }

    qint64 labelClassId(const qint64 label_id) const override
    {
        return data_manager_.labelClassId(label_id);
    }

    QVariantMap labelData(const qint64 label_id) const override
    {
        return data_manager_.labelData(label_id);
    }

    QString labelClassName(const qint64 label_class_id) const override
    {
        return data_manager_.labelClassName(label_class_id);
    }

    QString labelClassGroup(const qint64 label_class_id) const override
    {
        return data_manager_.labelClassGroup(label_class_id);
    }

    QString datasetName(const qint64 dataset_id) const override
    {
        return data_manager_.datasetName(dataset_id);
    }

private:
    const DataManager       &data_manager_;
    std::vector<int64_t>     dataset_ids_;
};

} // namespace

void DataManager::runDatasetExportAsync(QObject *context, DatasetExportRequest request,
                                        DataOperationWorkflow::Options options, DatasetExportWork work,
                                        DataOperationWorkflow::Completion completion) const
{
    if (!work)
    {
        DataOperationWorkflow::start(
            context, std::move(options),
            [](DataOperationWorkflow::Result &result) { result.error = QStringLiteral("数据集导出工作为空"); },
            std::move(completion));
        return;
    }

    DataOperationWorkflow::start(
        context, std::move(options),
        [this, request = std::move(request), work = std::move(work)](DataOperationWorkflow::Result &result) mutable
        {
            DataManagerDatasetExportSource source(*this, std::move(request.dataset_ids));
            work(source, result);
        },
        std::move(completion));
}

} // namespace dltool::data
