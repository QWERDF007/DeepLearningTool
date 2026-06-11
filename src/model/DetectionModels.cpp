#include "model/ModelManager.h"

#include <memory>

namespace dltool::model {

constexpr int DetectionMethod = 1;

class YoloV5Config final : public IModelConfig
{
public:
    int method() const override
    {
        return DetectionMethod;
    }

    QString typeName() const override
    {
        return QStringLiteral("YOLOv5");
    }

    std::unique_ptr<IModelConfig> clone() const override
    {
        return std::make_unique<YoloV5Config>(*this);
    }
};

class YoloV5Model final : public IModel
{
public:
    YoloV5Model()
        : IModel(std::make_unique<YoloV5Config>())
    {
    }

    static QString staticTypeName()
    {
        return QStringLiteral("YOLOv5");
    }

    int method() const override
    {
        return DetectionMethod;
    }

    QString typeName() const override
    {
        return staticTypeName();
    }

    std::unique_ptr<IModel> clone() const override
    {
        return std::make_unique<YoloV5Model>();
    }
};

class YoloV8Config final : public IModelConfig
{
public:
    int method() const override
    {
        return DetectionMethod;
    }

    QString typeName() const override
    {
        return QStringLiteral("YOLOv8");
    }

    std::unique_ptr<IModelConfig> clone() const override
    {
        return std::make_unique<YoloV8Config>(*this);
    }
};

class YoloV8Model final : public IModel
{
public:
    YoloV8Model()
        : IModel(std::make_unique<YoloV8Config>())
    {
    }

    static QString staticTypeName()
    {
        return QStringLiteral("YOLOv8");
    }

    int method() const override
    {
        return DetectionMethod;
    }

    QString typeName() const override
    {
        return staticTypeName();
    }

    std::unique_ptr<IModel> clone() const override
    {
        return std::make_unique<YoloV8Model>();
    }
};

DLT_REGISTER_MODEL(DetectionMethod, YoloV5Model);
DLT_REGISTER_MODEL(DetectionMethod, YoloV8Model);

} // namespace dltool::model
