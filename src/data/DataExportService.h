#pragma once

/**
 * @file DataExportService.h
 * @brief data 模块私有：数据集批量导出用例服务。
 *
 * 独占批量导出的编排：导出准备（快照）、逐数据集串行导出、批间取消
 * 裁决、进度任务与终态通知。取消身份为服务持有的取消令牌。
 */

#include "DataManagerServices.h"

#include <atomic>
#include <memory>

namespace dltool::data {

class DataExportService
{
public:
    explicit DataExportService(DataManagerServices services);
    ~DataExportService();

    DataExportService(const DataExportService &) = delete;
    DataExportService &operator=(const DataExportService &) = delete;

    /**
     * @brief 批量导出数据集：准备快照后逐数据集串行执行。
     *
     * 任一数据集被取消或失败后不再启动下一数据集；成功/失败计数与
     * 耗时汇总通过进度消息与系统通知回报。
     */
    void exportDatasets(const std::vector<int64_t> &dataset_ids, int data_format, const QString &output_dir,
                        const QVariantMap &options);

    /// 请求取消当前批量导出；后续数据集不再启动，已提交数据集保留其结果。
    void requestCancel();

private:
    DataManagerServices               s_;
    std::shared_ptr<std::atomic_bool> active_cancel_token_;
};

} // namespace dltool::data
