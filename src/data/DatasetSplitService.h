#pragma once

/**
 * @file DatasetSplitService.h
 * @brief data 模块私有：数据集划分用例服务。
 *
 * 独占划分编排：比例校验、确定性分配、目标数据集命名与原子复制
 *工作流、内存模型提交。取消身份为 DataOperationWorkflow 工作句柄；
 *事务边界由 ProjectDataBase::splitDatasetAtomic 独占。
 */

#include "DataManagerServices.h"

#include <memory>
#include <vector>

namespace dltool::data {

class DatasetSplitService
{
public:
    explicit DatasetSplitService(DataManagerServices services);
    ~DatasetSplitService();

    DatasetSplitService(const DatasetSplitService &) = delete;
    DatasetSplitService &operator=(const DatasetSplitService &) = delete;

    /// 划分数据集为 Train/Val/Test 子数据集；结果经 datasetSplitFinished 通知。
    void splitDataset(int64_t dataset_id, double train_ratio, double validation_ratio, double test_ratio,
                      bool use_validation);

private:
    struct DatasetSplitCopyResult;

    void commitDatasetSplit(const std::shared_ptr<DatasetSplitCopyResult> &result,
                            const DataOperationWorkflow::Result &operation);

    DataManagerServices s_;
};

} // namespace dltool::data
