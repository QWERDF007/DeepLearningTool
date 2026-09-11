#include "ImageTransferService.h"

#include "common/Utils.h"
#include "data/DataViewModels.h"
#include "data/DatasetSplitter.h"
#include "data/GlobalFilter.h"
#include "data/LabelData.h"
#include "database/DataBase.h"
#include "ui/SignalHelper.h"

#include <spdlog/spdlog.h>

#include <QElapsedTimer>
#include <QPointer>

#include <algorithm>
#include <set>
#include <utility>

namespace dltool::data {

namespace {

struct ImageCopyRequest
{
    int                                                           label_data_method{-1};
    int64_t                                                       dataset_id{-1};
    std::vector<dltool::database::ProjectDataBase::ImageSnapshot> images;
};

} // namespace

ImageTransferService::ImageTransferService(DataManagerServices services) : s_(std::move(services)) {}

ImageTransferService::~ImageTransferService() = default;

struct ImageTransferService::ImageCopyResult
{
    std::vector<LoadedImageInstance> images;
    std::vector<LoadedLabelInstance> labels;
    ImageOperationCompletionFn         completion;
    bool                             notify_user{true};
};

void ImageTransferService::deleteSelectedImages()
{
    if (s_.isShuttingDown())
        return;

    if (s_.is_data_operation_running())
    {
        ui::SignalHelper::notifyWarn(QString("删除图像"), QString("当前已有数据操作正在进行中"));
        return;
    }
    if (s_.database == nullptr || s_.image_source == nullptr || s_.image_instances == nullptr)
    {
        return;
    }

    const std::vector<int64_t> image_ids = s_.image_instances->getSelectedImagesId();
    if (image_ids.empty())
    {
        return;
    }

    if (s_.is_labels_loading())
    {
        s_.mark_labels_changed_during_loading();
    }
    s_.set_data_operation_running(true);
    image_operation_running_ = true;
    s_.emit_image_operation_running_changed();
    DataOperationWorkflow::Options options;
    options.title           = QString("删除图像");
    options.start_message   = QString("正在删除 %1 个图像及其标注").arg(image_ids.size());
    options.manage_progress = false;
    s_.track_operation(DataOperationWorkflow::startDatabase(
        s_.host, s_.database->path(), std::move(options),
        [image_ids](dltool::database::ProjectDataBase &database, DataOperationWorkflow::Result &result)
        { result.success = database.deleteImages(image_ids, result.error); },
        [this, image_ids](const DataOperationWorkflow::Result &result)
        { commitImageDeletion(image_ids, result.success, result.error, result.elapsed_ms); }));
}

bool ImageTransferService::copyToDatasetAsync(const std::vector<int64_t> &image_ids, const int64_t dataset_id,
                                     QObject *callback_context, ImageOperationCompletionFn completion,
                                     const bool notify_user)
{
    if (s_.isShuttingDown())
        return false;

    if (s_.is_data_operation_running())
    {
        ui::SignalHelper::notifyWarn(QString("复制图像"), QString("当前已有数据操作正在进行中"));
        return false;
    }
    if (s_.datasets == nullptr || s_.image_source == nullptr || s_.label_source == nullptr || s_.database == nullptr)
    {
        return false;
    }
    if (s_.is_labels_loading())
    {
        spdlog::warn("复制图像失败, 标注正在加载中");
        ui::SignalHelper::notifyWarn(QString("复制图像"), QString("标注正在加载，请稍后再试"));
        return false;
    }
    if (dataset_id < 0 || s_.datasets->getDatasetName(dataset_id).isEmpty())
    {
        spdlog::warn("复制图像失败, 目标数据集无效: {}", dataset_id);
        return false;
    }

    std::vector<int64_t> source_image_ids = image_ids;
    source_image_ids.erase(std::remove_if(source_image_ids.begin(), source_image_ids.end(),
                                          [](const int64_t image_id) { return image_id < 0; }),
                           source_image_ids.end());
    std::sort(source_image_ids.begin(), source_image_ids.end());
    source_image_ids.erase(std::unique(source_image_ids.begin(), source_image_ids.end()), source_image_ids.end());
    if (source_image_ids.empty())
    {
        return false;
    }

    auto request               = std::make_shared<ImageCopyRequest>();
    request->label_data_method = s_.method;
    request->dataset_id        = dataset_id;
    request->images.reserve(source_image_ids.size());
    for (const int64_t source_image_id : source_image_ids)
    {
        const ImageInstance *source_image = s_.image_source->getImageInstance(source_image_id);
        if (source_image == nullptr || source_image->path().isEmpty())
        {
            spdlog::warn("复制图像失败, 源图像不存在或路径无效: {}", source_image_id);
            return false;
        }

        dltool::database::ProjectDataBase::ImageSnapshot image;
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
                spdlog::warn("复制图像失败, 源标注不存在或数据无效: {}", source_label_id);
                return false;
            }

            dltool::database::ProjectDataBase::LabelSnapshot label;
            label.label_class_id = source_label->labelClassId();
            label.label_type     = source_label->data()->type();
            label.data           = source_label->data()->toBlob();
            const auto label_tags = source_label->tagIds();
            label.tag_ids.assign(label_tags.begin(), label_tags.end());
            image.labels.push_back(std::move(label));
        }
        request->images.push_back(std::move(image));
    }

    s_.set_data_operation_running(true);
    image_operation_running_ = true;
    s_.emit_image_operation_running_changed();
    auto result         = std::make_shared<ImageCopyResult>();
    result->notify_user = notify_user;
    if (completion)
    {
        if (callback_context != nullptr)
        {
            const QPointer<QObject> guarded_context(callback_context);
            result->completion = [guarded_context, completion = std::move(completion)](const bool     success,
                                                                                       const QString &message) mutable
            {
                if (guarded_context && completion)
                    completion(success, message);
            };
        }
        else
        {
            result->completion = std::move(completion);
        }
    }
    DataOperationWorkflow::Options options;
    options.title           = QString("复制图像");
    options.start_message   = QString("正在复制 %1 个图像及其标注").arg(request->images.size());
    options.manage_progress = false;
    s_.track_operation(DataOperationWorkflow::startDatabase(
        s_.host, s_.database->path(), std::move(options),
        [request, result](dltool::database::ProjectDataBase &database,
                          DataOperationWorkflow::Result     &operation)
        {
            dltool::database::ProjectDataBase::AtomicCopyOutput output;
            if (!database.copyImagesAtomic(request->dataset_id, request->images, output, operation.error,
                                           [&operation]() { return operation.cancellationRequested(); }))
            {
                operation.success = false;
                if (operation.error == QStringLiteral("操作已取消"))
                {
                    operation.cancelled = true;
                }
                return;
            }

            LabelDataHelper helper = data::createLabelDataHelper(request->label_data_method);
            if (helper == nullptr)
            {
                operation.success = false;
                operation.error   = QStringLiteral("标签数据工厂未初始化");
                return;
            }

            result->images.reserve(request->images.size());
            size_t label_idx = 0;
            for (size_t img_idx = 0; img_idx < request->images.size(); ++img_idx)
            {
                const auto   &source_img = request->images[img_idx];
                const int64_t new_img_id = output.image_ids[img_idx];

                LoadedImageInstance img;
                img.image_id       = new_img_id;
                img.dataset_id     = request->dataset_id;
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

            operation.success = true;
            operation.error.clear();
        },
        [this, result](const DataOperationWorkflow::Result &operation) { commitImageCopy(result, operation); }));

    return true;
}

bool ImageTransferService::moveToDatasetAsync(const std::vector<int64_t> &image_ids, const int64_t dataset_id,
                                     QObject *callback_context, ImageOperationCompletionFn completion,
                                     const bool notify_user)
{
    if (s_.isShuttingDown())
        return false;

    if (s_.is_data_operation_running())
    {
        ui::SignalHelper::notifyWarn(QString("移动图像"), QString("当前已有数据操作正在进行中"));
        return false;
    }
    if (s_.datasets == nullptr || s_.image_instances == nullptr || s_.database == nullptr)
    {
        return false;
    }
    if (dataset_id < 0 || s_.datasets->getDatasetName(dataset_id).isEmpty())
    {
        spdlog::warn("移动图像失败, 目标数据集无效: {}", dataset_id);
        return false;
    }

    std::vector<int64_t> selected_image_ids = image_ids;
    selected_image_ids.erase(std::remove_if(selected_image_ids.begin(), selected_image_ids.end(),
                                            [](const int64_t image_id) { return image_id < 0; }),
                             selected_image_ids.end());
    std::sort(selected_image_ids.begin(), selected_image_ids.end());
    selected_image_ids.erase(std::unique(selected_image_ids.begin(), selected_image_ids.end()),
                             selected_image_ids.end());
    if (selected_image_ids.empty())
    {
        return false;
    }

    std::vector<int64_t> moved_image_ids;
    moved_image_ids.reserve(selected_image_ids.size());
    for (const int64_t image_id : selected_image_ids)
    {
        const int64_t source_dataset_id = s_.image_source->getImageDatasetId(image_id);
        if (source_dataset_id < 0 || source_dataset_id == dataset_id)
        {
            continue;
        }
        moved_image_ids.push_back(image_id);
    }
    if (moved_image_ids.empty())
    {
        return false;
    }

    if (completion && callback_context != nullptr)
    {
        const QPointer<QObject> guarded_context(callback_context);
        completion
            = [guarded_context, completion = std::move(completion)](const bool success, const QString &message) mutable
        {
            if (guarded_context && completion)
                completion(success, message);
        };
    }

    s_.set_data_operation_running(true);
    image_operation_running_ = true;
    s_.emit_image_operation_running_changed();
    DataOperationWorkflow::Options options;
    options.title           = QString("移动图像");
    options.start_message   = QString("正在移动 %1 个图像").arg(moved_image_ids.size());
    options.manage_progress = false;
    s_.track_operation(DataOperationWorkflow::startDatabase(
        s_.host, s_.database->path(), std::move(options),
        [moved_image_ids, dataset_id](dltool::database::ProjectDataBase &database,
                                      DataOperationWorkflow::Result     &result)
        {
            result.success = database.moveImagesAtomic(moved_image_ids, dataset_id, result.error,
                                                       [&result]() { return result.cancellationRequested(); });
            if (!result.success && result.error == QStringLiteral("操作已取消"))
            {
                result.cancelled = true;
            }
        },
        [this, moved_image_ids, dataset_id, completion = std::move(completion),
         notify_user](const DataOperationWorkflow::Result &result) mutable
        {
            commitImageMove(moved_image_ids, dataset_id, result.success, result.error, result.elapsed_ms,
                            std::move(completion), notify_user);
        }));

    return true;
}

void ImageTransferService::commitImageDeletion(const std::vector<int64_t> &image_ids, const bool success, const QString &err_msg,
                                      const qint64 elapsed_ms)
{
    if (s_.isShuttingDown())
        return;

    if (success)
    {
        if (s_.is_labels_loading())
        {
            s_.mark_labels_changed_during_loading();
        }

        s_.image_instances->beginBulkUpdate();
        s_.label_instances->beginBulkUpdate();

        // 数据库事务已经删除图像、标注和标签关系；GUI 线程只提交内存状态。
        // 数据集统计必须在图像实体被移除前读取其归属和标注状态。
        if (s_.datasets != nullptr)
        {
            s_.datasets->removeImagesFromSource(s_.image_source, image_ids);
        }
        if (s_.label_source != nullptr)
        {
            s_.label_source->removeLabelsForImagesFromMemory(image_ids);
        }
        if (s_.image_tags != nullptr)
        {
            s_.image_tags->removeImagesTagsFromMemory(image_ids);
        }
        if (s_.image_source != nullptr)
        {
            s_.image_source->removeImagesFromMemory(image_ids);
        }
        if (s_.global_filter != nullptr && s_.global_filter->isActive())
        {
            // Most filters are already updated by the model removal.  A refresh is
            // still required for duplicate/unique-file-name conditions whose cache
            // depends on the complete image set.
            s_.global_filter->refresh();
        }
        s_.label_instances->endBulkUpdate();
        s_.image_instances->endBulkUpdate();

        const QString message = QString("已删除 %1 个图像，耗时 %2 ms").arg(image_ids.size()).arg(elapsed_ms);
        spdlog::info("{}", message.toUtf8().constData());
        ui::SignalHelper::notifySuccess(QString("删除图像完成"), message);
    }
    else
    {
        const QString message = QString("删除图像失败: %1").arg(err_msg);
        spdlog::error("{}", message.toUtf8().constData());
        ui::SignalHelper::notifyError(QString("删除图像失败"), message);
    }

    if (image_operation_running_)
    {
        image_operation_running_ = false;
        s_.emit_image_operation_running_changed();
    }
    s_.set_data_operation_running(false);
}

void ImageTransferService::commitImageMove(const std::vector<int64_t> &image_ids, const int64_t target_dataset_id,
                                  const bool success, const QString &err_msg, const qint64 elapsed_ms,
                                  ImageOperationCompletionFn completion, const bool notify_user)
{
    if (s_.isShuttingDown())
        return;

    QString message;
    if (success)
    {
        const bool filter_active = s_.global_filter != nullptr && s_.global_filter->isActive();
        s_.image_instances->beginBulkUpdate();
        s_.datasets->moveImagesFromSource(s_.image_source, image_ids, target_dataset_id);
        s_.image_source->updateImagesDatasetFromMemory(image_ids, target_dataset_id, !filter_active);
        if (filter_active)
        {
            s_.global_filter->refresh();
        }
        s_.image_instances->endBulkUpdate();

        message = QString("已移动 %1 个图像，耗时 %2 ms").arg(image_ids.size()).arg(elapsed_ms);
        if (notify_user)
        {
            spdlog::info("{}", message.toUtf8().constData());
            ui::SignalHelper::notifySuccess(QString("移动图像完成"), message);
        }
    }
    else
    {
        message = QString("移动图像失败: %1").arg(err_msg);
        spdlog::error("{}", message.toUtf8().constData());
        if (notify_user)
        {
            ui::SignalHelper::notifyError(QString("移动图像失败"), message);
        }
    }

    if (image_operation_running_)
    {
        image_operation_running_ = false;
        s_.emit_image_operation_running_changed();
    }
    s_.set_data_operation_running(false);

    if (completion)
        completion(success, success ? QString() : message);
}

void ImageTransferService::commitImageCopy(const std::shared_ptr<ImageCopyResult> &result,
                                  const DataOperationWorkflow::Result    &operation)
{
    if (s_.isShuttingDown())
        return;

    if (result != nullptr && operation.success)
    {
        QElapsedTimer model_update_timer;
        model_update_timer.start();

        if (s_.is_labels_loading())
        {
            s_.mark_labels_changed_during_loading();
        }

        // 大批量新增只在关系完整后发布一次源模型变化，避免 QML 为每行插入反复重排。
        const size_t image_count        = result->images.size();
        const size_t label_count        = result->labels.size();
        const bool   defer_model_update = image_count >= 256 || label_count >= 256;

        s_.image_instances->beginBulkUpdate();
        s_.label_instances->beginBulkUpdate();
        s_.image_source->addImagesFromMemory(result->images, defer_model_update);
        s_.label_source->addLabelsFromMemory(result->labels, defer_model_update);
        s_.image_source->syncAllLabelRelations(s_.label_source, !defer_model_update);
        s_.image_tags->addRelationsFromMemory(result->images, result->labels);
        s_.datasets->addImagesFromSource(s_.image_source, result->images);

        if (defer_model_update)
        {
            s_.image_source->refreshModelFromMemory();
            s_.label_source->refreshModelFromMemory();
        }

        if (s_.global_filter != nullptr && s_.global_filter->isActive())
        {
            s_.global_filter->refresh();
        }
        s_.label_instances->endBulkUpdate();
        s_.image_instances->endBulkUpdate();

        const qint64  model_update_elapsed_ms = model_update_timer.elapsed();
        const QString message = QString("已复制 %1 个图像、%2 个标注，数据库耗时 %3 ms，界面模型更新耗时 %4 ms")
                                    .arg(image_count)
                                    .arg(label_count)
                                    .arg(operation.elapsed_ms)
                                    .arg(model_update_elapsed_ms);
        spdlog::info("{}", message.toUtf8().constData());
        if (result->notify_user)
        {
            ui::SignalHelper::notifySuccess(QString("复制图像完成"), message);
        }
    }
    else
    {
        const QString message
            = result != nullptr ? QString("复制图像失败: %1").arg(operation.error) : QString("复制图像失败");
        spdlog::error("{}", message.toUtf8().constData());
        if (result == nullptr || result->notify_user)
        {
            ui::SignalHelper::notifyError(QString("复制图像失败"), message);
        }
    }

    if (image_operation_running_)
    {
        image_operation_running_ = false;
        s_.emit_image_operation_running_changed();
    }
    s_.set_data_operation_running(false);

    if (result != nullptr && result->completion)
    {
        const bool operation_success = operation.success;
        result->completion(operation_success,
                           operation_success
                               ? QString()
                               : (operation.error.isEmpty() ? QStringLiteral("复制图像失败") : operation.error));
    }
}

} // namespace dltool::data
