#include "model/IModel.h"

namespace dltool::model {

IModelConfig::~IModelConfig() = default;

IModel::IModel(std::unique_ptr<IModelConfig> config)
    : config_(std::move(config))
{
}

IModel::~IModel() = default;

IModelConfig *IModel::config()
{
    return config_.get();
}

const IModelConfig *IModel::config() const
{
    return config_.get();
}

} // namespace dltool::model
