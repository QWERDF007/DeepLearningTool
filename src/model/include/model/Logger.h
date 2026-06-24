#pragma once

#include "dltool/model/Export.h"

#include <spdlog/spdlog.h>

namespace dltool::model {

MODEL_API bool registerLogger(std::shared_ptr<spdlog::logger> logger);

} // namespace dltool::model
