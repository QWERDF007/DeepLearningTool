#pragma once

/**
 * @file DatasetRepository.h
 * @brief data 模块私有：datasets 表访问仓储。
 *
 * 方法只做单表读写；deleteDatasetsWithContents 之类的跨实体删除
 * 由 ProjectDataBase 的共享事务组合多仓储完成。
 */

#include "DatabaseContext.h"

#include <QString>

#include <cstdint>
#include <vector>

namespace dltool::database {

class DatasetRepository
{
public:
    static bool getAllDatasets(DatabaseContext &context, std::vector<int64_t> &dataset_ids,
                               std::vector<QString> &names, QString &err_msg);

    static bool addDataset(DatabaseContext &context, const QString &name, int64_t &dataset_id, QString &err_msg);

    static bool addDatasets(DatabaseContext &context, const std::vector<QString> &names,
                            std::vector<int64_t> &dataset_ids, QString &err_msg);

    static bool updateDataset(DatabaseContext &context, const int64_t dataset_id, const QString &name,
                              QString &err_msg);

    static bool deleteDataset(DatabaseContext &context, const int64_t dataset_id, QString &err_msg);

    /// 删除数据集及其从属图像、标注、tag 关系（调用方负责同一事务提交）。
    static bool deleteDatasetsWithContents(DatabaseContext &context, const std::vector<int64_t> &dataset_ids,
                                           QString &err_msg);
};

} // namespace dltool::database
