#pragma once

#include "dltool/feature/Export.h"

#include <spdlog/spdlog.h>

namespace dltool::feature {

/**
 * @brief 注册并设置默认的 spdlog logger
 * @param logger 要注册的 logger 实例
 * @return 注册成功返回 true，logger 为空返回 false
 */
FEATURE_API bool registerLogger(std::shared_ptr<spdlog::logger> logger);

} // namespace dltool::feature
