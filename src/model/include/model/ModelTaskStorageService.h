#pragma once

#include "model/ModelStorageService.h"

namespace dltool::model {

// The task-oriented name is the public architecture name; the implementation
// remains one path service so train/test callers cannot drift apart.
using ModelTaskStorageService = ModelStorageService;

} // namespace dltool::model
