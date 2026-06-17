#pragma once

#include "model/IModel.h"

#include <memory>

namespace dltool::model {

std::unique_ptr<IModel> createYamlModel(int method, const QString &type_name);

} // namespace dltool::model
