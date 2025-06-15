#pragma once

#include "CommonExport.h"

#include <functional>

namespace dltool::common {

class COMMON_API CrashHandler
{
public:
    explicit CrashHandler() = default;
    void setup(std::function<void()> crash_callback = nullptr);

private:
};

} // namespace dltool::common
