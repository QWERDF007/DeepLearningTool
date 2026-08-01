#pragma once

#include "dltool/model/Export.h"

#include <QString>

namespace dltool::model {

struct MODEL_API ModelStorageMigrationResult
{
    bool migrated{false};
    bool legacy_test_created{false};
    QString error;
};

/**
 * @brief 将旧模型目录一次性迁移到 train/test 任务布局。
 *
 * 迁移只在模型根目录内移动文件，目标已存在时绝不覆盖。storage.yaml 是
 * 提交标志；任一步失败都会留下 failed 状态，下次打开仍可安全重试。
 */
MODEL_API ModelStorageMigrationResult migrateModelStorage(const QString &project_dir,
                                                          const QString &model_name,
                                                          const QString &model_uuid);

} // namespace dltool::model

