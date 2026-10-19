#include "common/Logger.h"

#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>

#include <filesystem>

namespace dltool::common {

std::shared_ptr<spdlog::logger> setupLogger(const std::vector<spdlog::sink_ptr> &sinks)
{
    static const std::string logger_name = "dltool";

    auto logger = spdlog::get(logger_name);
    if (!logger)
    {
        if (sinks.size() > 0)
        {
            logger = std::make_shared<spdlog::logger>(logger_name, std::begin(sinks), std::end(sinks));
            spdlog::register_logger(logger);
            spdlog::set_default_logger(logger);
            spdlog::flush_every(std::chrono::seconds(1));
        }
        else
        {
            logger = spdlog::stdout_color_mt(logger_name);
        }
    }
    return logger;
}

std::vector<spdlog::sink_ptr> defaultSinks()
{
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());
    try
    {
        std::filesystem::create_directories("logs");
        sinks.push_back(std::make_shared<spdlog::sinks::daily_file_sink_st>("logs/log.txt", 23, 59));
    }
    catch (...)
    {
    }
    return sinks;
}


} // namespace dltool::common
