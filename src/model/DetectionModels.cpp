#include "core/CoreDef.h"
#include "model/ModelManager.h"
#include "model/ModelParamDefs.h"

#include <memory>
#include <utility>
#include <vector>

namespace dltool::model {

namespace {

constexpr int DetectionMethod = dltool::core::DeepLearningMethod::Detection;

std::vector<ParamGroupDefinition> makeDetectionTrainGroups(const int image_size, const QString &optimizer,
                                                           const bool mosaic_enabled)
{
    ParamGroupDefinition network_group;
    network_group.key        = QStringLiteral("network");
    network_group.label      = QStringLiteral("Network Parameters");
    network_group.part_index = 0;
    network_group.params     = {
        makeIntegerParam(QStringLiteral("image_size"), QStringLiteral("Image Size"), image_size, 320, 1536, 32),
        makeIntegerParam(QStringLiteral("num_classes"), QStringLiteral("Classes"), 80, 1, 10000, 1),
        makeComboParam(QStringLiteral("pretrained"), QStringLiteral("Pretrained Weights"), QStringLiteral("COCO"),
                           {QStringLiteral("None"), QStringLiteral("COCO"), QStringLiteral("Custom")}),
    };

    ParamGroupDefinition train_group;
    train_group.key        = QStringLiteral("training");
    train_group.label      = QStringLiteral("Training Parameters");
    train_group.part_index = 0;
    train_group.params     = {
        makeIntegerParam(QStringLiteral("epochs"), QStringLiteral("Epochs"), 100, 1, 10000, 1),
        makeIntegerParam(QStringLiteral("batch_size"), QStringLiteral("Batch Size"), 16, 1, 512, 1),
        makeDoubleParam(QStringLiteral("learning_rate"), QStringLiteral("Learning Rate"), 0.01, 0.000001, 1.0, 0.0001,
                            6),
        makeComboParam(QStringLiteral("optimizer"), QStringLiteral("Optimizer"), optimizer,
                           {QStringLiteral("SGD"), QStringLiteral("Adam"), QStringLiteral("AdamW")}),
    };

    ParamGroupDefinition augmentation_group;
    augmentation_group.key        = QStringLiteral("augmentation");
    augmentation_group.label      = QStringLiteral("Data Augmentation");
    augmentation_group.part_index = 1;
    augmentation_group.params     = {
        makeCheckParam(QStringLiteral("mosaic"), QStringLiteral("Mosaic"), mosaic_enabled),
        makeCheckParam(QStringLiteral("mixup"), QStringLiteral("MixUp"), false),
        makeSliderParam(QStringLiteral("hsv_gain"), QStringLiteral("HSV Gain"), 0.015, 0.0, 0.1, 0.001, 3),
        makeSliderParam(QStringLiteral("flip_probability"), QStringLiteral("Flip Probability"), 0.5, 0.0, 1.0, 0.01, 2),
    };

    return {network_group, train_group, augmentation_group};
}

std::vector<ParamGroupDefinition> makeDetectionTestGroups(const int image_size)
{
    ParamGroupDefinition inference_group;
    inference_group.key        = QStringLiteral("inference");
    inference_group.label      = QStringLiteral("Inference Parameters");
    inference_group.part_index = 0;
    inference_group.params     = {
        makeIntegerParam(QStringLiteral("image_size"), QStringLiteral("Image Size"), image_size, 320, 1536, 32),
        makeSliderParam(QStringLiteral("confidence_threshold"), QStringLiteral("Confidence Threshold"), 0.25, 0.0, 1.0,
                            0.01, 2),
        makeSliderParam(QStringLiteral("nms_threshold"), QStringLiteral("NMS Threshold"), 0.45, 0.0, 1.0, 0.01, 2),
    };

    ParamGroupDefinition evaluation_group;
    evaluation_group.key        = QStringLiteral("evaluation");
    evaluation_group.label      = QStringLiteral("Evaluation Parameters");
    evaluation_group.part_index = 1;
    evaluation_group.params     = {
        makeIntegerParam(QStringLiteral("batch_size"), QStringLiteral("Batch Size"), 8, 1, 512, 1),
        makeComboParam(QStringLiteral("metric"), QStringLiteral("Metric"), QStringLiteral("mAP@0.5"),
                           {QStringLiteral("mAP@0.5"), QStringLiteral("mAP@0.5:0.95"), QStringLiteral("Recall")}),
    };

    return {inference_group, evaluation_group};
}

class YoloV5TrainParams final : public ITrainParams
{
public:
    YoloV5TrainParams()
    {
        auto groups = makeDetectionTrainGroups(640, QStringLiteral("SGD"), true);
        for (ParamGroupDefinition &group : groups)
        {
            addGroup(group.key, group.label, std::move(group.params), group.description, group.enabled,
                     group.part_index);
        }
    }

    QString typeName() const override
    {
        return QStringLiteral("YOLOv5 Train Params");
    }

    std::unique_ptr<ITrainParams> cloneTrainParams() const override
    {
        auto cloned = std::make_unique<YoloV5TrainParams>();
        cloned->copyValuesFrom(*this);
        return cloned;
    }
};

class YoloV5TestParams final : public ITestParams
{
public:
    YoloV5TestParams()
    {
        auto groups = makeDetectionTestGroups(640);
        for (ParamGroupDefinition &group : groups)
        {
            addGroup(group.key, group.label, std::move(group.params), group.description, group.enabled,
                     group.part_index);
        }
    }

    QString typeName() const override
    {
        return QStringLiteral("YOLOv5 Test Params");
    }

    std::unique_ptr<ITestParams> cloneTestParams() const override
    {
        auto cloned = std::make_unique<YoloV5TestParams>();
        cloned->copyValuesFrom(*this);
        return cloned;
    }
};

class YoloV8TrainParams final : public ITrainParams
{
public:
    YoloV8TrainParams()
    {
        auto groups = makeDetectionTrainGroups(640, QStringLiteral("AdamW"), true);
        for (ParamGroupDefinition &group : groups)
        {
            addGroup(group.key, group.label, std::move(group.params), group.description, group.enabled,
                     group.part_index);
        }
    }

    QString typeName() const override
    {
        return QStringLiteral("YOLOv8 Train Params");
    }

    std::unique_ptr<ITrainParams> cloneTrainParams() const override
    {
        auto cloned = std::make_unique<YoloV8TrainParams>();
        cloned->copyValuesFrom(*this);
        return cloned;
    }
};

class YoloV8TestParams final : public ITestParams
{
public:
    YoloV8TestParams()
    {
        auto groups = makeDetectionTestGroups(640);
        for (ParamGroupDefinition &group : groups)
        {
            addGroup(group.key, group.label, std::move(group.params), group.description, group.enabled,
                     group.part_index);
        }
    }

    QString typeName() const override
    {
        return QStringLiteral("YOLOv8 Test Params");
    }

    std::unique_ptr<ITestParams> cloneTestParams() const override
    {
        auto cloned = std::make_unique<YoloV8TestParams>();
        cloned->copyValuesFrom(*this);
        return cloned;
    }
};

} // namespace

class YoloV5Config final : public IModelConfig
{
public:
    YoloV5Config()
        : train_params_(std::make_unique<YoloV5TrainParams>())
        , test_params_(std::make_unique<YoloV5TestParams>())
    {
        train_params_->setParent(this);
        test_params_->setParent(this);
    }

    YoloV5Config(const YoloV5Config &other)
        : train_params_(other.train_params_ ? other.train_params_->cloneTrainParams() : nullptr)
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
        return DetectionMethod;
    }

    QString typeName() const override
    {
        return QStringLiteral("YOLOv5");
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
        return std::make_unique<YoloV5Config>(*this);
    }

private:
    std::unique_ptr<ITrainParams> train_params_;
    std::unique_ptr<ITestParams>  test_params_;
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
    YoloV8Config()
        : train_params_(std::make_unique<YoloV8TrainParams>())
        , test_params_(std::make_unique<YoloV8TestParams>())
    {
        train_params_->setParent(this);
        test_params_->setParent(this);
    }

    YoloV8Config(const YoloV8Config &other)
        : train_params_(other.train_params_ ? other.train_params_->cloneTrainParams() : nullptr)
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
        return DetectionMethod;
    }

    QString typeName() const override
    {
        return QStringLiteral("YOLOv8");
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
        return std::make_unique<YoloV8Config>(*this);
    }

private:
    std::unique_ptr<ITrainParams> train_params_;
    std::unique_ptr<ITestParams>  test_params_;
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
