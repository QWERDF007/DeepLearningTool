#pragma once

#include "dltool/feature/Export.h"

#include <spdlog/spdlog.h>

namespace dltool::feature {

FEATURE_API bool registerLogger(std::shared_ptr<spdlog::logger> logger);

} // namespace dltool::feature
