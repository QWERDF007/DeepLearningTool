#pragma once

#include "dltool/model/Export.h"

#include <QString>

namespace dltool::model {

struct MODEL_API ModelStorageMigrationResult
{
    bool    migrated{false};
    bool    legacy_test_created{false};
    QString error;
};

/**
 * @brief 保留的迁移入口。
 *
 * 当前存储格式采用破坏性切换，不提供旧 YAML 存储迁移。
 */
MODEL_API ModelStorageMigrationResult migrateModelStorage(const QString &project_dir, const QString &model_name,
                                                          const QString &model_uuid);

} // namespace dltool::model
