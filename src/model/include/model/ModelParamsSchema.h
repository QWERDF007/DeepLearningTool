#pragma once

#include "dltool/model/Export.h"
#include "model/ModelParamDefs.h"

#include <QString>
#include <vector>

namespace dltool::model {

struct MODEL_API ModelParamsSchema
{
    QString                           model_name;
    QString                           method;
    QString                           config_path;
    std::vector<ParamGroupDefinition> train_groups;
    std::vector<ParamGroupDefinition> test_groups;
};

/// Load one model parameter schema by model name from applicationDirPath()/config/models.
/// Returns an empty schema (with a spdlog warning) if the file is not found or cannot be parsed.
MODEL_API ModelParamsSchema loadModelParamsSchema(const QString &type_name);

} // namespace dltool::model
