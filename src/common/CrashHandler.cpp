#include "common/CrashHandler.h"

#if defined(_WIN32)
#    include "common/WindowsCCrashHandler.h"
#else
#    include "common/LinuxCCrashHandler.h"
#endif

namespace dltool::common {

void CrashHandler::setup(std::function<void()> crash_callback)
{
#if defined(_WIN32)
    WindowsCCrashHandler ccrash_handler;
#else
    LinuxCCrashHandler ccrash_handler;
#endif
    ccrash_handler.setup(crash_callback);
}

} // namespace dltool::common
