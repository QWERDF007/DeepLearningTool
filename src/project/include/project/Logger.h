#pragma once

#include "dltool/project/Export.h"

#include <spdlog/spdlog.h>

namespace dltool::project {

PROJECT_API bool registerLogger(std::shared_ptr<spdlog::logger> logger);

} // namespace dltool::project
