#pragma once

#include "IModelConfig.h"
#include "dltool/model/Export.h"

#include <QString>

#include <memory>

namespace dltool::model {

class MODEL_API IModel
{
public:
    explicit IModel(std::unique_ptr<IModelConfig> config);
    virtual ~IModel();

    IModelConfig *config();
    const IModelConfig *config() const;

    virtual int method() const = 0;
    virtual QString typeName() const = 0;
    virtual std::unique_ptr<IModel> clone() const = 0;

protected:
    std::unique_ptr<IModelConfig> config_;
};

} // namespace dltool::model
