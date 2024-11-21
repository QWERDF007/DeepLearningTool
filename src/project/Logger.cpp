#include "project/Logger.h"

namespace dltool::project {

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
} // namespace dltool::project
