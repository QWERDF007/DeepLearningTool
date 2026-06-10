#pragma once

#include "ModelExport.h"

#include <QString>

#include <memory>

namespace dltool::model {

class MODEL_API IModelConfig
{
public:
    virtual ~IModelConfig();

    virtual int method() const = 0;
    virtual QString typeName() const = 0;
    virtual std::unique_ptr<IModelConfig> clone() const = 0;
};

} // namespace dltool::model
