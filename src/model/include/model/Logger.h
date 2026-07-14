#pragma once

#include "dltool/model/Export.h"

#include <spdlog/spdlog.h>

namespace dltool::model {

/**
 * @brief 注册日志记录器，将其设置为 spdlog 的默认 logger
 * @param logger 要注册的 spdlog logger 实例
 * @return 注册成功返回 true，logger 为空返回 false
 */
MODEL_API bool registerLogger(std::shared_ptr<spdlog::logger> logger);

} // namespace dltool::model
