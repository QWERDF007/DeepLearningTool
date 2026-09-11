#include "ClusterWritebackService.h"

#include "common/Utils.h"
#include "data/DataViewModels.h"
#include "data/GlobalFilter.h"
#include "data/LabelData.h"
#include "database/DataBase.h"
#include "ui/SignalHelper.h"

#include <spdlog/spdlog.h>

#include <QPointer>

#include <algorithm>
#include <utility>

namespace dltool::data {

ClusterWritebackService::ClusterWritebackService(DataManagerServices services) : s_(std::move(services)) {}

ClusterWritebackService::~ClusterWritebackService() = default;

bool ClusterWritebackService::writebackClusterAsync(const ClusterWritebackRequest &request,
                                        QObject *callback_context,
                                        ClusterWritebackCompletion completion)
{
    if (s_.isShuttingDown())
        return false;

    if (s_.is_data_operation_running())
    {
        ui::SignalHelper::notifyWarn(QStringLiteral("聚类写回"), QStringLiteral("当前已有数据操作正在进行中"));
        return false;
    }
    if (s_.datasets == nullptr || s_.image_source == nullptr || s_.label_source == nullptr || s_.database == nullptr)
    {
        return false;
    }
    if (s_.is_labels_loading())
    {
        spdlog::warn("聚类写回失败, 标注正在加载中");
        ui::SignalHelper::notifyWarn(QStringLiteral("聚类写回"), QStringLiteral("标注正在加载，请稍后再试"));
        return false;
    }
    if (request.targets.empty())
    {
        return false;
    }

    std::vector<database::ProjectDataBase::ClusterTarget> db_targets;
    db_targets.reserve(request.targets.size());

    for (const auto &t : request.targets)
    {
        database::ProjectDataBase::ClusterTarget db_target;
        db_target.target_dataset_name = t.target_dataset_name;

        if (request.is_copy)
        {
            db_target.copy_images.reserve(t.image_ids.size());
            for (const int64_t image_id : t.image_ids)
            {
                const ImageInstance *source_image = s_.image_source->getImageInstance(image_id);
                if (source_image == nullptr || source_image->path().isEmpty())
                {
                    spdlog::warn("聚类写回复制失败, 源图像不存在或路径无效: {}", image_id);
                    return false;
                }

                database::ProjectDataBase::ImageSnapshot image;
                image.path       = source_image->path();
                image.extra_data = ImageInstancesListModel::extraDataForImageLabelClassId(source_image->imageLabelClassId());
                const auto image_tags = source_image->tagIds();
                image.tag_ids.assign(image_tags.begin(), image_tags.end());
                image.labels.reserve(source_image->labelIds().size());
                for (const int64_t source_label_id : source_image->labelIds())
                {
                    const LabelInstance *source_label = s_.label_source->getLabelInstance(source_label_id);
                    if (source_label == nullptr || source_label->data() == nullptr)
                    {
                        spdlog::warn("聚类写回复制失败, 源标注不存在或数据无效: {}", source_label_id);
                        return false;
                    }

                    database::ProjectDataBase::LabelSnapshot label;
                    label.label_class_id = source_label->labelClassId();
                    label.label_type     = source_label->data()->type();
                    label.data           = source_label->data()->toBlob();
                    const auto label_tags = source_label->tagIds();
                    label.tag_ids.assign(label_tags.begin(), label_tags.end());
                    image.labels.push_back(std::move(label));
                }
                db_target.copy_images.push_back(std::move(image));
            }
        }
        else
        {
            db_target.move_image_ids = t.image_ids;
        }

        db_targets.push_back(std::move(db_target));
    }

    s_.set_data_operation_running(true);
    image_operation_running_ = true;
    s_.emit_image_operation_running_changed();

    auto result = std::make_shared<ClusterWritebackResult>();
    auto safe_completion = std::make_shared<ClusterWritebackCompletion>();
    if (completion)
    {
        if (callback_context != nullptr)
        {
            const QPointer<QObject> guarded_context(callback_context);
            *safe_completion = [guarded_context, completion = std::move(completion)](const ClusterWritebackResult &res)
            {
                if (guarded_context && completion)
                    completion(res);
            };
        }
        else
        {
            *safe_completion = std::move(completion);
        }
    }

    DataOperationWorkflow::Options options;
    options.title           = QStringLiteral("聚类写回");
    options.start_message   = QStringLiteral("正在原子写入聚类目标数据集与图像");
    options.manage_progress = false;

    auto db_targets_shared = std::make_shared<std::vector<database::ProjectDataBase::ClusterTarget>>(std::move(db_targets));
    const bool is_copy = request.is_copy;
    const int label_data_method = s_.method;

    s_.track_operation(DataOperationWorkflow::startDatabase(
        s_.host, s_.database->path(), std::move(options),
        [db_targets_shared, is_copy, label_data_method, result](dltool::database::ProjectDataBase &database,
                                                                 DataOperationWorkflow::Result     &operation)
        {
            dltool::database::ProjectDataBase::AtomicClusterOutput output;
            if (!database.applyClusterAtomic(*db_targets_shared, output, operation.error,
                                             [&operation]() { return operation.cancellationRequested(); }))
            {
                operation.success = false;
                result->success   = false;
                result->error     = operation.error;
                if (operation.error == QStringLiteral("操作已取消"))
                {
                    operation.cancelled = true;
                    result->cancelled   = true;
                }
                return;
            }

            if (is_copy)
            {
                LabelDataHelper helper = data::createLabelDataHelper(label_data_method);
                if (helper == nullptr)
                {
                    operation.success = false;
                    operation.error   = QStringLiteral("标签数据工厂未初始化");
                    result->success   = false;
                    result->error     = operation.error;
                    return;
                }

                size_t img_idx   = 0;
                size_t label_idx = 0;
                for (const auto &target : *db_targets_shared)
                {
                    const int64_t target_dataset_id = output.dataset_ids_by_name[target.target_dataset_name];
                    for (const auto &source_img : target.copy_images)
                    {
                        const int64_t new_img_id = output.new_image_ids[img_idx++];
                        LoadedImageInstance img;
                        img.image_id       = new_img_id;
                        img.dataset_id     = target_dataset_id;
                        img.path           = source_img.path;
                        img.label_class_id = ImageInstancesListModel::imageLabelClassIdFromExtraData(source_img.extra_data);
                        img.tag_ids.insert(source_img.tag_ids.begin(), source_img.tag_ids.end());
                        result->copied_images.push_back(std::move(img));

                        for (const auto &source_lbl : source_img.labels)
                        {
                            const int64_t new_lbl_id = output.new_label_ids[label_idx++];
                            LabelData label_data = helper->createLabelData();
                            if (label_data != nullptr)
                            {
                                label_data->fromBlob(source_lbl.data);
                                LoadedLabelInstance lbl;
                                lbl.label_id       = new_lbl_id;
                                lbl.image_id       = new_img_id;
                                lbl.label_class_id = source_lbl.label_class_id;
                                lbl.data           = std::move(label_data);
                                lbl.tag_ids.insert(source_lbl.tag_ids.begin(), source_lbl.tag_ids.end());
                                result->copied_labels.push_back(std::move(lbl));
                            }
                        }
                    }
                }
            }

            operation.success            = true;
            result->success              = true;
            result->moved_image_count    = output.moved_image_count;
            result->copied_image_count   = output.copied_image_count;
            result->created_dataset_ids  = output.created_dataset_ids;
            result->dataset_ids_by_name  = output.dataset_ids_by_name;
            result->target_dataset_count = output.dataset_ids_by_name.size();
        },
        [this, is_copy, db_targets_shared, result, safe_completion](const DataOperationWorkflow::Result &operation)
        {
            if (s_.isShuttingDown())
                return;

            if (result->success)
            {
                if (s_.is_labels_loading())
                {
                    s_.mark_labels_changed_during_loading();
                }

                const bool filter_active = s_.global_filter != nullptr && s_.global_filter->isActive();
                s_.image_instances->beginBulkUpdate();
                s_.label_instances->beginBulkUpdate();

                std::vector<int64_t> new_dataset_ids;
                std::vector<QString> new_dataset_names;
                for (const auto &[name, id] : result->dataset_ids_by_name)
                {
                    if (std::find(result->created_dataset_ids.begin(), result->created_dataset_ids.end(), id)
                        != result->created_dataset_ids.end())
                    {
                        new_dataset_ids.push_back(id);
                        new_dataset_names.push_back(name);
                    }
                }
                if (!new_dataset_ids.empty())
                {
                    s_.datasets->addDatasetsFromMemory(new_dataset_ids, new_dataset_names);
                }

                if (!is_copy)
                {
                    for (const auto &target : *db_targets_shared)
                    {
                        const auto it = result->dataset_ids_by_name.find(target.target_dataset_name);
                        if (it != result->dataset_ids_by_name.end())
                        {
                            const int64_t target_id = it->second;
                            s_.datasets->moveImagesFromSource(s_.image_source, target.move_image_ids, target_id);
                            s_.image_source->updateImagesDatasetFromMemory(target.move_image_ids, target_id, !filter_active);
                        }
                    }
                }
                else
                {
                    if (!result->copied_images.empty())
                    {
                        s_.image_source->addImagesFromMemory(result->copied_images, true);
                        s_.label_source->addLabelsFromMemory(result->copied_labels, true);
                        s_.image_source->syncAllLabelRelations(s_.label_source, false);
                        s_.image_tags->addRelationsFromMemory(result->copied_images, result->copied_labels);
                        s_.datasets->addImagesFromSource(s_.image_source, result->copied_images);
                        s_.image_source->refreshModelFromMemory();
                        s_.label_source->refreshModelFromMemory();
                    }
                }

                if (filter_active)
                {
                    s_.global_filter->refresh();
                }
                s_.label_instances->endBulkUpdate();
                s_.image_instances->endBulkUpdate();
            }

            if (image_operation_running_)
            {
                image_operation_running_ = false;
                s_.emit_image_operation_running_changed();
            }
            s_.set_data_operation_running(false);

            if (*safe_completion)
            {
                (*safe_completion)(*result);
            }
        }));

    return true;
}

} // namespace dltool::data
