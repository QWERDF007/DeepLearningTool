#pragma once

#include "TestFixture.h"
#include "model/IParams.h"
#include "model/ModelEvaluationViewModel.h"

#include <QPointer>
#include <QObject>

#include <memory>

namespace dltool::model::testsupport {

class QmlModelFixture : public QObject
{
    Q_OBJECT

public:
    explicit QmlModelFixture(QObject *parent = nullptr);

    Q_INVOKABLE ModelEvaluationViewModel *createAnomalyEvaluation();
    Q_INVOKABLE ModelEvaluationViewModel *createDetectionViewModel();
    Q_INVOKABLE ModelEvaluationViewModel *createSegmentationEvaluation();
    Q_INVOKABLE ITestParams *createParameterTestParams();

private:
    ModelEvaluationViewModel *createViewModel(evaluation::Method method);

    std::unique_ptr<EvaluationFixture> anomaly_fixture_;
    std::unique_ptr<EvaluationFixture> detection_fixture_;
    std::unique_ptr<EvaluationFixture> segmentation_fixture_;
    QPointer<ModelEvaluationViewModel> anomaly_evaluation_;
    QPointer<ModelEvaluationViewModel> detection_evaluation_;
    QPointer<ModelEvaluationViewModel> segmentation_evaluation_;
    std::unique_ptr<ITestParams>       parameter_test_params_;
};

} // namespace dltool::model::testsupport
