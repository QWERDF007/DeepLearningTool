#pragma once

#include "dltool/database/Export.h"

#include <QList>
#include <QString>
#include <QVariant>

namespace dltool::database {

/**
 * @brief A dataset selection persisted by a model/task database.
 *
 * Whole-dataset selections are stored as the explicit label-class IDs at save
 * time.  An empty list means no label classes are selected; it is never
 * interpreted as a whole-dataset selection when loading.
 */
struct DATABASE_API DatasetSelectionRecord
{
    QString      type;
    qint64       dataset_id{-1};
    QList<qint64> class_ids;
};

struct DATABASE_API ModelTestTaskRecord
{
    QString task_id;
    QString name;
    qint64  ctime{0};
    qint64  mtime{0};
};

struct DATABASE_API TaskInfoRecord
{
    QString task_id;
    qint64  ctime{0};
    qint64  mtime{0};
};

struct DATABASE_API PredictionRecord
{
    qint64   image_id{-1};
    QVariant data;
};

} // namespace dltool::database

