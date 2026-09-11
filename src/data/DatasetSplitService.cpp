#include "DatasetSplitService.h"

#include "common/Utils.h"
#include "core/CoreDef.h"
#include "data/DataViewModels.h"
#include "data/DatasetSplitter.h"
#include "data/GlobalFilter.h"
#include "data/LabelData.h"
#include "database/DataBase.h"
#include "ui/SignalHelper.h"

#include <spdlog/spdlog.h>

#include <set>
#include <utility>

namespace dltool::data {

namespace {

struct DatasetSplitRequest
{
    int                                                                label_data_method{-1};
    std::vector<dltool::database::ProjectDataBase::DatasetSplitTarget> targets;
};

} // namespace

DatasetSplitService::DatasetSplitService(DataManagerServices services) : s_(std::move(services)) {}

DatasetSplitService::~DatasetSplitService() = default;

struct DatasetSplitService::DatasetSplitCopyResult
{
    std::vector<int64_t>             dataset_ids;
    std::vector<QString>             dataset_names;
    std::vector<LoadedImageInstance> images;
    std::vector<LoadedLabelInstance> labels;
};

void DatasetSplitService::commitDatasetSplit(const std::shared_ptr<DatasetSplitCopyResult> &result,
                                     const DataOperationWorkflow::Result           &operation)
{
    if (s_.isShuttingDown())
        return;

    if (result != nullptr && operation.success)
    {
        if (s_.is_labels_loading())
        {
            s_.mark_labels_changed_during_loading();
        }

        s_.datasets->addDatasetsFromMemory(result->dataset_ids, result->dataset_names);

        s_.image_instances->beginBulkUpdate();
        s_.label_instances->beginBulkUpdate();
        s_.image_source->addImagesFromMemory(result->images, true);
        s_.label_source->addLabelsFromMemory(result->labels, true);
        s_.image_source->syncAllLabelRelations(s_.label_source, false);
        s_.image_tags->addRelationsFromMemory(result->images, result->labels);
        s_.datasets->addImagesFromSource(s_.image_source, result->images);
        s_.image_source->refreshModelFromMemory();
        s_.label_source->refreshModelFromMemory();

        if (s_.global_filter != nullptr && s_.global_filter->isActive())
        {
            s_.global_filter->refresh();
        }
        s_.label_instances->endBulkUpdate();
        s_.image_instances->endBulkUpdate();

        const QString message = QString("已完成数据集划分，创建 %1 个子数据集、复制 %2 个图像和 %3 个标注，耗时 %4 ms")
                                    .arg(result->dataset_ids.size())
                                    .arg(result->images.size())
                                    .arg(result->labels.size())
                                    .arg(operation.elapsed_ms);
        spdlog::info("{}", message.toUtf8().constData());
        ui::SignalHelper::notifySuccess(QString("划分数据集完成"), message);
        s_.emit_dataset_split_finished(true, message);
    }
    else
    {
        const QString message = QString("划分数据集失败: %1")
                                    .arg(operation.error.isEmpty() ? QStringLiteral("后台操作失败") : operation.error);
        spdlog::error("{}", message.toUtf8().constData());
        ui::SignalHelper::notifyError(QString("划分数据集失败"), message);
        s_.emit_dataset_split_finished(false, message);
    }

    s_.set_data_operation_running(false);
}

void DatasetSplitService::splitDataset(const int64_t dataset_id, const double train_ratio, const double validation_ratio,
                               const double test_ratio, const bool use_validation)
{
    if (s_.isShuttingDown())
    {
        const QString message = QStringLiteral("数据管理器正在关闭");
        spdlog::warn("划分数据集失败: {}", message.toUtf8().constData());
        s_.emit_dataset_split_finished(false, message);
        return;
    }

    const auto reportFailure = [this](const QString &message)
    {
        spdlog::error("划分数据集失败: {}", message.toUtf8().constData());
        ui::SignalHelper::notifyError(QString("划分数据集失败"), message);
        s_.emit_dataset_split_finished(false, message);
    };

    if (s_.is_data_operation_running())
    {
        const QString message = QStringLiteral("当前已有数据操作正在进行中");
        ui::SignalHelper::notifyWarn(QString("划分数据集"), message);
        s_.emit_dataset_split_finished(false, message);
        return;
    }
    if (s_.database == nullptr || s_.datasets == nullptr || s_.image_source == nullptr || s_.label_source == nullptr)
    {
        reportFailure(QStringLiteral("数据管理器未初始化"));
        return;
    }
    if (s_.is_labels_loading())
    {
        reportFailure(QStringLiteral("标注正在加载，请稍后再试"));
        return;
    }
    if (!core::DeepLearningMethod::isSupportedMethod(s_.method))
    {
        reportFailure(QStringLiteral("当前项目类型不支持数据集划分"));
        return;
    }
    const QString source_dataset_name = s_.datasets->getDatasetName(dataset_id);
    if (dataset_id < 0 || source_dataset_name.isEmpty())
    {
        reportFailure(QStringLiteral("源数据集不存在"));
        return;
    }

    const auto                                      &all_images = s_.image_source->getAllImageInstances();
    std::vector<DatasetSplitItem>                                       items;
    std::map<int64_t, dltool::database::ProjectDataBase::ImageSnapshot> source_snapshots;
    for (const auto &[image_id, image] : all_images)
    {
        if (image == nullptr || image->datasetId() != dataset_id)
        {
            continue;
        }

        DatasetSplitItem item;
        item.image_id             = image_id;
        item.image_label_class_id = image->imageLabelClassId();

        dltool::database::ProjectDataBase::ImageSnapshot snapshot;
        snapshot.path       = image->path();
        snapshot.extra_data = ImageInstancesListModel::extraDataForImageLabelClassId(image->imageLabelClassId());
        const auto image_tags = image->tagIds();
        snapshot.tag_ids.assign(image_tags.begin(), image_tags.end());
        const bool copies_geometry_labels
            = s_.method == core::DeepLearningMethod::Detection
           || s_.method == core::DeepLearningMethod::Segmentation
           || s_.method == core::DeepLearningMethod::AnomalyDetection;
        if (copies_geometry_labels)
        {
            snapshot.labels.reserve(image->labelIds().size());
            for (const int64_t label_id : image->labelIds())
            {
                const LabelInstance *label = s_.label_source->getLabelInstance(label_id);
                if (label == nullptr || label->data() == nullptr)
                {
                    reportFailure(QString("图像 %1 的标注 %2 不存在或数据无效").arg(image_id).arg(label_id));
                    return;
                }

                item.label_class_ids.push_back(label->labelClassId());

                dltool::database::ProjectDataBase::LabelSnapshot label_snapshot;
                label_snapshot.label_class_id = label->labelClassId();
                label_snapshot.label_type     = label->data()->type();
                label_snapshot.data           = label->data()->toBlob();
                const auto label_tags = label->tagIds();
                label_snapshot.tag_ids.assign(label_tags.begin(), label_tags.end());
                snapshot.labels.push_back(std::move(label_snapshot));
            }
        }
        items.push_back(std::move(item));
        source_snapshots.emplace(image_id, std::move(snapshot));
    }

    DatasetSplitRatios ratios;
    ratios.train          = train_ratio;
    ratios.validation     = validation_ratio;
    ratios.test           = test_ratio;
    ratios.use_validation = use_validation;
    QString ratio_error;
    if (!DatasetSplitter::validateRatios(ratios, &ratio_error))
    {
        reportFailure(ratio_error);
        return;
    }
    if (items.empty())
    {
        reportFailure(QStringLiteral("不能划分空数据集"));
        return;
    }

    const DatasetSplitResult split = DatasetSplitter::split(items, s_.method, ratios, 0xD17A5EEDU);
    if (!split.success)
    {
        reportFailure(split.error);
        return;
    }

    auto request               = std::make_shared<DatasetSplitRequest>();
    request->label_data_method = s_.method;
    std::set<QString> reserved_names;
    const auto        uniqueName = [&](const QString &suffix)
    {
        const QString base_name = source_dataset_name + QStringLiteral("-") + suffix;
        QString       candidate = base_name;
        int           index     = 1;
        while (s_.datasets->getDatasetId(candidate) >= 0 || reserved_names.contains(candidate))
        {
            candidate = QStringLiteral("%1_%2").arg(base_name).arg(index++);
        }
        reserved_names.insert(candidate);
        return candidate;
    };

    auto build_target = [&](const QString &name, const std::vector<int64_t> &image_ids)
    {
        dltool::database::ProjectDataBase::DatasetSplitTarget target;
        target.name = name;
        target.images.reserve(image_ids.size());
        for (const int64_t id : image_ids)
        {
            auto it = source_snapshots.find(id);
            if (it != source_snapshots.end())
            {
                target.images.push_back(it->second);
            }
        }
        return target;
    };

    request->targets.push_back(build_target(uniqueName(QStringLiteral("Train")), split.train_image_ids));
    if (ratios.use_validation)
    {
        request->targets.push_back(build_target(uniqueName(QStringLiteral("Val")), split.validation_image_ids));
    }
    request->targets.push_back(build_target(uniqueName(QStringLiteral("Test")), split.test_image_ids));

    s_.set_data_operation_running(true);
    auto result = std::make_shared<DatasetSplitCopyResult>();
    DataOperationWorkflow::Options options;
    options.title         = QStringLiteral("划分数据集");
    options.start_message = QString("正在划分数据集: %1").arg(source_dataset_name);
    s_.track_operation(DataOperationWorkflow::startDatabase(
        s_.host, s_.database->path(), std::move(options),
        [request, result](dltool::database::ProjectDataBase &database,
                          DataOperationWorkflow::Result     &operation)
        {
            dltool::database::ProjectDataBase::AtomicSplitOutput output;
            if (!database.splitDatasetAtomic(request->targets, output, operation.error,
                                             [&operation]() { return operation.cancellationRequested(); }))
            {
                operation.success = false;
                if (operation.error == QStringLiteral("操作已取消"))
                {
                    operation.cancelled = true;
                }
                return;
            }

            result->dataset_ids = output.dataset_ids;
            result->dataset_names.reserve(request->targets.size());
            for (const auto &t : request->targets)
            {
                result->dataset_names.push_back(t.name);
            }

            LabelDataHelper helper = data::createLabelDataHelper(request->label_data_method);
            if (helper == nullptr)
            {
                operation.success = false;
                operation.error   = QStringLiteral("标签数据工厂未初始化");
                return;
            }

            size_t img_idx   = 0;
            size_t label_idx = 0;
            for (size_t target_idx = 0; target_idx < request->targets.size(); ++target_idx)
            {
                const auto   &target            = request->targets[target_idx];
                const int64_t target_dataset_id = output.dataset_ids[target_idx];

                for (const auto &source_img : target.images)
                {
                    const int64_t new_img_id = output.image_ids[img_idx++];

                    LoadedImageInstance img;
                    img.image_id       = new_img_id;
                    img.dataset_id     = target_dataset_id;
                    img.path           = source_img.path;
                    img.label_class_id = ImageInstancesListModel::imageLabelClassIdFromExtraData(source_img.extra_data);
                    img.tag_ids.insert(source_img.tag_ids.begin(), source_img.tag_ids.end());
                    result->images.push_back(std::move(img));

                    for (const auto &source_lbl : source_img.labels)
                    {
                        const int64_t new_lbl_id = output.label_ids[label_idx++];

                        LabelData label_data = helper->createLabelData();
                        if (label_data == nullptr)
                        {
                            operation.success = false;
                            operation.error   = QStringLiteral("标签数据创建失败");
                            return;
                        }
                        label_data->fromBlob(source_lbl.data);

                        LoadedLabelInstance lbl;
                        lbl.label_id       = new_lbl_id;
                        lbl.image_id       = new_img_id;
                        lbl.label_class_id = source_lbl.label_class_id;
                        lbl.data           = std::move(label_data);
                        lbl.tag_ids.insert(source_lbl.tag_ids.begin(), source_lbl.tag_ids.end());
                        result->labels.push_back(std::move(lbl));
                    }
                }
            }

            operation.success = true;
            operation.error.clear();
        },
        [this, result](const DataOperationWorkflow::Result &operation) { commitDatasetSplit(result, operation); }));
}

} // namespace dltool::data
