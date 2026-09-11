#pragma once

/**
 * @file ModelRepository.h
 * @brief data 模块私有：models 表访问仓储。
 *
 * 方法只做单表读写，事务由 ProjectDataBase 的共享上下文裁决。
 */

#include "DatabaseContext.h"

#include <QString>

#include <cstdint>
#include <vector>

namespace dltool::database {

class ModelRepository
{
public:
    static bool getAllModels(DatabaseContext &context, std::vector<int64_t> &model_ids, std::vector<QString> &uuids,
                             std::vector<QString> &names, std::vector<QString> &framework_names,
                             std::vector<QString> &model_architectures, std::vector<qint64> &ctimes,
                             std::vector<qint64> &mtimes, std::vector<std::vector<uint8_t>> &extra_data,
                             QString &err_msg);

    static bool addModel(DatabaseContext &context, const QString &uuid, const QString &name,
                         const QString &framework_name, const QString &model_architecture, const qint64 ctime,
                         const qint64 mtime, int64_t &model_id, QString &err_msg);

    static bool updateModelName(DatabaseContext &context, const int64_t model_id, const QString &name,
                                const qint64 mtime, QString &err_msg);

    static bool updateModelExtraData(DatabaseContext &context, const int64_t model_id,
                                     const std::vector<uint8_t> &extra_data, QString &err_msg);

    static bool updateModelMtime(DatabaseContext &context, const int64_t model_id, const qint64 mtime,
                                 QString &err_msg);

    static bool deleteModel(DatabaseContext &context, const int64_t model_id, QString &err_msg);
};

} // namespace dltool::database
