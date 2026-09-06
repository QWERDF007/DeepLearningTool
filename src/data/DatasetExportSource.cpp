#include "data/DataManager.h"

#include "data/DatasetExportSource.h"

#include <algorithm>
#include <utility>

namespace dltool::data {

namespace {

DatasetExportSnapshot makeDatasetExportSnapshot(const DataManager &data_manager,
                                                 const std::vector<int64_t> &dataset_ids)
{
    DatasetExportSnapshot::SnapshotData data;

    for (const int64_t dataset_id : dataset_ids)
    {
        data.datasets.emplace(
            dataset_id, DatasetExportSnapshot::Dataset{dataset_id, data_manager.datasetName(dataset_id)});
    }

    std::vector<int64_t> image_ids = data_manager.imageIdsForDatasets(dataset_ids);
    std::sort(image_ids.begin(), image_ids.end());
    image_ids.erase(std::unique(image_ids.begin(), image_ids.end()), image_ids.end());
    for (const int64_t image_id : image_ids)
    {
        DatasetExportSnapshot::Image image;
        image.id                     = image_id;
        image.dataset_id             = data_manager.imageDatasetId(image_id);
        image.path                   = data_manager.imagePath(image_id);
        image.image_level_label_data = data_manager.getImageLevelLabelData(image_id);
        image.label_ids              = data_manager.imageLabelIds(image_id);
        std::sort(image.label_ids.begin(), image.label_ids.end());
        image.label_ids.erase(std::unique(image.label_ids.begin(), image.label_ids.end()), image.label_ids.end());
        data.images.emplace(image_id, std::move(image));

        for (const int64_t label_id : data.images.at(image_id).label_ids)
        {
            if (data.labels.contains(label_id))
                continue;

            const int64_t label_class_id = data_manager.labelClassId(label_id);
            data.labels.emplace(label_id, DatasetExportSnapshot::Label{label_id, label_class_id,
                                                                         data_manager.labelData(label_id)});
            if (!data.label_classes.contains(label_class_id))
            {
                data.label_classes.emplace(
                    label_class_id,
                    DatasetExportSnapshot::LabelClass{label_class_id,
                                                      data_manager.labelClassName(label_class_id),
                                                      data_manager.labelClassColor(label_class_id),
                                                      data_manager.labelClassGroup(label_class_id)});
            }
        }
    }

    return DatasetExportSnapshot(std::move(data));
}

} // namespace

DatasetExportSnapshot::DatasetExportSnapshot(SnapshotData data)
    : data_(std::move(data))
{
    for (auto &entry : data_.images)
    {
        auto &image = entry.second;
        std::sort(image.label_ids.begin(), image.label_ids.end());
        image.label_ids.erase(std::unique(image.label_ids.begin(), image.label_ids.end()), image.label_ids.end());
    }
}

std::vector<int64_t> DatasetExportSnapshot::allImageIds() const
{
    std::vector<int64_t> image_ids;
    image_ids.reserve(data_.images.size());
    for (const auto &entry : data_.images)
    {
        image_ids.push_back(entry.first);
    }
    return image_ids;
}

qint64 DatasetExportSnapshot::imageDatasetId(const qint64 image_id) const
{
    const auto found = data_.images.find(image_id);
    return found == data_.images.end() ? -1 : found->second.dataset_id;
}

QString DatasetExportSnapshot::imagePath(const qint64 image_id) const
{
    const auto found = data_.images.find(image_id);
    return found == data_.images.end() ? QString() : found->second.path;
}

QVariantMap DatasetExportSnapshot::imageLevelLabelData(const qint64 image_id) const
{
    const auto found = data_.images.find(image_id);
    return found == data_.images.end() ? QVariantMap() : found->second.image_level_label_data;
}

std::vector<int64_t> DatasetExportSnapshot::imageLabelIds(const qint64 image_id) const
{
    const auto found = data_.images.find(image_id);
    return found == data_.images.end() ? std::vector<int64_t>{} : found->second.label_ids;
}

qint64 DatasetExportSnapshot::labelClassId(const qint64 label_id) const
{
    const auto found = data_.labels.find(label_id);
    return found == data_.labels.end() ? -1 : found->second.class_id;
}

QVariantMap DatasetExportSnapshot::labelData(const qint64 label_id) const
{
    const auto found = data_.labels.find(label_id);
    return found == data_.labels.end() ? QVariantMap() : found->second.data;
}

QString DatasetExportSnapshot::labelClassName(const qint64 label_class_id) const
{
    const auto found = data_.label_classes.find(label_class_id);
    return found == data_.label_classes.end() ? QString() : found->second.name;
}

QString DatasetExportSnapshot::labelClassColor(const qint64 label_class_id) const
{
    const auto found = data_.label_classes.find(label_class_id);
    return found == data_.label_classes.end() ? QString() : found->second.color;
}

QString DatasetExportSnapshot::labelClassGroup(const qint64 label_class_id) const
{
    const auto found = data_.label_classes.find(label_class_id);
    return found == data_.label_classes.end() ? QString() : found->second.group;
}

QString DatasetExportSnapshot::datasetName(const qint64 dataset_id) const
{
    const auto found = data_.datasets.find(dataset_id);
    return found == data_.datasets.end() ? QString() : found->second.name;
}

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

    DatasetExportSnapshot snapshot = makeDatasetExportSnapshot(*this, request.dataset_ids);
    DataOperationWorkflow::start(
        context, std::move(options),
        [snapshot = std::move(snapshot), work = std::move(work)](DataOperationWorkflow::Result &result) mutable
        {
            work(snapshot, result);
        },
        std::move(finish));
}

} // namespace dltool::data
