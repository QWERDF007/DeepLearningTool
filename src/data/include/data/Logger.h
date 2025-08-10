#pragma once

#include "DataExport.h"

#include <spdlog/spdlog.h>

namespace dltool::data {

DATA_API bool registerLogger(std::shared_ptr<spdlog::logger> logger);

} // namespace dltool::data
