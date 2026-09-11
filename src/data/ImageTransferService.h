#pragma once

/**
 * @file ImageTransferService.h
 * @brief data 模块私有：图像删除/复制/移动用例服务。
 *
 * 独占图像级数据搬运的编排：快照收集、原子数据库工作流、GUI 线程内存
 * 模型提交与运行状态广播。取消身份为 DataOperationWorkflow 工作句柄；
 * 事务边界由 ProjectDataBase 的 Atomic* 工作流独占。
 */

#include "DataManagerServices.h"

#include <memory>
#include <string>
#include <vector>

namespace dltool::data {

class ImageTransferService
{
public:
    explicit ImageTransferService(DataManagerServices services);
    ~ImageTransferService();

    ImageTransferService(const ImageTransferService &) = delete;
    ImageTransferService &operator=(const ImageTransferService &) = delete;

    bool imageOperationRunning() const
    {
        return image_operation_running_;
    }

    /// 删除当前选中的图像（数据库原子删除 + 内存模型提交）。
    void deleteSelectedImages();

    /// 复制图像（含标注与标签关系）到目标数据集。
    bool copyToDatasetAsync(const std::vector<int64_t> &image_ids, int64_t dataset_id, QObject *callback_context,
                            ImageOperationCompletionFn completion, bool notify_user = true);

    /// 移动图像到目标数据集（原子改归属）。
    bool moveToDatasetAsync(const std::vector<int64_t> &image_ids, int64_t dataset_id, QObject *callback_context,
                            ImageOperationCompletionFn completion, bool notify_user = true);

private:
    struct ImageCopyResult;

    void commitImageDeletion(const std::vector<int64_t> &image_ids, bool success, const QString &err_msg,
                             qint64 elapsed_ms);
    void commitImageMove(const std::vector<int64_t> &image_ids, int64_t target_dataset_id, bool success,
                         const QString &err_msg, qint64 elapsed_ms, ImageOperationCompletionFn completion = {},
                         bool notify_user = true);
    void commitImageCopy(const std::shared_ptr<ImageCopyResult> &result, const DataOperationWorkflow::Result &operation);

    DataManagerServices s_;
    bool                image_operation_running_{false};
};

} // namespace dltool::data
