#include "QmlModelFixture.h"

#include "TestFixture.h"

#include "model/EvaluationViewModelRegistry.h"
#include "model/ModelEvaluationOptions.h"
#include "model/ModelParamDefs.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <QDir>

namespace dltool::model::testsupport {

namespace {

class ParameterTestParams final : public ITestParams
{
public:
    ParameterTestParams()
    {
        addGroup(QStringLiteral("inference"), QStringLiteral("推理参数"),
                 {makeIntegerParam(QStringLiteral("image_size"), QStringLiteral("图像大小"), 448, 1, 2048, 1)},
                 QStringLiteral("测试推理参数"));
    }

    QString typeName() const override
    {
        return QStringLiteral("Parameter test params");
    }

    std::unique_ptr<ITestParams> cloneTestParams() const override
    {
        auto cloned = std::make_unique<ParameterTestParams>();
        cloned->copyValuesFrom(*this);
        return cloned;
    }
};

} // namespace

QmlModelFixture::QmlModelFixture(QObject *parent)
    : QObject(parent)
{
}

ModelEvaluationViewModel *QmlModelFixture::createAnomalyEvaluation()
{
    if (anomaly_evaluation_ != nullptr)
        return anomaly_evaluation_;

    anomaly_fixture_ = std::make_unique<EvaluationFixture>(static_cast<int>(evaluation::Method::AnomalyDetection));
    if (!anomaly_fixture_->isValid())
        return nullptr;

    const qint64 good = anomaly_fixture_->addClass(QStringLiteral("Good"), QStringLiteral("good"));
    const qint64 anomaly = anomaly_fixture_->addClass(QStringLiteral("Scratch"), QStringLiteral("anomaly"));
    const qint64 normal_image
        = anomaly_fixture_->addImage(QStringLiteral("normal"), {{QStringLiteral("image_label_class_id"), good}});
    const qint64 bad_image
        = anomaly_fixture_->addImage(QStringLiteral("bad"), {{QStringLiteral("image_label_class_id"), anomaly}});
    const qint64 false_positive_image
        = anomaly_fixture_->addImage(QStringLiteral("false-positive"),
                                     {{QStringLiteral("image_label_class_id"), good}});
    if (good < 0 || anomaly < 0 || normal_image < 0 || bad_image < 0 || false_positive_image < 0
        || !anomaly_fixture_->writeImageList() || !anomaly_fixture_->setTestSelection({good, anomaly})
        || !anomaly_fixture_->writePrediction(normal_image, anomalyPrediction(0.2))
        || !anomaly_fixture_->writePrediction(bad_image, anomalyPrediction(0.9))
        || !anomaly_fixture_->writePrediction(false_positive_image, anomalyPrediction(0.8)))
        return nullptr;

    cv::Mat normal_score(8, 8, CV_32FC1, cv::Scalar(0.2F));
    cv::Mat anomaly_score(8, 8, CV_32FC1, cv::Scalar(0.1F));
    cv::Mat false_positive_score(8, 8, CV_32FC1, cv::Scalar(0.1F));
    anomaly_score(cv::Range(2, 6), cv::Range(2, 6)).setTo(0.9F);
    false_positive_score(cv::Range(1, 4), cv::Range(4, 7)).setTo(0.8F);
    const QString normal_score_path
        = QDir(anomaly_fixture_->predictionDirectory()).filePath(QStringLiteral("%1.tiff").arg(normal_image));
    const QString anomaly_score_path
        = QDir(anomaly_fixture_->predictionDirectory()).filePath(QStringLiteral("%1.tiff").arg(bad_image));
    const QString false_positive_score_path = QDir(anomaly_fixture_->predictionDirectory())
                                                  .filePath(QStringLiteral("%1.tiff").arg(false_positive_image));
    if (!cv::imwrite(normal_score_path.toStdString(), normal_score)
        || !cv::imwrite(anomaly_score_path.toStdString(), anomaly_score)
        || !cv::imwrite(false_positive_score_path.toStdString(), false_positive_score))
        return nullptr;

    ModelEvaluationViewModel *view_model = createViewModel(evaluation::Method::AnomalyDetection);
    if (view_model == nullptr)
        return nullptr;

    ModelEvaluationOptions options;
    options.model_uuid              = QStringLiteral("qml-model");
    options.test_task_uuid          = QStringLiteral("qml-task");
    options.model_name              = QStringLiteral("QML model");
    options.task_directory          = anomaly_fixture_->rootPath();
    options.method                  = evaluation::Method::AnomalyDetection;
    options.project_database_path   = anomaly_fixture_->projectDatabasePath();
    options.dataset_file_list_path  = anomaly_fixture_->fileListPath();
    options.task_database_path      = anomaly_fixture_->taskDatabasePath();
    options.prediction_dir          = anomaly_fixture_->predictionDirectory();
    options.confidence_threshold    = 0.5;
    options.iou_threshold           = 0.5;
    options.matching_strategy       = evaluation::MatchingStrategy::GreedyIoU;
    options.preprocessing_config    = {
        {QStringLiteral("network"),
         QVariantMap{{QStringLiteral("image_size"), 32}, {QStringLiteral("center_crop_size"), 32}}}
    };
    view_model->setEvaluationOptions(options);
    view_model->evaluate(false);
    anomaly_evaluation_ = view_model;
    return view_model;
}

ModelEvaluationViewModel *QmlModelFixture::createDetectionViewModel()
{
    if (detection_evaluation_ != nullptr)
        return detection_evaluation_;

    detection_fixture_ = std::make_unique<EvaluationFixture>(static_cast<int>(evaluation::Method::Detection));
    if (!detection_fixture_->isValid())
        return nullptr;
    const qint64 cat       = detection_fixture_->addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
    const qint64 image     = detection_fixture_->addImage(QStringLiteral("detection"));
    const qint64 fn_image  = detection_fixture_->addImage(QStringLiteral("detection-fn"));
    const qint64 fp_image  = detection_fixture_->addImage(QStringLiteral("detection-fp"));
    if (cat < 0 || image < 0 || fn_image < 0 || fp_image < 0
        || detection_fixture_->addDetectionLabel(image, cat, 0, 0, 10, 10) < 0
        || detection_fixture_->addDetectionLabel(fn_image, cat, 0, 0, 10, 10) < 0
        || !detection_fixture_->writeImageList() || !detection_fixture_->setTestSelection({cat})
        || !detection_fixture_->writePrediction(
            image, detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.9, 0, 0, 10, 10))
        || !detection_fixture_->writePrediction(
            fp_image, detectionPrediction(static_cast<int>(cat), QStringLiteral("Cat"), 0.8, 0, 0, 10, 10)))
        return nullptr;

    detection_evaluation_ = createViewModel(evaluation::Method::Detection);
    if (detection_evaluation_ == nullptr)
        return nullptr;

    ModelEvaluationOptions options;
    options.model_uuid              = QStringLiteral("qml-detection-model");
    options.test_task_uuid          = QStringLiteral("qml-detection-task");
    options.model_name              = QStringLiteral("QML detection");
    options.task_directory          = detection_fixture_->rootPath();
    options.method                  = evaluation::Method::Detection;
    options.project_database_path   = detection_fixture_->projectDatabasePath();
    options.dataset_file_list_path  = detection_fixture_->fileListPath();
    options.task_database_path      = detection_fixture_->taskDatabasePath();
    options.prediction_dir          = detection_fixture_->predictionDirectory();
    options.confidence_threshold    = 0.5;
    options.iou_threshold           = 0.5;
    options.matching_strategy       = evaluation::MatchingStrategy::GreedyIoU;
    detection_evaluation_->setEvaluationOptions(options);
    detection_evaluation_->evaluate(false);
    return detection_evaluation_;
}

ModelEvaluationViewModel *QmlModelFixture::createSegmentationEvaluation()
{
    if (segmentation_evaluation_ != nullptr)
        return segmentation_evaluation_;

    segmentation_fixture_ = std::make_unique<EvaluationFixture>(static_cast<int>(evaluation::Method::Segmentation));
    if (!segmentation_fixture_->isValid())
        return nullptr;
    const qint64 object = segmentation_fixture_->addClass(QStringLiteral("Object"), QStringLiteral("normal"));
    const qint64 image  = segmentation_fixture_->addImage(QStringLiteral("segmentation"));
    if (object < 0 || image < 0
        || segmentation_fixture_->addSegmentationLabel(
               image, object, {QPointF(2, 2), QPointF(14, 2), QPointF(14, 14), QPointF(2, 14)})
               < 0
        || !segmentation_fixture_->writeImageList() || !segmentation_fixture_->setTestSelection({object})
        || !segmentation_fixture_->writePrediction(
            image, detectionPrediction(static_cast<int>(object), QStringLiteral("Object"), 0.9, 2, 2, 12, 12)))
        return nullptr;

    segmentation_evaluation_ = createViewModel(evaluation::Method::Segmentation);
    if (segmentation_evaluation_ == nullptr)
        return nullptr;
    ModelEvaluationOptions options;
    options.model_uuid              = QStringLiteral("qml-segmentation-model");
    options.test_task_uuid          = QStringLiteral("qml-segmentation-task");
    options.model_name              = QStringLiteral("QML segmentation");
    options.task_directory          = segmentation_fixture_->rootPath();
    options.method                  = evaluation::Method::Segmentation;
    options.project_database_path   = segmentation_fixture_->projectDatabasePath();
    options.dataset_file_list_path  = segmentation_fixture_->fileListPath();
    options.task_database_path      = segmentation_fixture_->taskDatabasePath();
    options.prediction_dir          = segmentation_fixture_->predictionDirectory();
    options.confidence_threshold    = 0.5;
    options.iou_threshold           = 0.5;
    options.matching_strategy       = evaluation::MatchingStrategy::GreedyIoU;
    segmentation_evaluation_->setEvaluationOptions(options);
    segmentation_evaluation_->evaluate(false);
    return segmentation_evaluation_;
}

ITestParams *QmlModelFixture::createParameterTestParams()
{
    if (!parameter_test_params_)
        parameter_test_params_ = std::make_unique<ParameterTestParams>();
    return parameter_test_params_.get();
}

ModelEvaluationViewModel *QmlModelFixture::createViewModel(const evaluation::Method method)
{
    return EvaluationViewModelRegistry::instance().createViewModel(method, this);
}

} // namespace dltool::model::testsupport
