#pragma once

#include "model/IModel.h"

#include <memory>

namespace dltool::model {

std::unique_ptr<IModel> createYamlModel(int method, const QString &framework_name, const QString &model_architecture);

} // namespace dltool::model
