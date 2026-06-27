#include "feature/Logger.h"

namespace dltool::feature {

/**
 * @brief 注册并设置默认的 spdlog logger
 * @param logger 要注册的 logger 实例
 * @return 注册成功返回 true，logger 为空返回 false
 */
bool registerLogger(std::shared_ptr<spdlog::logger> logger)
{
    if (logger)
    {
        spdlog::register_logger(logger);
        spdlog::set_default_logger(logger);
        return true;
    }
    return false;
}

} // namespace dltool::feature
