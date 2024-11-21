#pragma once

#include "CommonExport.h"

#include <spdlog/spdlog.h>

namespace dltool::common {

COMMON_API std::vector<spdlog::sink_ptr> defaultSinks();

COMMON_API std::shared_ptr<spdlog::logger> setupLogger(const std::vector<spdlog::sink_ptr> &sinks);

} // namespace dltool::common
