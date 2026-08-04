#include "model/YamlModel.h"

#include "model/ModelParamsSchema.h"

#include <memory>
#include <utility>
#include <vector>

namespace dltool::model {

namespace {

class YamlTrainParams final : public ITrainParams
{
public:
    YamlTrainParams(QString model_architecture, std::vector<ParamGroupDefinition> groups)
        : model_architecture_(std::move(model_architecture))
        , group_defs_(std::move(groups))
    {
        for (const ParamGroupDefinition &group : group_defs_)
        {
            addGroup(group.name_en, group.name_cn, group.params, group.description, group.enabled, group.part_index);
        }
    }

    QString typeName() const override
    {
        return model_architecture_ + QStringLiteral(" Train Params");
    }

    std::unique_ptr<ITrainParams> cloneTrainParams() const override
    {
        auto cloned = std::make_unique<YamlTrainParams>(model_architecture_, group_defs_);
        cloned->copyValuesFrom(*this);
        return cloned;
    }

private:
    QString                           model_architecture_;
    std::vector<ParamGroupDefinition> group_defs_;
};

class YamlTestParams final : public ITestParams
{
public:
    YamlTestParams(QString model_architecture, std::vector<ParamGroupDefinition> groups)
        : model_architecture_(std::move(model_architecture))
        , group_defs_(std::move(groups))
    {
        for (const ParamGroupDefinition &group : group_defs_)
        {
            addGroup(group.name_en, group.name_cn, group.params, group.description, group.enabled, group.part_index);
        }
    }

    QString typeName() const override
    {
        return model_architecture_ + QStringLiteral(" Test Params");
    }

    std::unique_ptr<ITestParams> cloneTestParams() const override
    {
        auto cloned = std::make_unique<YamlTestParams>(model_architecture_, group_defs_);
        cloned->copyValuesFrom(*this);
        return cloned;
    }

private:
    QString                           model_architecture_;
    std::vector<ParamGroupDefinition> group_defs_;
};

class YamlModelConfig final : public IModelConfig
{
public:
    YamlModelConfig(const int method, QString framework_name, QString model_architecture, ModelParamsSchema schema)
        : framework_name_(std::move(framework_name))
        , model_architecture_(std::move(model_architecture))
        , method_(method)
    {
        train_params_ = std::make_unique<YamlTrainParams>(model_architecture_, std::move(schema.train_groups));
        test_params_  = std::make_unique<YamlTestParams>(model_architecture_, std::move(schema.test_groups));
        train_params_->setParent(this);
        test_params_->setParent(this);
    }

    YamlModelConfig(const YamlModelConfig &other)
        : framework_name_(other.framework_name_)
        , model_architecture_(other.model_architecture_)
        , method_(other.method_)
        , train_params_(other.train_params_ ? other.train_params_->cloneTrainParams() : nullptr)
        , test_params_(other.test_params_ ? other.test_params_->cloneTestParams() : nullptr)
    {
        if (train_params_)
        {
            train_params_->setParent(this);
        }
        if (test_params_)
        {
            test_params_->setParent(this);
        }
    }

    int method() const override
    {
        return method_;
    }

    QString frameworkName() const override
    {
        return framework_name_;
    }

    QString modelArchitecture() const override
    {
        return model_architecture_;
    }

    QString typeName() const override
    {
        return model_architecture_;
    }

    ITrainParams *trainParams() override
    {
        return train_params_.get();
    }

    ITestParams *testParams() override
    {
        return test_params_.get();
    }

    const ITrainParams *trainParams() const override
    {
        return train_params_.get();
    }

    const ITestParams *testParams() const override
    {
        return test_params_.get();
    }

    std::unique_ptr<IModelConfig> clone() const override
    {
        return std::make_unique<YamlModelConfig>(*this);
    }

private:
    QString                       framework_name_;
    QString                       model_architecture_;
    int                           method_{-1};
    std::unique_ptr<ITrainParams> train_params_;
    std::unique_ptr<ITestParams>  test_params_;
};

class YamlModel final : public IModel
{
public:
    YamlModel(const int method, QString framework_name, QString model_architecture, ModelParamsSchema schema)
        : IModel(std::make_unique<YamlModelConfig>(method, std::move(framework_name), std::move(model_architecture),
                                                   std::move(schema)))
    {
    }

    int method() const override
    {
        const IModelConfig *model_config = config();
        return model_config != nullptr ? model_config->method() : -1;
    }

    QString frameworkName() const override
    {
        const IModelConfig *model_config = config();
        return model_config != nullptr ? model_config->frameworkName() : QString();
    }

    QString modelArchitecture() const override
    {
        const IModelConfig *model_config = config();
        return model_config != nullptr ? model_config->modelArchitecture() : QString();
    }

    QString typeName() const override
    {
        const IModelConfig *model_config = config();
        return model_config != nullptr ? model_config->typeName() : QString();
    }

    std::unique_ptr<IModel> clone() const override
    {
        return std::unique_ptr<IModel>(new YamlModel(config_ ? config_->clone() : nullptr));
    }

private:
    explicit YamlModel(std::unique_ptr<IModelConfig> config)
        : IModel(std::move(config))
    {
    }
};

} // namespace

std::unique_ptr<IModel> createYamlModel(const int method, const QString &framework_name,
                                        const QString &model_architecture)
{
    const QString trimmed_framework_name     = framework_name.trimmed();
    const QString trimmed_model_architecture = model_architecture.trimmed();
    if (trimmed_framework_name.isEmpty() || trimmed_model_architecture.isEmpty())
        return nullptr;

    ModelParamsSchema schema = loadModelParamsSchema(trimmed_framework_name, trimmed_model_architecture);
    return std::make_unique<YamlModel>(method, trimmed_framework_name, trimmed_model_architecture, std::move(schema));
}

} // namespace dltool::model
