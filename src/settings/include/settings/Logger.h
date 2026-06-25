#pragma once

/**
 * @file Logger.h
 * @brief settings 模块日志注册接口声明。
 */

#include "dltool/settings/Export.h"

#include <spdlog/spdlog.h>

namespace dltool::settings {

/**
 * @brief 注册 settings 模块使用的日志器。
 * @param logger 外部创建并共享的 spdlog 日志器。
 * @return 注册成功返回 true，否则返回 false。
 */
SETTINGS_API bool registerLogger(std::shared_ptr<spdlog::logger> logger);

} // namespace dltool::settings
