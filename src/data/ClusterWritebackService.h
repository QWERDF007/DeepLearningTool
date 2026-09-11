#pragma once

/**
 * @file ClusterWritebackService.h
 * @brief data 模块私有：聚类写回用例服务。
 *
 * 独占聚类结果的批量写回编排：目标数据集快照收集、原子写库
 * （复制或移动）、内存模型提交与完成回调的上下文保护。
 * 取消身份为 DataOperationWorkflow 工作句柄；事务边界由
 * ProjectDataBase::applyClusterAtomic 独占。
 */

#include "DataManagerServices.h"

#include "data/DataManager.h"

#include <memory>

namespace dltool::data {

// DataManager 的公共嵌套类型，服务签名直接复用。
using ClusterWritebackRequest    = DataManager::ClusterWritebackRequest;
using ClusterWritebackResult     = DataManager::ClusterWritebackResult;
using ClusterWritebackCompletion = DataManager::ClusterWritebackCompletion;

class ClusterWritebackService
{
public:
    explicit ClusterWritebackService(DataManagerServices services);
    ~ClusterWritebackService();

    ClusterWritebackService(const ClusterWritebackService &) = delete;
    ClusterWritebackService &operator=(const ClusterWritebackService &) = delete;

    bool imageOperationRunning() const
    {
        return image_operation_running_;
    }

    /// 把聚类目标写回为一个或多个数据集（is_copy 决定复制或移动语义）。
    bool writebackClusterAsync(const ClusterWritebackRequest &request, QObject *callback_context,
                               ClusterWritebackCompletion completion);

private:
    DataManagerServices s_;
    bool                image_operation_running_{false};
};

} // namespace dltool::data
