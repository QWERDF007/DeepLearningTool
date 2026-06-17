#include "YamlModel.h"

#include "model/ModelParamsSchema.h"

#include <memory>
#include <utility>
#include <vector>

namespace dltool::model {

namespace {

class YamlTrainParams final : public ITrainParams
{
public:
    YamlTrainParams(QString type_name, std::vector<ParamGroupDefinition> groups)
        : type_name_(std::move(type_name))
        , group_defs_(std::move(groups))
    {
        for (const ParamGroupDefinition &group : group_defs_)
        {
            addGroup(group.key, group.label, group.params, group.description, group.enabled, group.part_index);
        }
    }

    QString typeName() const override
    {
        return type_name_ + QStringLiteral(" Train Params");
    }

    std::unique_ptr<ITrainParams> cloneTrainParams() const override
    {
        auto cloned = std::make_unique<YamlTrainParams>(type_name_, group_defs_);
        cloned->copyValuesFrom(*this);
        return cloned;
    }

private:
    QString                           type_name_;
    std::vector<ParamGroupDefinition> group_defs_;
};

class YamlTestParams final : public ITestParams
{
public:
    YamlTestParams(QString type_name, std::vector<ParamGroupDefinition> groups)
        : type_name_(std::move(type_name))
        , group_defs_(std::move(groups))
    {
        for (const ParamGroupDefinition &group : group_defs_)
        {
            addGroup(group.key, group.label, group.params, group.description, group.enabled, group.part_index);
        }
    }

    QString typeName() const override
    {
        return type_name_ + QStringLiteral(" Test Params");
    }

    std::unique_ptr<ITestParams> cloneTestParams() const override
    {
        auto cloned = std::make_unique<YamlTestParams>(type_name_, group_defs_);
        cloned->copyValuesFrom(*this);
        return cloned;
    }

private:
    QString                           type_name_;
    std::vector<ParamGroupDefinition> group_defs_;
};

class YamlModelConfig final : public IModelConfig
{
public:
    YamlModelConfig(const int method, QString type_name, ModelParamsSchema schema)
        : type_name_(std::move(type_name))
        , method_(method)
    {
        train_params_ = std::make_unique<YamlTrainParams>(type_name_, std::move(schema.train_groups));
        test_params_  = std::make_unique<YamlTestParams>(type_name_, std::move(schema.test_groups));
        train_params_->setParent(this);
        test_params_->setParent(this);
    }

    YamlModelConfig(const YamlModelConfig &other)
        : type_name_(other.type_name_)
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

    QString typeName() const override
    {
        return type_name_;
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
    QString                       type_name_;
    int                           method_{-1};
    std::unique_ptr<ITrainParams> train_params_;
    std::unique_ptr<ITestParams>  test_params_;
};

class YamlModel final : public IModel
{
public:
    YamlModel(const int method, QString type_name, ModelParamsSchema schema)
        : IModel(std::make_unique<YamlModelConfig>(method, std::move(type_name), std::move(schema)))
    {
    }

    int method() const override
    {
        const IModelConfig *model_config = config();
        return model_config != nullptr ? model_config->method() : -1;
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

std::unique_ptr<IModel> createYamlModel(const int method, const QString &type_name)
{
    const QString trimmed_type_name = type_name.trimmed();
    if (trimmed_type_name.isEmpty())
        return nullptr;

    ModelParamsSchema schema = loadModelParamsSchema(trimmed_type_name);
    return std::make_unique<YamlModel>(method, trimmed_type_name, std::move(schema));
}

} // namespace dltool::model
