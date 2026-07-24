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

    QString labelClassColor(const qint64 label_class_id) const override
    {
        return data_manager_.labelClassColor(label_class_id);
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
                                        DataOperationWorkflow::Completion completion)
{
    if (context == nullptr)
    {
        return;
    }

    if (isDataOperationRunning())
    {
        DataOperationWorkflow::Result result;
        result.error = QString("当前已有数据操作正在进行中");
        if (completion)
        {
            completion(result);
        }
        return;
    }

    if (labels_loading_)
    {
        DataOperationWorkflow::Result result;
        result.error = QString("标注正在加载，请稍后再试");
        if (completion)
        {
            completion(result);
        }
        return;
    }

    if (!work)
    {
        DataOperationWorkflow::start(
            context, std::move(options),
            [](DataOperationWorkflow::Result &result) { result.error = QString("数据集导出工作为空"); },
            std::move(completion));
        return;
    }

    setDataOperationRunning(true);
    auto finish = [this, completion = std::move(completion)](const DataOperationWorkflow::Result &result) mutable
    {
        setDataOperationRunning(false);
        if (completion)
        {
            completion(result);
        }
    };

    DataOperationWorkflow::start(
        context, std::move(options),
        [this, request = std::move(request), work = std::move(work)](DataOperationWorkflow::Result &result) mutable
        {
            DataManagerDatasetExportSource source(*this, std::move(request.dataset_ids));
            work(source, result);
        },
        std::move(finish));
}

} // namespace dltool::data
