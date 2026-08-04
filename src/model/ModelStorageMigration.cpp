#include "model/ModelStorageMigration.h"

namespace dltool::model {

ModelStorageMigrationResult migrateModelStorage(const QString &project_dir, const QString &model_name,
                                                const QString &model_uuid)
{
    Q_UNUSED(project_dir)
    Q_UNUSED(model_name)
    Q_UNUSED(model_uuid)
    // The storage format is intentionally destructive and has no legacy
    // migration path.  New projects create model.db/task.db directly.
    return {};
}

} // namespace dltool::model
