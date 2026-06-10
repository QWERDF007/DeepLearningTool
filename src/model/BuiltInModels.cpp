#include "model/ModelManager.h"

#include <memory>

namespace dltool::model {

namespace method {
constexpr int Classification = 0;
constexpr int Detection = 1;
constexpr int Segmentation = 2;
} // namespace method

#define DLT_DEFINE_BUILT_IN_MODEL(ClassName, DisplayName, ModelMethod)                                           \
    class ClassName##Config final : public IModelConfig                                                          \
    {                                                                                                            \
    public:                                                                                                      \
        int method() const override                                                                              \
        {                                                                                                        \
            return ModelMethod;                                                                                  \
        }                                                                                                        \
                                                                                                                 \
        QString typeName() const override                                                                        \
        {                                                                                                        \
            return QStringLiteral(DisplayName);                                                                  \
        }                                                                                                        \
                                                                                                                 \
        std::unique_ptr<IModelConfig> clone() const override                                                     \
        {                                                                                                        \
            return std::make_unique<ClassName##Config>(*this);                                                   \
        }                                                                                                        \
    };                                                                                                           \
                                                                                                                 \
    class ClassName final : public IModel                                                                        \
    {                                                                                                            \
    public:                                                                                                      \
        ClassName()                                                                                              \
            : IModel(std::make_unique<ClassName##Config>())                                                      \
        {                                                                                                        \
        }                                                                                                        \
                                                                                                                 \
        static QString staticTypeName()                                                                          \
        {                                                                                                        \
            return QStringLiteral(DisplayName);                                                                  \
        }                                                                                                        \
                                                                                                                 \
        int method() const override                                                                              \
        {                                                                                                        \
            return ModelMethod;                                                                                  \
        }                                                                                                        \
                                                                                                                 \
        QString typeName() const override                                                                        \
        {                                                                                                        \
            return staticTypeName();                                                                             \
        }                                                                                                        \
                                                                                                                 \
        std::unique_ptr<IModel> clone() const override                                                           \
        {                                                                                                        \
            return std::make_unique<ClassName>();                                                                \
        }                                                                                                        \
                                                                                                                 \
    private:                                                                                                     \
        static const bool registered_;                                                                           \
    };                                                                                                           \
                                                                                                                 \
    const bool ClassName::registered_ = ModelManager::registerModel(                                             \
        ModelMethod, ClassName::staticTypeName(), []() -> std::unique_ptr<IModel> { return std::make_unique<ClassName>(); })

DLT_DEFINE_BUILT_IN_MODEL(LeNetModel, "LeNet", method::Classification);
DLT_DEFINE_BUILT_IN_MODEL(AlexNetModel, "AlexNet", method::Classification);
DLT_DEFINE_BUILT_IN_MODEL(Vgg16Model, "VGG16", method::Classification);
DLT_DEFINE_BUILT_IN_MODEL(ResNet18Model, "ResNet18", method::Classification);
DLT_DEFINE_BUILT_IN_MODEL(ResNet50Model, "ResNet50", method::Classification);
DLT_DEFINE_BUILT_IN_MODEL(MobileNetV2Model, "MobileNetV2", method::Classification);
DLT_DEFINE_BUILT_IN_MODEL(EfficientNetB0Model, "EfficientNet-B0", method::Classification);

DLT_DEFINE_BUILT_IN_MODEL(YoloV5Model, "YOLOv5", method::Detection);
DLT_DEFINE_BUILT_IN_MODEL(RfDetrModel, "RF-DETR", method::Detection);

DLT_DEFINE_BUILT_IN_MODEL(UNetModel, "UNet", method::Segmentation);
DLT_DEFINE_BUILT_IN_MODEL(DeepLabV3Model, "DeepLabV3", method::Segmentation);
DLT_DEFINE_BUILT_IN_MODEL(SegFormerModel, "SegFormer", method::Segmentation);

#undef DLT_DEFINE_BUILT_IN_MODEL

} // namespace dltool::model
