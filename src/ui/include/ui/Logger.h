#pragma once

#include "dltool/ui/Export.h"

#include <spdlog/spdlog.h>

namespace dltool::ui {

UI_API bool registerLogger(std::shared_ptr<spdlog::logger> logger);

} // namespace dltool::ui
