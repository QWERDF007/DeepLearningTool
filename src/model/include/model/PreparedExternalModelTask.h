#pragma once

#include "dltool/model/Export.h"

#include <QString>
#include <QStringList>

namespace dltool::model {

struct MODEL_API PreparedExternalModelTask
{
    int         task_id{-1};
    QString     program;
    QStringList arguments;
    QString     working_directory;
    QStringList python_paths;
    QString     log_path;
};

} // namespace dltool::model
