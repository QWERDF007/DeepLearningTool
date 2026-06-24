#pragma once

#include "dltool/settings/Export.h"

#include <spdlog/spdlog.h>

namespace dltool::settings {

SETTINGS_API bool registerLogger(std::shared_ptr<spdlog::logger> logger);

} // namespace dltool::settings
