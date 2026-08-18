#include "model/AnomalyEvaluationEngine.h"

#include "model/EvaluationAnomalyConfusion.h"
#include "model/EvaluationCharts.h"
#include "model/EvaluationCommon.h"

#include <QVariantList>

namespace dltool::model {

AnomalyEvaluationEngine::AnomalyEvaluationEngine()
    : IEvaluationEngine()
{
}

evaluation::Method AnomalyEvaluationEngine::method() const
{
    return evaluation::Method::AnomalyDetection;
}

void AnomalyEvaluationEngine::buildClasses(const QMap<qint64, EvaluationImageData> &, QMap<int, QString> &classes)
{
    // Normal images have no label row.  Keep an explicit negative category so
    // the anomaly matrix remains rectangular even for an all-normal dataset.
    if (!classes.contains(0))
        classes.insert(0, evaluation::displayText(evaluation::DisplayText::Good));
}

bool AnomalyEvaluationEngine::computeInstanceCounts(const QMap<qint64, EvaluationImageData> &, const QMap<int, QString> &,
                                                    QMap<int, EvaluationCounts> &, EvaluationCounts &, QString *)
{
    // 异常检测无实例级指标，保持 per_class/overall 为空。
    return true;
}

bool AnomalyEvaluationEngine::runAnomalyLoop(const QMap<qint64, EvaluationImageData> &images, QString *err_msg)
{
    const auto fail = [err_msg](const QString &message)
    {
        if (err_msg)
            *err_msg = message;
        return false;
    };

    for (auto image_it = images.begin(); image_it != images.end(); ++image_it)
    {
        if (cancelled(scratch_.cancel_token))
            return fail(QString("评估已取消"));
        const EvaluationImageData &image = image_it.value();

        /**
         * @brief 异常检测按图像生成事件供 UI 统一消费。
         *
         * 没有原始实例事件的真负样本也会生成一条记录。
         */
        const EvaluationGroundTruthData *category_gt = nullptr;
        bool                             ground_truth_anomaly = false;
        for (const EvaluationGroundTruthData &gt : image.gt)
        {
            ground_truth_anomaly = ground_truth_anomaly || gt.anomaly;
            if (category_gt == nullptr || gt.label_id < 0)
                category_gt = &gt;
        }
        const EvaluationPredictionData *anomaly_prediction = nullptr;
        double                          image_score        = 0.0;
        bool                            has_image_score    = false;
        for (const EvaluationPredictionData &prediction : image.predictions)
        {
            if (!has_image_score || prediction.score > image_score)
            {
                image_score     = prediction.score;
                has_image_score = true;
            }
            if (prediction.class_id == 1 && prediction.score >= scratch_.confidence
                && (anomaly_prediction == nullptr || prediction.score > anomaly_prediction->score))
                anomaly_prediction = &prediction;
        }
        const bool               predicted_anomaly = anomaly_prediction != nullptr;
        const evaluation::Status status            = ground_truth_anomaly && predicted_anomaly
                                                         ? evaluation::Status::TruePositive
                                                         : (!ground_truth_anomaly && !predicted_anomaly
                                                                ? evaluation::Status::TrueNegative
                                                                : (predicted_anomaly ? evaluation::Status::FalsePositive
                                                                                     : evaluation::Status::FalseNegative));

        if (status == evaluation::Status::TruePositive)
            ++scratch_.image_counts.tp;
        else if (status == evaluation::Status::FalsePositive)
            ++scratch_.image_counts.fp;
        else if (status == evaluation::Status::FalseNegative)
            ++scratch_.image_counts.fn;

        EvaluationGroundTruthData display_gt = category_gt != nullptr ? *category_gt : EvaluationGroundTruthData{};
        if (category_gt == nullptr)
        {
            /**
             * @brief 正常图像没有 GT 标签，使用隐式负类展示。
             */
            display_gt.class_id   = 0;
            display_gt.class_name = evaluation::displayText(evaluation::DisplayText::Good);
            display_gt.anomaly    = false;
        }
        EvaluationPredictionData display_prediction
            = anomaly_prediction != nullptr ? *anomaly_prediction : EvaluationPredictionData{};
        display_prediction.class_id = predicted_anomaly ? 1 : 0;
        display_prediction.class_name
            = evaluation::displayText(predicted_anomaly ? evaluation::DisplayText::Anomaly
                                                        : evaluation::DisplayText::Good);
        display_prediction.score = image_score;

        const QVariantMap event_map
            = buildInstanceEvent(image, status, &display_gt, &display_prediction, 0.0, scratch_.dataset_root,
                                 scratch_.prediction_root, static_cast<qint64>(scratch_.events.size() + 1));
        scratch_.events.push_back(instanceFromMap(event_map));
    }
    return true;
}

bool AnomalyEvaluationEngine::computeImageCounts(const QMap<qint64, EvaluationImageData> &images,
                                                 EvaluationCounts &image_counts, QString *err_msg)
{
    // 单次异常循环同时产出图像级计数与事件，后续 buildEvents 从共享暂存区读取。
    if (!runAnomalyLoop(images, err_msg))
        return false;
    image_counts = scratch_.image_counts;
    return true;
}

bool AnomalyEvaluationEngine::buildEvents(const QMap<qint64, EvaluationImageData> &,
                                          QList<EvaluationInstanceRecord> &events, QString *)
{
    // 每图像一条事件（含真负样本）已在 runAnomalyLoop 中生成。
    events = scratch_.events;
    return true;
}

QList<QVariantMap>
AnomalyEvaluationEngine::buildCharts(const QMap<qint64, EvaluationImageData> &images, const QMap<int, QString> &,
                                     const EvaluationCounts &, const EvaluationCounts &image_counts,
                                     const QMap<int, EvaluationCounts> &, const QMap<QString, qint64> &,
                                     const QList<EvaluationInstanceRecord> &, QString *)
{
    // 与 assembleEvaluationResult 一致的诊断结构，异常分支只消费 Image 指标。
    const QVariantMap diagnostic = {
        {evaluation::fieldName(evaluation::Field::Instance),
         QVariantMap{{evaluation::fieldName(evaluation::Field::Overall), evaluationMetricMap(0, 0, 0)},
                     {evaluation::fieldName(evaluation::Field::PerClass), QVariantList{}}}},
        {evaluation::fieldName(evaluation::Field::Image),
         evaluationMetricMap(image_counts.tp, image_counts.fp, image_counts.fn)}
    };
    const EvaluationChartOutput official
        = buildAnomalyEvaluationCharts(images, diagnostic, scratch_.confidence);
    QList<QVariantMap> charts;
    charts.reserve(official.charts.size());
    for (const QVariant &value : official.charts)
        charts.push_back(value.toMap());
    return charts;
}

QVector<EvaluationConfusionCell>
AnomalyEvaluationEngine::buildConfusionMatrix(const QMap<int, QString> &classes, const QMap<QString, qint64> &)
{
    QList<AnomalyConfusionSample> samples;
    samples.reserve(scratch_.events.size());
    for (const EvaluationInstanceRecord &event : scratch_.events)
    {
        const bool predicted_anomaly = event.status == evaluation::Status::TruePositive
                                    || event.status == evaluation::Status::FalsePositive;
        const bool category_anomaly  = event.status == evaluation::Status::TruePositive
                                    || event.status == evaluation::Status::FalseNegative;
        samples.push_back({event.gt_class_id,
                           event.gt_class,
                           category_anomaly,
                           predicted_anomaly});
    }

    const std::vector<EvaluationConfusionCell> cells = buildAnomalyConfusionCells(samples, classes);
    return QVector<EvaluationConfusionCell>(cells.cbegin(), cells.cend());
}

bool AnomalyEvaluationEngine::hasConfusionMatrix() const
{
    return evaluation::hasConfusionMatrix(evaluation::Method::AnomalyDetection);
}

QStringList AnomalyEvaluationEngine::chartKinds() const
{
    return {evaluation::chartKindKey(evaluation::ChartKind::Line)};
}

} // namespace dltool::model
