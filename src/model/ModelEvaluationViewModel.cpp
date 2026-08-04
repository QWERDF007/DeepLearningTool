#include "model/ModelEvaluationViewModel.h"
#include "model/ModelEvaluationService.h"
#include "model/ModelEvaluationProtocol.h"
#include "ui/SignalHelper.h"

#include <spdlog/spdlog.h>

#include <QDateTime>
#include <QFileInfo>
#include <QVariantList>
#include <QMap>
#include <QMetaMethod>
#include <QSet>
#include <QMetaObject>
#include <QThreadPool>
#include <QPointer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace dltool::model {

namespace {

QString textValue(const QVariantMap &map, const QString &name, const QString &fallback = {})
{
    const QString value = map.value(name).toString();
    return value.isEmpty() ? fallback : value;
}

QString statusDisplayText(const evaluation::Status status)
{
    return evaluation::statusDisplayName(status);
}

QString classColor(const int class_id)
{
    static const QStringList palette = {QStringLiteral("#ef5350"), QStringLiteral("#42a5f5"),
                                        QStringLiteral("#66bb6a"), QStringLiteral("#ffa726"),
                                        QStringLiteral("#ab47bc"), QStringLiteral("#26c6da"),
                                        QStringLiteral("#8d6e63"), QStringLiteral("#78909c")};
    const int index = class_id >= 0 ? class_id % palette.size() : 0;
    return palette.at(index);
}

int intValue(const QVariantMap &map, const QString &name, const int fallback = -1)
{
    bool ok = false;
    const int value = map.value(name).toInt(&ok);
    return ok ? value : fallback;
}

qint64 longValue(const QVariantMap &map, const QString &name, const qint64 fallback = 0)
{
    bool ok = false;
    const qint64 value = map.value(name).toLongLong(&ok);
    return ok ? value : fallback;
}

double realValue(const QVariantMap &map, const QString &name, const double fallback = 0.0)
{
    bool ok = false;
    const double value = map.value(name).toDouble(&ok);
    return ok ? value : fallback;
}

QString textValue(const QVariantMap &map, const evaluation::Field field, const QString &fallback = {})
{
    return textValue(map, evaluation::fieldName(field), fallback);
}

int intValue(const QVariantMap &map, const evaluation::Field field, const int fallback = -1)
{
    return intValue(map, evaluation::fieldName(field), fallback);
}

qint64 longValue(const QVariantMap &map, const evaluation::Field field, const qint64 fallback = 0)
{
    return longValue(map, evaluation::fieldName(field), fallback);
}

double realValue(const QVariantMap &map, const evaluation::Field field, const double fallback = 0.0)
{
    return realValue(map, evaluation::fieldName(field), fallback);
}

bool hasInvokable(QObject *object, const char *method, const int parameter_count)
{
    if (object == nullptr)
        return false;
    const QMetaObject *meta_object = object->metaObject();
    for (int index = 0; index < meta_object->methodCount(); ++index)
    {
        const QMetaMethod meta_method = meta_object->method(index);
        if (meta_method.name() == method && meta_method.parameterCount() == parameter_count)
            return true;
    }
    return false;
}

EvaluationMetricRecord metricFromMap(const QString &key, const QVariantMap &map, const QString &fallback_label = {})
{
    EvaluationMetricRecord metric;
    metric.key = key;
    metric.label = fallback_label.isEmpty() ? key : fallback_label;
    metric.class_name = textValue(map, evaluation::Field::ClassName);
    metric.class_id = intValue(map, evaluation::Field::ClassId);
    metric.precision = realValue(map, evaluation::Field::Precision);
    metric.recall = realValue(map, evaluation::Field::Recall);
    metric.f1 = realValue(map, evaluation::Field::F1);
    metric.tp = longValue(map, evaluation::Field::Tp);
    metric.fp = longValue(map, evaluation::Field::Fp);
    metric.fn = longValue(map, evaluation::Field::Fn);
    metric.precision_defined = map.contains(evaluation::fieldName(evaluation::Field::PrecisionDefined))
        ? map.value(evaluation::fieldName(evaluation::Field::PrecisionDefined)).toBool() : metric.tp + metric.fp > 0;
    metric.recall_defined = map.contains(evaluation::fieldName(evaluation::Field::RecallDefined))
        ? map.value(evaluation::fieldName(evaluation::Field::RecallDefined)).toBool() : metric.tp + metric.fn > 0;
    metric.f1_defined = map.contains(evaluation::fieldName(evaluation::Field::F1Defined))
        ? map.value(evaluation::fieldName(evaluation::Field::F1Defined)).toBool() : metric.precision_defined && metric.recall_defined;
    return metric;
}

EvaluationInstanceRecord instanceFromMap(const QVariantMap &map)
{
    EvaluationInstanceRecord record;
    record.event_uuid = textValue(map, evaluation::Field::EventUuid);
    record.image_id = longValue(map, evaluation::Field::ImageId, -1);
    record.dataset_id = longValue(map, evaluation::Field::DatasetId, -1);
    record.image_name = textValue(map, evaluation::Field::ImageName);
    record.image_path = textValue(map, evaluation::Field::ImagePath);
    record.image_width = intValue(map, evaluation::Field::ImageWidth, 0);
    record.image_height = intValue(map, evaluation::Field::ImageHeight, 0);
    record.status = evaluation::statusFromKey(textValue(map, evaluation::Field::Status));
    record.score = realValue(map, evaluation::Field::Score);
    record.iou = realValue(map, evaluation::Field::Iou);
    record.gt_label_id = longValue(map, evaluation::Field::GtLabelId, -1);
    record.gt_instance_id = record.gt_label_id >= 0 ? QString::number(record.gt_label_id) : QString();
    record.pred_instance_id = textValue(map, evaluation::Field::PredInstanceId);
    record.gt_class_id = intValue(map, evaluation::Field::GtClassId);
    record.pred_class_id = intValue(map, evaluation::Field::PredClassId);
    record.gt_class = textValue(map, evaluation::Field::GtClassName);
    record.pred_class = textValue(map, evaluation::Field::PredClassName);
    record.gt_geometry = map.value(evaluation::fieldName(evaluation::Field::GtGeometry)).toMap();
    record.pred_geometry = map.value(evaluation::fieldName(evaluation::Field::PredGeometry)).toMap();
    record.gt_bounds = record.gt_geometry.value(evaluation::fieldName(evaluation::Field::Bounds)).toMap();
    record.pred_bounds = record.pred_geometry.value(evaluation::fieldName(evaluation::Field::Bounds)).toMap();
    record.crop_bounds = map.value(evaluation::fieldName(evaluation::Field::CropBounds)).toMap();
    record.gt_overlay_bounds = map.value(evaluation::fieldName(evaluation::Field::GtOverlayBounds)).toMap();
    record.pred_overlay_bounds = map.value(evaluation::fieldName(evaluation::Field::PredOverlayBounds)).toMap();
    record.gt_overlay_points = map.value(evaluation::fieldName(evaluation::Field::GtOverlayPoints)).toList();
    record.pred_overlay_points = map.value(evaluation::fieldName(evaluation::Field::PredOverlayPoints)).toList();
    record.gt_mask_url = textValue(map, evaluation::Field::GtMaskUrl);
    record.pred_mask_url = textValue(map, evaluation::Field::PredMaskUrl);
    if (record.image_name.isEmpty())
        record.image_name = QFileInfo(record.image_path).fileName();
    return record;
}

EvaluationImageRecord imageFromMap(const QVariantMap &map)
{
    EvaluationImageRecord record;
    record.image_id = longValue(map, evaluation::Field::ImageId, -1);
    record.dataset_id = longValue(map, evaluation::Field::DatasetId, -1);
    record.image_name = textValue(map, evaluation::Field::ImageName);
    record.image_path = textValue(map, evaluation::Field::ImagePath);
    record.image_width = intValue(map, evaluation::Field::ImageWidth, 0);
    record.image_height = intValue(map, evaluation::Field::ImageHeight, 0);
    for (const QVariant &value : map.value(evaluation::fieldName(evaluation::Field::GtInstances)).toList())
    {
        const QVariantMap item = value.toMap();
        EvaluationGroundTruthRecord gt;
        gt.label_id = longValue(item, evaluation::Field::LabelId, -1);
        gt.class_id = intValue(item, evaluation::Field::ClassId, -1);
        gt.class_name = textValue(item, evaluation::Field::ClassName);
        gt.geometry = item.value(evaluation::fieldName(evaluation::Field::Geometry)).toMap();
        record.gt_instances.push_back(std::move(gt));
    }
    for (const QVariant &value : map.value(evaluation::fieldName(evaluation::Field::Predictions)).toList())
    {
        const QVariantMap item = value.toMap();
        EvaluationPredictionRecord prediction;
        prediction.prediction_id = textValue(item, evaluation::Field::PredictionId);
        prediction.class_id = intValue(item, evaluation::Field::ClassId, -1);
        prediction.class_name = textValue(item, evaluation::Field::ClassName);
        prediction.score = realValue(item, evaluation::Field::Score);
        prediction.geometry = item.value(evaluation::fieldName(evaluation::Field::Geometry)).toMap();
        record.predictions.push_back(std::move(prediction));
    }
    if (record.image_name.isEmpty())
        record.image_name = QFileInfo(record.image_path).fileName();
    return record;
}

const QList<int> &gtClassIds(const EvaluationImageRecord &record)
{
    return record.gt_class_ids;
}

QList<int> predClassIds(const EvaluationImageRecord &record, const double threshold)
{
    QList<int> ids;
    for (const EvaluationPredictionRecord &prediction : record.predictions)
        if (prediction.class_id >= 0 && prediction.score >= threshold && !ids.contains(prediction.class_id))
            ids.push_back(prediction.class_id);
    return ids;
}

bool hasPredictions(const EvaluationImageRecord &record, const double threshold)
{
    return std::any_of(record.predictions.cbegin(), record.predictions.cend(),
                       [threshold](const EvaluationPredictionRecord &prediction)
                       { return prediction.score >= threshold; });
}

bool hasGroundTruth(const EvaluationImageRecord &record)
{
    return record.has_gt;
}

bool isAnomalyImage(const EvaluationImageRecord &record, const double threshold, const bool predicted)
{
    const QList<int> classes = predicted ? predClassIds(record, threshold) : gtClassIds(record);
    return classes.contains(1);
}

double imageScore(const EvaluationImageRecord &record)
{
    return record.max_prediction_score;
}

struct ScoreHistogramData
{
    QVariantList labels;
    QVariantList good_points;
    QVariantList anomaly_points;
    std::vector<double> centers;
    int max_count{0};
    double min_score{0.0};
    double max_score{1.0};
};

ScoreHistogramData scoreHistogram(const QVariantList &good_scores, const QVariantList &anomaly_scores)
{
    ScoreHistogramData histogram;
    std::vector<double> good_values;
    std::vector<double> anomaly_values;
    const auto collect = [](const QVariantList &source, std::vector<double> &target)
    {
        for (const QVariant &value : source)
        {
            bool ok = false;
            const double score = value.toDouble(&ok);
            if (ok && std::isfinite(score))
                target.push_back(score);
        }
    };
    collect(good_scores, good_values);
    collect(anomaly_scores, anomaly_values);

    std::vector<double> all_values;
    all_values.reserve(good_values.size() + anomaly_values.size());
    all_values.insert(all_values.end(), good_values.cbegin(), good_values.cend());
    all_values.insert(all_values.end(), anomaly_values.cbegin(), anomaly_values.cend());
    if (all_values.empty())
        return histogram;

    const auto minmax = std::minmax_element(all_values.cbegin(), all_values.cend());
    histogram.min_score = *minmax.first;
    histogram.max_score = *minmax.second;
    constexpr int bin_count = 24;
    int actual_bin_count = bin_count;
    if (std::abs(histogram.max_score - histogram.min_score) <= 1e-12)
    {
        const double padding = std::max(0.01, std::abs(histogram.min_score) * 0.05);
        histogram.min_score -= padding;
        histogram.max_score += padding;
        actual_bin_count = 1;
    }
    const double bin_width = (histogram.max_score - histogram.min_score) / actual_bin_count;
    std::vector<int> good_counts(static_cast<std::size_t>(actual_bin_count), 0);
    std::vector<int> anomaly_counts(static_cast<std::size_t>(actual_bin_count), 0);
    histogram.centers.reserve(actual_bin_count);
    histogram.labels.reserve(actual_bin_count);
    for (int index = 0; index < actual_bin_count; ++index)
    {
        const double center = histogram.min_score + (static_cast<double>(index) + 0.5) * bin_width;
        histogram.centers.push_back(center);
        histogram.labels.push_back(QString::number(center, 'f', 4));
    }

    const auto addToBins = [&](const std::vector<double> &values, std::vector<int> &counts)
    {
        for (const double score : values)
        {
            const int index = std::clamp(
                static_cast<int>(std::floor((score - histogram.min_score) / bin_width)), 0, actual_bin_count - 1);
            ++counts.at(static_cast<std::size_t>(index));
        }
    };
    addToBins(good_values, good_counts);
    addToBins(anomaly_values, anomaly_counts);

    const auto makePoints = [&histogram](const std::vector<int> &counts)
    {
        QVariantList points;
        points.reserve(static_cast<int>(counts.size()));
        for (std::size_t index = 0; index < counts.size(); ++index)
        {
            // A zero-count bin is a gap, not a point on the x axis.  Using an
            // invalid QVariant becomes null at the QML/JSON boundary, which
            // makes Chart.js break the line instead of drawing a zero baseline
            // through empty ranges.
            const QVariant y = counts.at(index) > 0 ? QVariant(counts.at(index)) : QVariant();
            points.push_back(QVariantMap{{QStringLiteral("x"), histogram.centers.at(index)},
                                         {QStringLiteral("y"), y}});
        }
        return points;
    };
    histogram.good_points = makePoints(good_counts);
    histogram.anomaly_points = makePoints(anomaly_counts);
    histogram.max_count = std::max(*std::max_element(good_counts.cbegin(), good_counts.cend()),
                                   *std::max_element(anomaly_counts.cbegin(), anomaly_counts.cend()));
    return histogram;
}

QVariantMap anomalyScoreChart(const QVariantList &good_scores, const QVariantList &anomaly_scores,
                              const bool has_good, const double good_max, const bool has_anomaly,
                              const double anomaly_min)
{
    constexpr const char *good_color = "#43A047";
    constexpr const char *good_fill = "rgba(67, 160, 71, 0.24)";
    constexpr const char *anomaly_color = "#E53935";
    constexpr const char *anomaly_fill = "rgba(229, 57, 53, 0.24)";
    ScoreHistogramData histogram = scoreHistogram(good_scores, anomaly_scores);
    const auto alignBoundaryPoint = [](QVariantList &points, const double score, const bool last)
    {
        if (!std::isfinite(score))
            return;

        const int start = last ? points.size() - 1 : 0;
        const int end = last ? -1 : points.size();
        const int step = last ? -1 : 1;
        for (int index = start; index != end; index += step)
        {
            QVariantMap point = points.at(index).toMap();
            bool valid_count = false;
            const double count = point.value(QStringLiteral("y")).toDouble(&valid_count);
            if (!valid_count || count <= 0.0)
                continue;

            // Histogram points normally use bin centers.  The boundary point
            // associated with a reference marker is moved to the original
            // sample extremum so the curve and its marker share the same
            // visible x coordinate.
            point.insert(QStringLiteral("x"), score);
            points[index] = point;
            return;
        }
    };
    if (has_good)
        alignBoundaryPoint(histogram.good_points, good_max, true);
    if (has_anomaly)
        alignBoundaryPoint(histogram.anomaly_points, anomaly_min, false);

    const auto isolatedPointRadii = [](const QVariantList &points)
    {
        const auto hasValue = [](const QVariant &value)
        {
            bool ok = false;
            const double count = value.toDouble(&ok);
            return ok && std::isfinite(count) && count > 0.0;
        };

        QVariantList radii;
        radii.reserve(points.size());
        for (int index = 0; index < points.size(); ++index)
        {
            const QVariantMap point = points.at(index).toMap();
            const bool current = hasValue(point.value(QStringLiteral("y")));
            const bool previous = index > 0
                && hasValue(points.at(index - 1).toMap().value(QStringLiteral("y")));
            const bool next = index + 1 < points.size()
                && hasValue(points.at(index + 1).toMap().value(QStringLiteral("y")));
            radii.push_back(current && !previous && !next ? 3 : 0);
        }
        return radii;
    };

    const auto distributionDataset = [&isolatedPointRadii](const QString &label, const QString &line_color,
                                                           const QString &fill_color, const QVariantList &points)
    {
        return QVariantMap{{QStringLiteral("label"), label},
                           {QStringLiteral("data"), points},
                           {QStringLiteral("backgroundColor"), fill_color},
                           {QStringLiteral("borderColor"), line_color},
                           {QStringLiteral("pointBackgroundColor"), line_color},
                           {QStringLiteral("pointBorderColor"), line_color},
                           {QStringLiteral("pointRadius"), isolatedPointRadii(points)},
                           {QStringLiteral("pointHoverRadius"), 4},
                           {QStringLiteral("borderWidth"), 2},
                           {QStringLiteral("lineTension"), 0},
                           {QStringLiteral("spanGaps"), false},
                           {QStringLiteral("xAxisID"), QStringLiteral("score-axis")},
                           {QStringLiteral("yAxisID"), QStringLiteral("count-axis")},
                           {QStringLiteral("showLine"), true},
                           {QStringLiteral("fill"), true}};
    };
    const auto referenceDataset = [](const QString &label, const QString &color, const double value,
                                     const int max_count)
    {
        return QVariantMap{{QStringLiteral("label"), label},
                           {QStringLiteral("data"), QVariantList{
                               QVariantMap{{QStringLiteral("x"), value}, {QStringLiteral("y"), 0}},
                               QVariantMap{{QStringLiteral("x"), value}, {QStringLiteral("y"), max_count}}}},
                           {QStringLiteral("backgroundColor"), color},
                           {QStringLiteral("borderColor"), color},
                           {QStringLiteral("borderWidth"), 2},
                           {QStringLiteral("borderDash"), QVariantList{6, 4}},
                           {QStringLiteral("pointRadius"), 0},
                           {QStringLiteral("pointHoverRadius"), 0},
                           {QStringLiteral("lineTension"), 0},
                           {QStringLiteral("spanGaps"), false},
                           {QStringLiteral("xAxisID"), QStringLiteral("score-axis")},
                           {QStringLiteral("yAxisID"), QStringLiteral("count-axis")},
                           {QStringLiteral("showLine"), true},
                           {QStringLiteral("fill"), false}};
    };

    QVariantList datasets;
    if (has_good)
        datasets.push_back(distributionDataset(QStringLiteral("GOOD"), QString::fromLatin1(good_color),
                                                QString::fromLatin1(good_fill), histogram.good_points));
    if (has_anomaly)
        datasets.push_back(distributionDataset(QStringLiteral("Anomaly"), QString::fromLatin1(anomaly_color),
                                                QString::fromLatin1(anomaly_fill), histogram.anomaly_points));
    if (has_good)
        datasets.push_back(referenceDataset(
            QString("GOOD 最大分数：%1").arg(QString::number(good_max, 'f', 4)),
            QString::fromLatin1(good_color), good_max, histogram.max_count));
    if (has_anomaly)
        datasets.push_back(referenceDataset(
            QString("Anomaly 最小分数：%1").arg(QString::number(anomaly_min, 'f', 4)),
            QString::fromLatin1(anomaly_color), anomaly_min, histogram.max_count));

    const double suggested_count = histogram.max_count > 0 ? histogram.max_count * 1.1 : 1.0;
    const QVariantMap options{
        {QStringLiteral("maintainAspectRatio"), false},
        {QStringLiteral("responsive"), true},
        {QStringLiteral("legend"), QVariantMap{{QStringLiteral("display"), true},
                                                 {QStringLiteral("position"), QStringLiteral("top")}}},
        {QStringLiteral("tooltips"), QVariantMap{{QStringLiteral("mode"), QStringLiteral("nearest")},
                                                  {QStringLiteral("intersect"), false}}},
        {QStringLiteral("scales"), QVariantMap{
            {QStringLiteral("xAxes"), QVariantList{QVariantMap{
                {QStringLiteral("id"), QStringLiteral("score-axis")},
                {QStringLiteral("type"), QStringLiteral("linear")},
                {QStringLiteral("display"), true},
                {QStringLiteral("ticks"), QVariantMap{{QStringLiteral("min"), histogram.min_score},
                                                       {QStringLiteral("max"), histogram.max_score},
                                                       {QStringLiteral("maxTicksLimit"), 12},
                                                       {QStringLiteral("maxRotation"), 0},
                                                       {QStringLiteral("minRotation"), 0}}},
                {QStringLiteral("scaleLabel"), QVariantMap{{QStringLiteral("display"), true},
                                                             {QStringLiteral("labelString"), QString("分数")}}}}}},
            {QStringLiteral("yAxes"), QVariantList{QVariantMap{
                {QStringLiteral("id"), QStringLiteral("count-axis")},
                {QStringLiteral("type"), QStringLiteral("linear")},
                {QStringLiteral("display"), true},
                {QStringLiteral("ticks"), QVariantMap{{QStringLiteral("beginAtZero"), true},
                                                        {QStringLiteral("suggestedMax"), suggested_count}}},
                {QStringLiteral("scaleLabel"), QVariantMap{{QStringLiteral("display"), true},
                                                             {QStringLiteral("labelString"), QString("数量")}}}}}}}}};

    return QVariantMap{{evaluation::fieldName(evaluation::Field::Kind), QStringLiteral("line")},
                       {evaluation::fieldName(evaluation::Field::ChartId),
                        QStringLiteral("anomaly_score_distribution")},
                       {evaluation::fieldName(evaluation::Field::FilterKind), QStringLiteral("image_score")},
                       {evaluation::fieldName(evaluation::Field::Title), QString("异常分数分布（图像级 pred_score）")},
                       {evaluation::fieldName(evaluation::Field::Data),
                        QVariantMap{{evaluation::fieldName(evaluation::Field::Labels), histogram.labels},
                                    {evaluation::fieldName(evaluation::Field::Datasets), datasets}}},
                       {evaluation::fieldName(evaluation::Field::Options), options}};
}

QVariantMap anomalyScoreChartForImages(const QList<EvaluationImageRecord> &images)
{
    QVariantList good_scores;
    QVariantList anomaly_scores;
    bool has_good = false;
    bool has_anomaly = false;
    double good_max = 0.0;
    double anomaly_min = std::numeric_limits<double>::max();
    for (const EvaluationImageRecord &image : images)
    {
        const double score = imageScore(image);
        if (image.gt_class_ids.contains(1))
        {
            good_scores.push_back(QVariant());
            anomaly_scores.push_back(score);
            has_anomaly = true;
            anomaly_min = std::min(anomaly_min, score);
        }
        else
        {
            good_scores.push_back(score);
            anomaly_scores.push_back(QVariant());
            has_good = true;
            good_max = std::max(good_max, score);
        }
    }
    return anomalyScoreChart(good_scores, anomaly_scores, has_good, good_max, has_anomaly, anomaly_min);
}

struct EvaluationAggregateInput
{
    struct InstanceEvent
    {
        evaluation::Status status{evaluation::Status::Unknown};
        QString gt_class;
        QString pred_class;
        int gt_class_id{-1};
        int pred_class_id{-1};
    };

    QList<InstanceEvent> instances;
    QList<EvaluationImageRecord> images;
    QMap<int, QString> class_catalog;
    QList<QVariantMap> chart_descriptors;
    QVariantList class_ids;
    double confidence_threshold{0.5};
    double iou_threshold{0.5};
    evaluation::MatchingStrategy matching_strategy{evaluation::MatchingStrategy::GreedyIoU};
    bool has_instance_metrics{false};
    bool has_image_metrics{false};
    bool has_confusion_matrix{false};
    bool anomaly_detection{false};
};

struct EvaluationAggregateOutput
{
    std::vector<EvaluationMetricRecord> instance_metrics;
    std::vector<EvaluationMetricRecord> image_metrics;
    std::vector<EvaluationMetricRecord> per_class_metrics;
    std::vector<EvaluationConfusionCell> confusion;
    QList<QVariantMap> charts;
};

struct AggregateCounts
{
    qint64 tp{0};
    qint64 fp{0};
    qint64 fn{0};
};

struct AggregateBox
{
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};
    bool valid{false};
};

AggregateBox aggregateBox(const QVariantMap &value)
{
    QVariantMap source = value;
    if (source.contains(QStringLiteral("bounds")))
        source = source.value(QStringLiteral("bounds")).toMap();
    if (source.contains(QStringLiteral("values")))
    {
        const QVariantList values = source.value(QStringLiteral("values")).toList();
        if (values.size() >= 4)
        {
            bool ok[4] = {false, false, false, false};
            const double x = values.at(0).toDouble(&ok[0]);
            const double y = values.at(1).toDouble(&ok[1]);
            const double width = values.at(2).toDouble(&ok[2]);
            const double height = values.at(3).toDouble(&ok[3]);
            if (ok[0] && ok[1] && ok[2] && ok[3])
                return {x, y, width, height, width > 0.0 && height > 0.0};
        }
    }
    bool x_ok = false;
    bool y_ok = false;
    bool width_ok = false;
    bool height_ok = false;
    AggregateBox box;
    box.x = source.value(QStringLiteral("x")).toDouble(&x_ok);
    box.y = source.value(QStringLiteral("y")).toDouble(&y_ok);
    box.width = source.value(QStringLiteral("width")).toDouble(&width_ok);
    box.height = source.value(QStringLiteral("height")).toDouble(&height_ok);
    box.valid = x_ok && y_ok && width_ok && height_ok && box.width > 0.0 && box.height > 0.0;
    return box;
}

double aggregateIou(const QVariantMap &lhs, const QVariantMap &rhs)
{
    const AggregateBox a = aggregateBox(lhs);
    const AggregateBox b = aggregateBox(rhs);
    if (!a.valid && !b.valid)
        return 1.0;
    if (!a.valid || !b.valid)
        return 0.0;
    const double left = std::max(a.x, b.x);
    const double top = std::max(a.y, b.y);
    const double right = std::min(a.x + a.width, b.x + b.width);
    const double bottom = std::min(a.y + a.height, b.y + b.height);
    const double intersection = std::max(0.0, right - left) * std::max(0.0, bottom - top);
    const double area = a.width * a.height + b.width * b.height - intersection;
    return area > 0.0 ? intersection / area : 0.0;
}

struct AggregateMatch
{
    int prediction{-1};
    int ground_truth{-1};
    double iou{0.0};
};

QList<AggregateMatch> aggregateMatches(const QList<EvaluationPredictionRecord> &predictions,
                                        const QList<EvaluationGroundTruthRecord> &ground_truth,
                                        const double threshold, const evaluation::MatchingStrategy strategy)
{
    const bool use_hungarian = strategy == evaluation::MatchingStrategy::HungarianIoU;
    if (!use_hungarian)
    {
        struct Candidate
        {
            int prediction{-1};
            int ground_truth{-1};
            double iou{0.0};
        };
        QList<Candidate> candidates;
        for (int prediction = 0; prediction < predictions.size(); ++prediction)
            for (int gt = 0; gt < ground_truth.size(); ++gt)
            {
                const double overlap = aggregateIou(predictions.at(prediction).geometry,
                                                    ground_truth.at(gt).geometry);
                if (overlap >= threshold)
                    candidates.push_back({prediction, gt, overlap});
            }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate &lhs, const Candidate &rhs)
                  {
                      if (lhs.iou != rhs.iou)
                          return lhs.iou > rhs.iou;
                      if (lhs.prediction != rhs.prediction)
                          return lhs.prediction < rhs.prediction;
                      return lhs.ground_truth < rhs.ground_truth;
                  });
        QVector<bool> used_prediction(predictions.size(), false);
        QVector<bool> used_gt(ground_truth.size(), false);
        QList<AggregateMatch> result;
        for (const Candidate &candidate : candidates)
        {
            if (used_prediction.at(candidate.prediction) || used_gt.at(candidate.ground_truth))
                continue;
            used_prediction[candidate.prediction] = true;
            used_gt[candidate.ground_truth] = true;
            result.push_back({candidate.prediction, candidate.ground_truth, candidate.iou});
        }
        return result;
    }

    const int size = std::max(predictions.size(), ground_truth.size());
    if (size <= 0)
        return {};
    QVector<QVector<double>> weight(size, QVector<double>(size, 0.0));
    for (int prediction = 0; prediction < predictions.size(); ++prediction)
        for (int gt = 0; gt < ground_truth.size(); ++gt)
        {
                const double overlap = aggregateIou(predictions.at(prediction).geometry,
                                                    ground_truth.at(gt).geometry);
            if (overlap >= threshold)
                weight[prediction][gt] = overlap;
        }

    // Hungarian maximum-weight assignment.  Dummy rows/columns have zero
    // weight, so assignments below the IoU threshold remain unmatched.
    const int n = size;
    QVector<double> u(n + 1), v(n + 1);
    QVector<int> p(n + 1), way(n + 1);
    for (int row = 1; row <= n; ++row)
    {
        p[0] = row;
        int column0 = 0;
        QVector<double> minv(n + 1, std::numeric_limits<double>::max());
        QVector<bool> used(n + 1, false);
        do
        {
            used[column0] = true;
            const int row0 = p[column0];
            double delta = std::numeric_limits<double>::max();
            int column1 = 0;
            for (int column = 1; column <= n; ++column)
            {
                if (used[column])
                    continue;
                const double current = -weight[row0 - 1][column - 1] - u[row0] - v[column];
                if (current < minv[column])
                {
                    minv[column] = current;
                    way[column] = column0;
                }
                if (minv[column] < delta)
                {
                    delta = minv[column];
                    column1 = column;
                }
            }
            for (int column = 0; column <= n; ++column)
            {
                if (used[column])
                {
                    u[p[column]] += delta;
                    v[column] -= delta;
                }
                else
                    minv[column] -= delta;
            }
            column0 = column1;
        } while (p[column0] != 0);
        do
        {
            const int column1 = way[column0];
            p[column0] = p[column1];
            column0 = column1;
        } while (column0 != 0);
    }

    QList<AggregateMatch> result;
    for (int column = 1; column <= n; ++column)
    {
        const int prediction = p[column] - 1;
        const int gt = column - 1;
        if (prediction < 0 || prediction >= predictions.size() || gt < 0 || gt >= ground_truth.size())
            continue;
        const double overlap = weight[prediction][gt];
        if (overlap >= threshold)
            result.push_back({prediction, gt, overlap});
    }
    std::sort(result.begin(), result.end(), [](const AggregateMatch &lhs, const AggregateMatch &rhs)
              { return lhs.prediction < rhs.prediction; });
    return result;
}

EvaluationMetricRecord aggregateMetric(const QString &key, const QString &label, const int class_id,
                                       const AggregateCounts &counts)
{
    EvaluationMetricRecord value;
    value.key = key;
    value.label = label;
    value.class_name = label;
    value.class_id = class_id;
    value.tp = counts.tp;
    value.fp = counts.fp;
    value.fn = counts.fn;
    value.precision_defined = counts.tp + counts.fp > 0;
    value.recall_defined = counts.tp + counts.fn > 0;
    value.precision = value.precision_defined ? static_cast<double>(counts.tp) / (counts.tp + counts.fp) : 0.0;
    value.recall = value.recall_defined ? static_cast<double>(counts.tp) / (counts.tp + counts.fn) : 0.0;
    value.f1_defined = value.precision_defined && value.recall_defined && value.precision + value.recall > 0.0;
    value.f1 = value.f1_defined ? 2.0 * value.precision * value.recall / (value.precision + value.recall) : 0.0;
    return value;
}

bool aggregateClassAllowed(const QVariantList &class_ids, const int class_id)
{
    if (class_ids.isEmpty() || class_id < 0)
        return true;
    for (const QVariant &value : class_ids)
        if (value.toInt() == class_id)
            return true;
    return false;
}

AggregateCounts aggregateThresholdCounts(const QList<EvaluationImageRecord> &images, const QVariantList &class_ids,
                                          const double threshold, const double iou_threshold,
                                          const evaluation::MatchingStrategy matching_strategy)
{
    AggregateCounts counts;
    for (const EvaluationImageRecord &image : images)
    {
        QList<EvaluationPredictionRecord> predictions;
        QList<EvaluationGroundTruthRecord> ground_truth;
        for (const EvaluationPredictionRecord &prediction : image.predictions)
        {
            if (prediction.score >= threshold && aggregateClassAllowed(class_ids, prediction.class_id))
                predictions.push_back(prediction);
        }
        for (const EvaluationGroundTruthRecord &gt : image.gt_instances)
        {
            if (aggregateClassAllowed(class_ids, gt.class_id))
                ground_truth.push_back(gt);
        }
        std::sort(predictions.begin(), predictions.end(), [](const auto &lhs, const auto &rhs)
                  { return lhs.score > rhs.score; });
        const QList<AggregateMatch> matches
            = aggregateMatches(predictions, ground_truth, iou_threshold, matching_strategy);
        QVector<bool> used_prediction(predictions.size(), false);
        QVector<bool> used_gt(ground_truth.size(), false);
        for (const AggregateMatch &candidate : matches)
        {
            if (used_prediction.at(candidate.prediction) || used_gt.at(candidate.ground_truth))
                continue;
            used_prediction[candidate.prediction] = true;
            used_gt[candidate.ground_truth] = true;
            if (predictions.at(candidate.prediction).class_id == ground_truth.at(candidate.ground_truth).class_id)
                ++counts.tp;
            else
            {
                ++counts.fp;
                ++counts.fn;
            }
        }
        for (int index = 0; index < used_prediction.size(); ++index)
            if (!used_prediction.at(index))
                ++counts.fp;
        for (int index = 0; index < used_gt.size(); ++index)
            if (!used_gt.at(index))
                ++counts.fn;
    }
    return counts;
}

EvaluationAggregateOutput aggregateEvaluation(const EvaluationAggregateInput &input)
{
    EvaluationAggregateOutput output;
    const QString matrix_fn = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalseNegative);
    const QString matrix_fp = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalsePositive);
    const QString matrix_total = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::Total);
    QMap<int, AggregateCounts> classes;
    QMap<int, QString> class_names = input.class_catalog;
    QMap<QString, qint64> matrix;
    AggregateCounts overall;
    for (const EvaluationAggregateInput::InstanceEvent &record : input.instances)
    {
        if (record.gt_class_id >= 0)
            class_names.insert(record.gt_class_id,
                               record.gt_class.isEmpty() ? QString::number(record.gt_class_id) : record.gt_class);
        if (record.pred_class_id >= 0)
            class_names.insert(record.pred_class_id,
                               record.pred_class.isEmpty() ? QString::number(record.pred_class_id) : record.pred_class);
        if (record.status == evaluation::Status::TruePositive)
        {
            ++overall.tp;
            ++classes[record.pred_class_id].tp;
            ++matrix[QString::number(record.pred_class_id) + QLatin1Char('\x1f') + QString::number(record.gt_class_id)];
        }
        else if (record.status == evaluation::Status::ClassMismatch)
        {
            ++overall.fp;
            ++overall.fn;
            ++classes[record.pred_class_id].fp;
            ++classes[record.gt_class_id].fn;
            ++matrix[QString::number(record.pred_class_id) + QLatin1Char('\x1f') + QString::number(record.gt_class_id)];
        }
        else if (record.status == evaluation::Status::FalsePositive)
        {
            ++overall.fp;
            ++classes[record.pred_class_id].fp;
            ++matrix[QString::number(record.pred_class_id) + QLatin1Char('\x1f') + matrix_fp];
        }
        else if (record.status == evaluation::Status::FalseNegative)
        {
            ++overall.fn;
            ++classes[record.gt_class_id].fn;
            ++matrix[matrix_fn + QLatin1Char('\x1f') + QString::number(record.gt_class_id)];
        }
    }

    // Anomaly projects are evaluated at image level.  A GOOD image has no
    // ground-truth label in the project database/task selection, so the instance-event
    // matrix above cannot represent true negatives (and anomaly projects do
    // not produce instance events).  Build the binary image matrix explicitly
    // while retaining the same FP/FN/total row and column layout as detection.
    if (input.anomaly_detection)
    {
        class_names.clear();
        class_names.insert(0, QStringLiteral("GOOD"));
        class_names.insert(1, QStringLiteral("Anomaly"));
        matrix.clear();
        for (const EvaluationImageRecord &image : input.images)
        {
            const bool ground_truth_anomaly = isAnomalyImage(image, input.confidence_threshold, false);
            const bool predicted_anomaly = isAnomalyImage(image, input.confidence_threshold, true);
            const QString row = predicted_anomaly ? QStringLiteral("1") : QStringLiteral("0");
            const QString column = ground_truth_anomaly ? QStringLiteral("1") : QStringLiteral("0");
            ++matrix[row + QLatin1Char('\x1f') + column];
        }
    }

    AggregateCounts image_counts;
    for (const EvaluationImageRecord &image : input.images)
    {
        QSet<int> gt_classes;
        QSet<int> pred_classes;
        for (const int class_id : gtClassIds(image))
            if (aggregateClassAllowed(input.class_ids, class_id))
                gt_classes.insert(class_id);
        for (const int class_id : predClassIds(image, input.confidence_threshold))
            if (aggregateClassAllowed(input.class_ids, class_id))
                pred_classes.insert(class_id);
        for (const int class_id : pred_classes)
        {
            if (gt_classes.contains(class_id))
                ++image_counts.tp;
            else
                ++image_counts.fp;
        }
        for (const int class_id : gt_classes)
            if (!pred_classes.contains(class_id))
                ++image_counts.fn;
        if (gt_classes.isEmpty() && pred_classes.isEmpty())
        {
            const bool has_gt = hasGroundTruth(image) && input.class_ids.isEmpty();
            const bool has_pred = hasPredictions(image, input.confidence_threshold) && input.class_ids.isEmpty();
            if (has_gt && has_pred)
                ++image_counts.tp;
            else if (has_pred)
                ++image_counts.fp;
            else if (has_gt)
                ++image_counts.fn;
        }
    }

    if (input.has_instance_metrics)
    {
        output.instance_metrics.push_back(aggregateMetric(QStringLiteral("overall"), QString("整体"), -1, overall));
        for (auto it = class_names.cbegin(); it != class_names.cend(); ++it)
            output.per_class_metrics.push_back(aggregateMetric(QString::number(it.key()), it.value(), it.key(),
                                                               classes.value(it.key())));
    }
    if (input.has_image_metrics)
        output.image_metrics.push_back(aggregateMetric(QStringLiteral("image"), QString("图像"), -1, image_counts));

    QMap<int, qint64> pred_totals;
    QMap<int, qint64> gt_totals;
    qint64 unmatched_fp = 0;
    qint64 unmatched_fn = 0;
    qint64 matched_pairs = 0;
    for (auto it = matrix.cbegin(); it != matrix.cend(); ++it)
    {
        const QList<QString> parts = it.key().split(QLatin1Char('\x1f'));
        if (parts.size() != 2)
            continue;
        if (parts.at(0) == matrix_fn)
            unmatched_fn += it.value();
        else
            pred_totals[parts.at(0).toInt()] += it.value();
        if (parts.at(1) == matrix_fp)
            unmatched_fp += it.value();
        else
        {
            gt_totals[parts.at(1).toInt()] += it.value();
            if (parts.at(0) != matrix_fn && parts.at(1) != matrix_fp)
                matched_pairs += it.value();
        }
    }
    const auto append_cell = [&output, &class_names, &matrix_fn, &matrix_total, &matrix_fp](
        const QString &row, const QString &column, const qint64 count, const evaluation::CellKind kind,
        const bool selectable, const bool diagonal, const bool error)
    {
        const bool row_fn = row == matrix_fn;
        const bool row_total = row == matrix_total;
        const bool column_fp = column == matrix_fp;
        const bool column_total = column == matrix_total;
        const int row_id = row_fn || row_total ? -1 : row.toInt();
        const int column_id = column_fp || column_total ? -1 : column.toInt();
        const QString total = QString("合计");
        output.confusion.push_back({row, column,
                                    row_fn ? matrix_fn : (row_total ? total : class_names.value(row_id)),
                                    column_fp ? matrix_fp
                                              : (column_total ? total : class_names.value(column_id)),
                                    count, row_id, column_id, kind, QString(), selectable, diagonal, error});
    };
    if (input.has_confusion_matrix)
    {
        for (auto row_it = class_names.cbegin(); row_it != class_names.cend(); ++row_it)
        {
            const QString row = QString::number(row_it.key());
            for (auto column_it = class_names.cbegin(); column_it != class_names.cend(); ++column_it)
            {
                const QString column = QString::number(column_it.key());
                append_cell(row, column, matrix.value(row + QLatin1Char('\x1f') + column),
                            row_it.key() == column_it.key() ? evaluation::CellKind::Match
                                                            : evaluation::CellKind::ClassMismatch,
                            true, row_it.key() == column_it.key(), row_it.key() != column_it.key());
            }
            append_cell(row, matrix_fp, matrix.value(row + QLatin1Char('\x1f') + matrix_fp),
                        evaluation::CellKind::FalsePositive, true, false, true);
            append_cell(row, matrix_total, pred_totals.value(row_it.key()), evaluation::CellKind::PredTotal,
                        true, false, false);
        }
        for (auto column_it = class_names.cbegin(); column_it != class_names.cend(); ++column_it)
        {
            const QString column = QString::number(column_it.key());
            append_cell(matrix_fn, column,
                        matrix.value(matrix_fn + QLatin1Char('\x1f') + column),
                        evaluation::CellKind::FalseNegative, true, false, true);
        }
        append_cell(matrix_fn, matrix_fp, 0, evaluation::CellKind::NotApplicable, false, false, false);
        append_cell(matrix_fn, matrix_total, unmatched_fn,
                    evaluation::CellKind::FalseNegativeTotal, true, false, true);
        for (auto column_it = class_names.cbegin(); column_it != class_names.cend(); ++column_it)
        {
            const QString column = QString::number(column_it.key());
            append_cell(matrix_total, column, gt_totals.value(column_it.key()), evaluation::CellKind::GtTotal,
                        true, false, false);
        }
        append_cell(matrix_total, matrix_fp, unmatched_fp,
                    evaluation::CellKind::FalsePositiveTotal, true, false, true);
        append_cell(matrix_total, matrix_total, matched_pairs + unmatched_fp + unmatched_fn,
                    evaluation::CellKind::All, true, false, false);
    }

    if (input.has_instance_metrics)
    {
        QVariantList labels;
        QVariantList precision;
        QVariantList recall;
        QVariantList f1;
        for (const EvaluationMetricRecord &metric : output.per_class_metrics)
        {
            labels.push_back(metric.label);
            precision.push_back(metric.precision);
            recall.push_back(metric.recall);
            f1.push_back(metric.f1);
        }
        output.charts.push_back({{evaluation::fieldName(evaluation::Field::Kind), QStringLiteral("bar")},
                                 {evaluation::fieldName(evaluation::Field::ChartId),
                                  QStringLiteral("per_class_metrics")},
                                 {evaluation::fieldName(evaluation::Field::FilterKind),
                                  QStringLiteral("per_class_metrics")},
                                 {evaluation::fieldName(evaluation::Field::Title), QString("按类别指标")},
                                 {evaluation::fieldName(evaluation::Field::Data), QVariantMap{
                                     {evaluation::fieldName(evaluation::Field::Labels), labels},
                                     {evaluation::fieldName(evaluation::Field::Datasets), QVariantList{
                                         QVariantMap{{evaluation::fieldName(evaluation::Field::Label),
                                                      QStringLiteral("Precision")},
                                                     {evaluation::fieldName(evaluation::Field::Data), precision}},
                                         QVariantMap{{evaluation::fieldName(evaluation::Field::Label),
                                                      QStringLiteral("Recall")},
                                                     {evaluation::fieldName(evaluation::Field::Data), recall}},
                                         QVariantMap{{evaluation::fieldName(evaluation::Field::Label),
                                                      QStringLiteral("F1")},
                                                     {evaluation::fieldName(evaluation::Field::Data), f1}}}}}},
                                  {evaluation::fieldName(evaluation::Field::Options),
                                   QVariantMap{{QStringLiteral("maintainAspectRatio"), false}}}});
    }

    if (input.anomaly_detection)
    {
        QList<EvaluationImageRecord> images;
        images.reserve(input.images.size());
        for (const EvaluationImageRecord &image : input.images)
            images.push_back(image);
        output.charts.push_back(anomalyScoreChartForImages(images));
    }

    for (const QVariantMap &descriptor : input.chart_descriptors)
    {
        const QString chart_id = descriptor.value(evaluation::fieldName(evaluation::Field::ChartId)).toString();
        const QString filter_kind = descriptor.value(evaluation::fieldName(evaluation::Field::FilterKind)).toString();
        if (chart_id == QStringLiteral("anomaly_score_distribution"))
            continue;
        if (descriptor.value(evaluation::fieldName(evaluation::Field::Kind)).toString() == QStringLiteral("bar")
            && (filter_kind == QStringLiteral("per_class_metrics")
                || chart_id == QStringLiteral("per_class_metrics")))
            continue;
        QVariantMap filtered = descriptor;
        if (filter_kind == QStringLiteral("precision_recall")
            || chart_id == QStringLiteral("precision_recall"))
        {
            QList<double> thresholds{1.0, input.confidence_threshold};
            for (const EvaluationImageRecord &image : input.images)
                for (const EvaluationPredictionRecord &prediction : image.predictions)
                    if (std::isfinite(prediction.score) && aggregateClassAllowed(input.class_ids, prediction.class_id))
                        thresholds.push_back(std::clamp(prediction.score, 0.0, 1.0));
            std::sort(thresholds.begin(), thresholds.end(), std::greater<double>());
            QList<double> unique;
            for (const double value : thresholds)
                if (unique.isEmpty() || !qFuzzyCompare(unique.back() + 1.0, value + 1.0))
                    unique.push_back(value);
            QVariantList labels;
            QVariantList precision;
            QVariantList recall;
            for (const double threshold : unique)
            {
                const AggregateCounts counts
                    = aggregateThresholdCounts(input.images, input.class_ids, threshold, input.iou_threshold,
                                                input.matching_strategy);
                labels.push_back(threshold);
                precision.push_back(counts.tp + counts.fp > 0
                                        ? static_cast<double>(counts.tp) / (counts.tp + counts.fp)
                                        : 0.0);
                recall.push_back(counts.tp + counts.fn > 0
                                     ? static_cast<double>(counts.tp) / (counts.tp + counts.fn)
                                     : 0.0);
            }
            filtered.insert(evaluation::fieldName(evaluation::Field::Data), QVariantMap{
                {evaluation::fieldName(evaluation::Field::Labels), labels},
                {evaluation::fieldName(evaluation::Field::Datasets), QVariantList{
                    QVariantMap{{evaluation::fieldName(evaluation::Field::Label), QStringLiteral("Precision")},
                                {evaluation::fieldName(evaluation::Field::Data), precision}},
                    QVariantMap{{evaluation::fieldName(evaluation::Field::Label), QStringLiteral("Recall")},
                                {evaluation::fieldName(evaluation::Field::Data), recall}}}}});
        }
        else if (filter_kind == QStringLiteral("image_score"))
        {
            QVariantList labels;
            QVariantList scores;
            for (const EvaluationImageRecord &image : input.images)
            {
                labels.push_back(image.image_name);
                scores.push_back(imageScore(image));
            }
            filtered.insert(evaluation::fieldName(evaluation::Field::Data), QVariantMap{
                {evaluation::fieldName(evaluation::Field::Labels), labels},
                {evaluation::fieldName(evaluation::Field::Datasets), QVariantList{
                    QVariantMap{{evaluation::fieldName(evaluation::Field::Label), QStringLiteral("score")},
                                {evaluation::fieldName(evaluation::Field::Data), scores}}}}});
        }
        output.charts.push_back(std::move(filtered));
    }
    return output;
}

} // namespace

ModelEvaluationViewModel::ModelEvaluationViewModel(QObject *parent)
    : QObject(parent)
    , state_(evaluation::viewStateKey(evaluation::ViewState::NotRun))
    , instance_metrics_(new EvaluationMetricModel(this))
    , image_metrics_(new EvaluationMetricModel(this))
    , per_class_metrics_(new EvaluationMetricModel(this))
    , sorted_per_class_metrics_(new EvaluationMetricSortProxyModel(this))
    , confusion_matrix_(new EvaluationConfusionModel(this))
    , images_(new EvaluationImageModel(this))
    , filtered_images_(new EvaluationImageFilterProxyModel(this))
    , instances_(new EvaluationInstanceModel(this))
    , global_filtered_instances_(new EvaluationGlobalFilterProxyModel(this))
    , filtered_instances_(new EvaluationCellFilterProxyModel(this))
    , charts_(new EvaluationChartModel(this))
{
    sorted_per_class_metrics_->setSourceModel(per_class_metrics_);
    filtered_images_->setSourceModel(images_);
    global_filtered_instances_->setSourceModel(instances_);
    filtered_instances_->setSourceModel(global_filtered_instances_);
    connect(global_filtered_instances_, &EvaluationGlobalFilterProxyModel::filterChanged, this,
            [this]()
            {
                selected_proxy_row_ = -1;
                selected_instance_.clear();
                instances_->setSelectedEvent({});
                scheduleRebuildFilteredAggregates();
                emit selectedInstanceChanged();
            });
    connect(filtered_images_, &EvaluationImageFilterProxyModel::filterChanged, this,
            [this]()
            {
                scheduleRebuildFilteredAggregates();
            });
    connect(filtered_instances_, &EvaluationCellFilterProxyModel::filterChanged, this,
            [this]()
            {
                selected_proxy_row_ = -1;
                selected_instance_.clear();
                instances_->setSelectedEvent({});
                emit selectedInstanceChanged();
            });
    connect(filtered_instances_, &QAbstractItemModel::modelReset, this,
            [this]() { selected_instance_ = {}; emit selectedInstanceChanged(); });
    connect(filtered_instances_, &QAbstractItemModel::rowsRemoved, this,
            [this]()
            {
                // Do not touch the source selection model while a proxy is
                // still dispatching rowsRemoved.  GridView can also request
                // the old index during this notification window.
                if (selected_proxy_row_ < 0)
                    return;
                const int selected_row = selected_proxy_row_;
                QMetaObject::invokeMethod(this,
                                          [this, selected_row]()
                                          {
                                              if (selected_proxy_row_ != selected_row || filtered_instances_ == nullptr)
                                                  return;
                                              const int row_count = filtered_instances_->rowCount();
                                              if (selected_proxy_row_ >= row_count)
                                              {
                                                  selectInstance(-1);
                                              }
                                          },
                                          Qt::QueuedConnection);
            });
}

bool ModelEvaluationViewModel::available() const { return available_; }
bool ModelEvaluationViewModel::loading() const { return loading_; }
QString ModelEvaluationViewModel::state() const { return state_; }
QString ModelEvaluationViewModel::error() const { return error_; }

QString ModelEvaluationViewModel::primaryMetricSet() const { return primary_metric_set_; }
bool ModelEvaluationViewModel::globalFilterActive() const
{
    if (global_filter_ == nullptr)
        return false;
    bool active = false;
    if (hasInvokable(global_filter_, "isActive", 0))
        QMetaObject::invokeMethod(global_filter_, "isActive", Qt::DirectConnection, Q_RETURN_ARG(bool, active));
    return active;
}

QString ModelEvaluationViewModel::globalFilterDescription() const
{
    if (global_filter_ != nullptr)
    {
        QString description;
        if (hasInvokable(global_filter_, "description", 0)
            && QMetaObject::invokeMethod(global_filter_, "description", Qt::DirectConnection,
                                         Q_RETURN_ARG(QString, description)) && !description.isEmpty())
            return description;
    }
    return globalFilterActive() ? QString("当前已应用全局过滤") : QString("全部测试样本");
}

QString ModelEvaluationViewModel::metricScopeDescription() const { return metric_scope_description_; }
QVariantMap ModelEvaluationViewModel::imageMetricDefinition() const { return image_metric_definition_; }
QString ModelEvaluationViewModel::resultRevision() const { return result_revision_; }
double ModelEvaluationViewModel::confidenceThreshold() const { return confidence_threshold_; }
double ModelEvaluationViewModel::iouThreshold() const { return iou_threshold_; }
QString ModelEvaluationViewModel::matchingStrategy() const { return matching_strategy_; }
bool ModelEvaluationViewModel::hasInstanceMetrics() const { return has_instance_metrics_; }
bool ModelEvaluationViewModel::hasImageMetrics() const { return has_image_metrics_; }
bool ModelEvaluationViewModel::hasConfusionMatrix() const { return has_confusion_matrix_; }
bool ModelEvaluationViewModel::hasInstanceEvents() const { return has_instance_events_; }
EvaluationMetricModel *ModelEvaluationViewModel::instanceMetrics() const { return instance_metrics_; }
EvaluationMetricModel *ModelEvaluationViewModel::imageMetrics() const { return image_metrics_; }
EvaluationMetricModel *ModelEvaluationViewModel::perClassMetrics() const { return per_class_metrics_; }
EvaluationMetricSortProxyModel *ModelEvaluationViewModel::sortedPerClassMetrics() const
{
    return sorted_per_class_metrics_;
}
EvaluationConfusionModel *ModelEvaluationViewModel::confusionMatrix() const { return confusion_matrix_; }
EvaluationImageModel *ModelEvaluationViewModel::images() const { return images_; }
EvaluationImageFilterProxyModel *ModelEvaluationViewModel::filteredImages() const { return filtered_images_; }
EvaluationInstanceModel *ModelEvaluationViewModel::instances() const { return instances_; }
EvaluationGlobalFilterProxyModel *ModelEvaluationViewModel::globalFilteredInstances() const
{
    return global_filtered_instances_;
}
EvaluationCellFilterProxyModel *ModelEvaluationViewModel::filteredInstances() const { return filtered_instances_; }
EvaluationChartModel *ModelEvaluationViewModel::charts() const { return charts_; }
QVariantMap ModelEvaluationViewModel::selectedInstance() const { return selected_instance_; }
QString ModelEvaluationViewModel::selectedEventUuid() const
{
    return selected_instance_.value(QStringLiteral("eventUuid")).toString();
}

int ModelEvaluationViewModel::selectedInstanceRow() const
{
    return selected_proxy_row_;
}

void ModelEvaluationViewModel::setLoading(const bool value)
{
    if (loading_ == value)
        return;
    loading_ = value;
    if (value)
        state_ = evaluation::viewStateKey(evaluation::ViewState::Loading);
    else if (state_ == evaluation::viewStateKey(evaluation::ViewState::Loading))
        state_ = available_ ? evaluation::viewStateKey(evaluation::ViewState::Ready)
                            : (error_.isEmpty() ? evaluation::viewStateKey(evaluation::ViewState::NotRun)
                                                : evaluation::viewStateKey(evaluation::ViewState::Error));
    emit loadingChanged();
}

void ModelEvaluationViewModel::clearEvaluation(const QString &error, const QString &state)
{
    ++aggregation_revision_;
    ++aggregation_schedule_token_;
    aggregation_rebuild_scheduled_ = false;
    available_ = false;
    error_ = error;
    state_ = state.isEmpty()
        ? (error.isEmpty() ? evaluation::viewStateKey(evaluation::ViewState::NotRun)
                           : evaluation::viewStateKey(evaluation::ViewState::Error))
        : state;
    primary_metric_set_.clear();
    metric_scope_description_.clear();
    image_metric_definition_.clear();
    result_revision_.clear();
    confidence_threshold_ = 0.0;
    iou_threshold_ = 0.0;
    matching_strategy_.clear();
    has_instance_metrics_ = false;
    has_image_metrics_ = false;
    has_confusion_matrix_ = false;
    has_instance_events_ = false;
    anomaly_detection_ = false;
    const bool previous_suppress_aggregation_rebuild = suppress_aggregation_rebuild_;
    suppress_aggregation_rebuild_ = true;
    instance_metrics_->setRecords({});
    image_metrics_->setRecords({});
    per_class_metrics_->setRecords({});
    confusion_matrix_->setRecords({});
    images_->setRecords({});
    instances_->setRecords({});
    global_filtered_instances_->setDatasetIds({});
    global_filtered_instances_->setClassIds({});
    filtered_instances_->setStatus({});
    filtered_instances_->setMatrixRow({});
    filtered_instances_->setMatrixColumn({});
    filtered_instances_->setPredClassIds({});
    filtered_instances_->setMinScore(-std::numeric_limits<double>::infinity());
    filtered_instances_->setMaxScore(std::numeric_limits<double>::infinity());
    charts_->setRecords({});
    suppress_aggregation_rebuild_ = previous_suppress_aggregation_rebuild;
    selected_instance_.clear();
    selected_proxy_row_ = -1;
    instances_->setSelectedEvent({});
}

bool ModelEvaluationViewModel::sameEvaluationInput(const ModelEvaluationOptions &lhs,
                                                   const ModelEvaluationOptions &rhs) const
{
    return lhs.model_uuid == rhs.model_uuid
        && lhs.test_task_uuid == rhs.test_task_uuid
        && lhs.model_name == rhs.model_name
        && lhs.task_directory == rhs.task_directory
        && lhs.method == rhs.method
        && lhs.project_database_path == rhs.project_database_path
        && lhs.dataset_file_list_path == rhs.dataset_file_list_path
        && lhs.task_database_path == rhs.task_database_path
        && lhs.prediction_dir == rhs.prediction_dir
        && lhs.evaluation_config == rhs.evaluation_config
        && qFuzzyCompare(lhs.confidence_threshold + 1.0, rhs.confidence_threshold + 1.0)
        && qFuzzyCompare(lhs.iou_threshold + 1.0, rhs.iou_threshold + 1.0)
        && lhs.matching_strategy == rhs.matching_strategy;
}

void ModelEvaluationViewModel::setEvaluationOptions(const ModelEvaluationOptions &options)
{
    if (has_evaluation_options_ && sameEvaluationInput(evaluation_options_, options))
        return;
    evaluation_options_ = options;
    has_evaluation_options_ = true;
    invalidate();
}

void ModelEvaluationViewModel::invalidate(const QString &state)
{
    ++evaluation_revision_;
    evaluation_attempted_ = false;
    notify_when_finished_ = false;
    if (cancel_token_ != nullptr)
        cancel_token_->store(true, std::memory_order_relaxed);
    cancel_token_.reset();
    clearEvaluation({}, state.isEmpty() ? evaluation::viewStateKey(evaluation::ViewState::NotRun) : state);
    setLoading(false);
    emit evaluationChanged();
    emit selectedInstanceChanged();
}

void ModelEvaluationViewModel::evaluate(const bool notify)
{
    if (!has_evaluation_options_)
    {
        invalidate(evaluation::viewStateKey(evaluation::ViewState::MissingResult));
        return;
    }
    if (loading_)
    {
        notify_when_finished_ = notify_when_finished_ || notify;
        return;
    }
    if (evaluation_attempted_)
        return;

    // 尚未开始测试或文件列表被清理时没有可评估的输入，显示“还没有可评估的预测结果”，
    // 而不是当作后台评估失败。
    if (!QFileInfo(evaluation_options_.dataset_file_list_path).isFile())
    {
        invalidate(evaluation::viewStateKey(evaluation::ViewState::MissingResult));
        return;
    }

    const int revision = ++evaluation_revision_;
    notify_when_finished_ = notify;
    clearEvaluation();
    cancel_token_ = std::make_shared<std::atomic_bool>(false);
    ModelEvaluationOptions options = evaluation_options_;
    options.cancel_token = cancel_token_;
    setLoading(true);

    const QPointer<ModelEvaluationViewModel> guard(this);
    QThreadPool::globalInstance()->start([guard, revision, options, notify]()
    {
        if (guard.isNull())
            return;

        ModelEvaluationResult evaluation_result;
        QString error;
        const bool success = ModelEvaluationService::evaluate(options, &evaluation_result, &error);
        QMetaObject::invokeMethod(guard.data(),
                                  [guard, revision, options, notify, success,
                                   result = std::move(evaluation_result.evaluation_data), error]() mutable
        {
            if (guard.isNull() || guard->evaluation_revision_ != revision)
                return;

            const bool should_notify = guard->notify_when_finished_ || notify;
            guard->notify_when_finished_ = false;
            if (!success)
            {
                guard->evaluation_attempted_ = true;
                const QString message = error.isEmpty() ? QString("C++ 评估失败") : error;
                spdlog::error("测试任务 {} 评估失败: {}", options.test_task_uuid.toUtf8().constData(),
                              message.toUtf8().constData());
                guard->clearEvaluation(message, evaluation::viewStateKey(evaluation::ViewState::Error));
                guard->setLoading(false);
                emit guard->evaluationChanged();
                emit guard->selectedInstanceChanged();
                if (should_notify)
                    ui::SignalHelper::notifyError(QString("模型评估失败"), message);
                return;
            }

            guard->result_revision_ = QString::number(QDateTime::currentMSecsSinceEpoch());
            guard->loadEvaluation(result);
            guard->loadInstanceRecords(result.value(evaluation::fieldName(evaluation::Field::InstanceRecords)).toList());
            guard->loadDerivedCharts();
            guard->evaluation_attempted_ = true;
            guard->available_ = true;
            guard->state_ = evaluation::viewStateKey(evaluation::ViewState::Ready);
            guard->error_.clear();
            guard->scheduleRebuildFilteredAggregates();
            guard->setLoading(false);
            emit guard->evaluationChanged();
            emit guard->selectedInstanceChanged();
            if (should_notify)
            {
                spdlog::info("测试任务 {} 评估完成", options.test_task_uuid.toUtf8().constData());
                ui::SignalHelper::notifySuccess(QString("模型评估完成"), QString("评估结果已更新"));
            }
        }, Qt::QueuedConnection);
    });
}

void ModelEvaluationViewModel::refreshEvaluation()
{
    invalidate();
    evaluate(false);
}

void ModelEvaluationViewModel::setRuntimeState(const QString &state)
{
    const QString value = state.trimmed();
    if (value.isEmpty())
        return;
    if (state_ == value && !available_ && error_.isEmpty())
        return;
    if (value == evaluation::viewStateKey(evaluation::ViewState::Running)
        || value == evaluation::viewStateKey(evaluation::ViewState::Failed)
        || value == evaluation::viewStateKey(evaluation::ViewState::NotRun))
        invalidate(value);
}

void ModelEvaluationViewModel::loadEvaluation(const QVariantMap &root)
{
    const evaluation::MetricSet metric_set
        = evaluation::metricSetFromKey(root.value(evaluation::fieldName(evaluation::Field::PrimaryMetricSet)).toString());
    primary_metric_set_ = evaluation::metricSetKey(metric_set);
    metric_scope_description_ = metric_set == evaluation::MetricSet::Official
        ? QString("官方指标")
        : QString("诊断匹配指标");
    image_metric_definition_ = root.value(evaluation::fieldName(evaluation::Field::ImageMetricDefinition)).toMap();
    const QVariantMap evaluation_config = root.value(evaluation::fieldName(evaluation::Field::EvaluationConfig)).toMap();
    anomaly_detection_ = evaluation::isAnomaly(evaluation_options_.method);
    confidence_threshold_ = realValue(evaluation_config, evaluation::Field::ConfidenceThreshold);
    iou_threshold_ = realValue(evaluation_config, evaluation::Field::IouThreshold);
    matching_strategy_ = evaluation::matchingStrategyKey(
        evaluation::matchingStrategyFromKey(textValue(evaluation_config, evaluation::Field::MatchingStrategy)));
    const QVariantMap capabilities = root.value(evaluation::fieldName(evaluation::Field::Capabilities)).toMap();
    has_instance_metrics_ = capabilities.value(evaluation::fieldName(evaluation::Field::HasInstanceMetrics)).toBool();
    has_image_metrics_ = capabilities.value(evaluation::fieldName(evaluation::Field::HasImageMetrics)).toBool();
    has_confusion_matrix_ = capabilities.value(evaluation::fieldName(evaluation::Field::HasConfusionMatrix)).toBool()
        || anomaly_detection_;
    // Anomaly results are image-level, but the instance grid still consumes
    // the one in-memory C++ event per image so matrix selections can show
    // GOOD/Anomaly samples, including true negatives with no original event.
    has_instance_events_ = capabilities.value(evaluation::fieldName(evaluation::Field::HasInstanceEvents)).toBool()
        || anomaly_detection_;
    const QVariantMap diagnostic = root.value(evaluation::fieldName(evaluation::Field::DiagnosticMetrics)).toMap();
    const QVariantMap instance = diagnostic.value(evaluation::fieldName(evaluation::Field::Instance)).toMap();
    const QVariantMap overall = instance.value(evaluation::fieldName(evaluation::Field::Overall)).toMap();
    if (!overall.isEmpty())
        instance_metrics_->setRecords({metricFromMap(QStringLiteral("overall"), overall, QString("整体"))});
    const QVariantMap image = diagnostic.value(evaluation::fieldName(evaluation::Field::Image)).toMap();
    if (!image.isEmpty())
        image_metrics_->setRecords({metricFromMap(QStringLiteral("image"), image, QString("图像"))});

    // The result keeps official and diagnostic metrics separate.  The
    // primary set only changes which values are shown in the overall panel;
    // matrix/events always remain diagnostic records.
    const QVariantMap official = root.value(evaluation::fieldName(evaluation::Field::OfficialMetrics)).toMap();
    if (primary_metric_set_ == evaluation::metricSetKey(evaluation::MetricSet::Official)
        && official.value(evaluation::fieldName(evaluation::Field::Available)).toBool())
    {
        const QVariantMap official_instance = official.value(evaluation::fieldName(evaluation::Field::Instance)).toMap();
        if (!official_instance.isEmpty())
            instance_metrics_->setRecords({metricFromMap(QStringLiteral("overall"), official_instance,
                                                          QString("整体"))});
        const QVariantMap official_image = official.value(evaluation::fieldName(evaluation::Field::Image)).toMap();
        if (!official_image.isEmpty())
            image_metrics_->setRecords({metricFromMap(QStringLiteral("image"), official_image,
                                                       QString("图像"))});
    }

    std::vector<EvaluationImageRecord> image_records;
    for (const QVariant &value : root.value(evaluation::fieldName(evaluation::Field::ImageRecords)).toList())
        image_records.push_back(imageFromMap(value.toMap()));
    images_->setRecords(std::move(image_records));

    std::vector<EvaluationMetricRecord> per_class;
    for (const QVariant &value : instance.value(evaluation::fieldName(evaluation::Field::PerClass)).toList())
    {
        const QVariantMap map = value.toMap();
        const QString key = map.value(evaluation::fieldName(evaluation::Field::ClassId)).toString();
        per_class.push_back(metricFromMap(key, map,
                                          map.value(evaluation::fieldName(evaluation::Field::ClassName)).toString()));
    }
    per_class_metrics_->setRecords(std::move(per_class));

    std::vector<EvaluationConfusionCell> cells;
    const QVariantMap matrix = root.value(evaluation::fieldName(evaluation::Field::ConfusionMatrix)).toMap();
    const QVariantList matrix_cells = matrix.value(evaluation::fieldName(evaluation::Field::Cells)).toList();
    for (const QVariant &value : matrix_cells)
    {
        const QVariantMap map = value.toMap();
        EvaluationConfusionCell cell;
        cell.row_key = textValue(map, evaluation::Field::RowKey);
        cell.column_key = textValue(map, evaluation::Field::ColumnKey);
        cell.row_label = textValue(map, evaluation::Field::RowLabel);
        cell.column_label = textValue(map, evaluation::Field::ColumnLabel);
        cell.count = longValue(map, evaluation::Field::Count);
        cell.row_class_id = intValue(map, evaluation::Field::RowClassId);
        cell.column_class_id = intValue(map, evaluation::Field::ColumnClassId);
        cell.cell_kind = evaluation::cellKindFromKey(textValue(map, evaluation::Field::CellKind));
        cell.selectable = map.value(evaluation::fieldName(evaluation::Field::Selectable)).toBool();
        cell.diagonal = map.value(evaluation::fieldName(evaluation::Field::IsDiagonal)).toBool();
        cell.error = map.value(evaluation::fieldName(evaluation::Field::IsError)).toBool();
        cells.push_back(cell);
    }
    confusion_matrix_->setRecords(std::move(cells));
    QList<QVariantMap> charts;
    for (const QVariant &value : root.value(evaluation::fieldName(evaluation::Field::Charts)).toList())
        charts.push_back(value.toMap());
    charts_->setRecords(std::move(charts));
}

void ModelEvaluationViewModel::loadDerivedCharts()
{
    if (!anomaly_detection_)
        return;

    QList<EvaluationImageRecord> images;
    images.reserve(images_->rowCount());
    for (const EvaluationImageRecord &image : images_->records())
        images.push_back(image);

    QList<QVariantMap> charts = charts_->records();
    charts.push_back(anomalyScoreChartForImages(images));
    charts_->setRecords(std::move(charts));
}

void ModelEvaluationViewModel::loadInstanceRecords(const QVariantList &records)
{
    QSet<QString> event_ids;
    QHash<qint64, const EvaluationImageRecord *> image_index;
    for (const EvaluationImageRecord &image : images_->records())
        image_index.insert(image.image_id, &image);
    std::vector<EvaluationInstanceRecord> values;
    values.reserve(static_cast<size_t>(records.size()));

    for (const QVariant &entry : records)
    {
        EvaluationInstanceRecord value = instanceFromMap(entry.toMap());
        const auto image = image_index.constFind(value.image_id);
        if (image != image_index.cend())
        {
            value.dataset_id = image.value()->dataset_id;
            value.image_name = image.value()->image_name;
            value.image_path = image.value()->image_path;
            value.image_width = image.value()->image_width;
            value.image_height = image.value()->image_height;
        }
        if (value.event_uuid.isEmpty() || event_ids.contains(value.event_uuid))
            continue;
        event_ids.insert(value.event_uuid);
        if (value.gt_class_color.isEmpty())
            value.gt_class_color = classColor(value.gt_class_id);
        if (value.pred_class_color.isEmpty())
            value.pred_class_color = classColor(value.pred_class_id);
        value.thumbnail_url = thumbnailUrl(value);
        values.push_back(std::move(value));
    }
    instances_->setRecords(std::move(values));
}

void ModelEvaluationViewModel::scheduleRebuildFilteredAggregates()
{
    if (suppress_aggregation_rebuild_ || !available_ || aggregation_rebuild_scheduled_)
        return;

    aggregation_rebuild_scheduled_ = true;
    const int token = ++aggregation_schedule_token_;
    QMetaObject::invokeMethod(this,
                              [this, token]()
                              {
                                  if (token != aggregation_schedule_token_)
                                      return;
                                  aggregation_rebuild_scheduled_ = false;
                                  if (!available_)
                                      return;
                                  rebuildFilteredAggregates();
                              },
                              Qt::QueuedConnection);
}

void ModelEvaluationViewModel::rebuildFilteredAggregates()
{
    if (!available_)
        return;

    const int revision = ++aggregation_revision_;
    EvaluationAggregateInput input;
    for (const EvaluationMetricRecord &metric : per_class_metrics_->records())
    {
        if (metric.class_id >= 0)
            input.class_catalog.insert(metric.class_id,
                                       metric.class_name.isEmpty() ? metric.label : metric.class_name);
    }
    for (const EvaluationConfusionCell &cell : confusion_matrix_->records())
    {
        if (cell.row_class_id >= 0 && !cell.row_label.isEmpty() && cell.row_label != QString("合计"))
            input.class_catalog.insert(cell.row_class_id, cell.row_label);
        if (cell.column_class_id >= 0 && !cell.column_label.isEmpty() && cell.column_label != QString("合计"))
            input.class_catalog.insert(cell.column_class_id, cell.column_label);
    }
    if (anomaly_detection_)
    {
        input.class_catalog.insert(0, QStringLiteral("GOOD"));
        input.class_catalog.insert(1, QStringLiteral("Anomaly"));
    }
    input.chart_descriptors = charts_->records();
    input.class_ids = global_filtered_instances_->classIds();
    input.confidence_threshold = confidence_threshold_;
    input.iou_threshold = iou_threshold_;
    input.matching_strategy = evaluation::matchingStrategyFromKey(matching_strategy_);
    input.has_instance_metrics = has_instance_metrics_;
    input.has_image_metrics = has_image_metrics_;
    input.has_confusion_matrix = has_confusion_matrix_;
    input.anomaly_detection = anomaly_detection_;

    // QSortFilterProxyModel remains the single GUI-thread filter boundary.
    // The worker receives only detached value records and never touches a
    // proxy, QModelIndex, QObject or QML object.
    for (const EvaluationInstanceRecord &record : instances_->records())
    {
        if (global_filtered_instances_->acceptsRecord(record))
            input.instances.push_back({record.status, record.gt_class, record.pred_class,
                                      record.gt_class_id, record.pred_class_id});
    }
    const QVariantList dataset_ids = global_filtered_instances_->datasetIds();
    const QVariantList class_ids = global_filtered_instances_->classIds();

    // The image proxy decides whether an image has at least one class that
    // passes the external GlobalFilter, but the image still contains all of
    // its classes.  Detach a class-filtered value record before handing it to
    // the worker so image metrics and PR charts cannot count unrelated
    // classes.  This keeps all QObject/proxy access on the GUI thread.
    bool external_class_filter_enabled = false;
    bool external_class_filter_available = false;
    if (global_filter_ != nullptr && hasInvokable(global_filter_, "isLabelClassFilterEnabled", 0))
    {
        external_class_filter_available = QMetaObject::invokeMethod(
            global_filter_, "isLabelClassFilterEnabled", Qt::DirectConnection,
            Q_RETURN_ARG(bool, external_class_filter_enabled));
    }
    const bool class_filter_active = !class_ids.isEmpty()
        || (external_class_filter_available && external_class_filter_enabled);
    const auto classAllowed = [this, &class_ids, external_class_filter_available,
                               external_class_filter_enabled](const int class_id)
    {
        if (class_id < 0)
            return false;
        if (!class_ids.isEmpty())
        {
            bool selected = false;
            for (const QVariant &value : class_ids)
                selected = selected || value.toInt() == class_id;
            if (!selected)
                return false;
        }
        if (external_class_filter_available && external_class_filter_enabled && global_filter_ != nullptr)
        {
            bool accepted = true;
            if (hasInvokable(global_filter_, "acceptsLabelClassId", 1)
                && QMetaObject::invokeMethod(global_filter_, "acceptsLabelClassId", Qt::DirectConnection,
                                             Q_RETURN_ARG(bool, accepted), Q_ARG(qint64, qint64(class_id))))
                return accepted;
        }
        return true;
    };
    for (const EvaluationImageRecord &record : images_->records())
    {
        if (!filtered_images_->acceptsRecord(record))
            continue;
        if (!dataset_ids.isEmpty())
        {
            bool match = false;
            for (const QVariant &value : dataset_ids)
                match = match || value.toLongLong() == record.dataset_id;
            if (!match)
                continue;
        }
        if (!class_ids.isEmpty())
        {
            bool match = false;
            const QList<int> ground_truth_classes = gtClassIds(record);
            const QList<int> predicted_classes = predClassIds(record, confidence_threshold_);
            const QList<int> relevant_classes
                = ground_truth_classes.isEmpty() ? predicted_classes : ground_truth_classes;
            for (const QVariant &value : class_ids)
                match = match || relevant_classes.contains(value.toInt());
            if (!match)
                continue;
        }
        if (!class_filter_active)
        {
            input.images.push_back(record);
            continue;
        }

        // Keep only selected GT/PRED classes and their corresponding detail
        // records.  A selected class filter is GT-preferred for image
        // visibility (handled by filtered_images_), while unrelated
        // predictions are excluded from the aggregate once the image is in.
        EvaluationImageRecord filtered = record;
        QList<EvaluationGroundTruthRecord> filtered_gt_instances;
        for (const EvaluationGroundTruthRecord &ground_truth : record.gt_instances)
            if (classAllowed(ground_truth.class_id))
                filtered_gt_instances.push_back(ground_truth);
        QList<EvaluationPredictionRecord> filtered_predictions;
        for (const EvaluationPredictionRecord &prediction : record.predictions)
            if (classAllowed(prediction.class_id))
                filtered_predictions.push_back(prediction);

        filtered.gt_instances = std::move(filtered_gt_instances);
        filtered.predictions = std::move(filtered_predictions);
        if (!hasGroundTruth(filtered) && !hasPredictions(filtered, confidence_threshold_))
            continue;
        input.images.push_back(std::move(filtered));
    }

    const QPointer<ModelEvaluationViewModel> guard(this);
    QThreadPool::globalInstance()->start([guard, revision, input = std::move(input)]() mutable
    {
        if (guard.isNull())
            return;
        EvaluationAggregateOutput output = aggregateEvaluation(input);
        QMetaObject::invokeMethod(guard, [guard, revision, output = std::move(output)]() mutable
        {
            if (guard.isNull() || guard->aggregation_revision_ != revision)
                return;
            guard->instance_metrics_->setRecords(std::move(output.instance_metrics));
            guard->image_metrics_->setRecords(std::move(output.image_metrics));
            guard->per_class_metrics_->setRecords(std::move(output.per_class_metrics));
            guard->confusion_matrix_->setRecords(std::move(output.confusion));
            guard->charts_->setRecords(std::move(output.charts));
        });
    });
    return;
}

QVariantMap ModelEvaluationViewModel::instanceToMap(const EvaluationInstanceRecord &record) const
{
    return {{QStringLiteral("eventUuid"), record.event_uuid}, {QStringLiteral("imageId"), record.image_id},
            {QStringLiteral("datasetId"), record.dataset_id}, {QStringLiteral("imageName"), record.image_name},
            {QStringLiteral("imagePath"), record.image_path}, {QStringLiteral("imageWidth"), record.image_width},
            {QStringLiteral("imageHeight"), record.image_height},
            {QStringLiteral("status"), evaluation::statusKey(record.status)},
            {QStringLiteral("statusKind"), static_cast<int>(record.status)},
            {QStringLiteral("statusText"), statusDisplayText(record.status)},
            {QStringLiteral("gtClass"), record.gt_class}, {QStringLiteral("gtClassName"), record.gt_class},
            {QStringLiteral("predClass"), record.pred_class}, {QStringLiteral("predClassName"), record.pred_class},
            {QStringLiteral("gtClassId"), record.gt_class_id}, {QStringLiteral("predClassId"), record.pred_class_id},
            {QStringLiteral("gtLabelId"), record.gt_label_id}, {QStringLiteral("gtInstanceId"), record.gt_instance_id},
            {QStringLiteral("predInstanceId"), record.pred_instance_id},
            {QStringLiteral("gtClassColor"), record.gt_class_color}, {QStringLiteral("predClassColor"), record.pred_class_color},
            {QStringLiteral("thumbnailUrl"), record.thumbnail_url},
            {QStringLiteral("score"), record.score}, {QStringLiteral("predScore"), record.score},
            {QStringLiteral("iou"), record.iou}, {QStringLiteral("selected"), record.selected},
            {QStringLiteral("gtGeometry"), record.gt_geometry},
            {QStringLiteral("predGeometry"), record.pred_geometry},
            {QStringLiteral("gtBounds"), record.gt_bounds}, {QStringLiteral("predBounds"), record.pred_bounds},
            {QStringLiteral("cropBounds"), record.crop_bounds},
            {QStringLiteral("gtOverlayBounds"), record.gt_overlay_bounds},
            {QStringLiteral("predOverlayBounds"), record.pred_overlay_bounds},
            {QStringLiteral("gtOverlayPoints"), record.gt_overlay_points},
            {QStringLiteral("predOverlayPoints"), record.pred_overlay_points},
            {QStringLiteral("gtMaskUrl"), record.gt_mask_url},
            {QStringLiteral("predMaskUrl"), record.pred_mask_url}};
}

QString ModelEvaluationViewModel::thumbnailUrl(const EvaluationInstanceRecord &record) const
{
    if (record.image_path.trimmed().isEmpty())
        return {};

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("event"), record.event_uuid);
    query.addQueryItem(QStringLiteral("revision"), result_revision_);
    query.addQueryItem(QStringLiteral("path"), record.image_path);
    const auto addBounds = [&query, &record](const QString &name, const QString &key)
    {
        const QVariant value = record.crop_bounds.value(key);
        if (value.isValid())
            query.addQueryItem(name, QString::number(value.toDouble(), 'f', 6));
    };
    addBounds(QStringLiteral("x"), QStringLiteral("x"));
    addBounds(QStringLiteral("y"), QStringLiteral("y"));
    addBounds(QStringLiteral("width"), QStringLiteral("width"));
    addBounds(QStringLiteral("height"), QStringLiteral("height"));

    const QString encoded_event = QString::fromLatin1(QUrl::toPercentEncoding(record.event_uuid));
    return QString("image://evaluationthumbnail/%1?%2")
        .arg(encoded_event, query.toString(QUrl::FullyEncoded));
}

void ModelEvaluationViewModel::selectInstance(const int proxyRow)
{
    const auto clearSelection = [this]()
    {
        selected_proxy_row_ = -1;
        selected_instance_.clear();
        if (instances_ != nullptr)
            instances_->setSelectedEvent({});
    };

    if (proxyRow < 0 || proxyRow >= filtered_instances_->rowCount())
    {
        clearSelection();
    }
    else
    {
        selected_proxy_row_ = proxyRow;
        const QModelIndex proxy_index = filtered_instances_->index(proxyRow, 0);
        if (!proxy_index.isValid() || proxy_index.model() != filtered_instances_)
        {
            clearSelection();
            emit selectedInstanceChanged();
            return;
        }
        const QModelIndex global_index = filtered_instances_->mapToSource(proxy_index);
        if (!global_index.isValid() || global_index.model() != global_filtered_instances_)
        {
            clearSelection();
            emit selectedInstanceChanged();
            return;
        }
        const QModelIndex source_index = global_filtered_instances_->mapToSource(global_index);
        if (!source_index.isValid() || source_index.model() != instances_)
        {
            clearSelection();
            emit selectedInstanceChanged();
            return;
        }
        const EvaluationInstanceRecord *record = instances_->recordAt(source_index.row());
        if (record == nullptr)
        {
            clearSelection();
            emit selectedInstanceChanged();
            return;
        }
        selected_instance_ = instanceToMap(*record);
        instances_->setSelectedEvent(record->event_uuid);
    }
    emit selectedInstanceChanged();
}

bool ModelEvaluationViewModel::selectInstance(const QString &eventUuid)
{
    const QString value = eventUuid.trimmed();
    if (value.isEmpty())
    {
        selectInstance(-1);
        return false;
    }
    for (int row = 0; row < filtered_instances_->rowCount(); ++row)
    {
        const QModelIndex index = filtered_instances_->index(row, 0);
        if (filtered_instances_->data(index, EvaluationInstanceModel::EventUuidRole).toString() == value)
        {
            selectInstance(row);
            return true;
        }
    }
    selectInstance(-1);
    return false;
}

void ModelEvaluationViewModel::selectMatrixCell(const QString &rowKey, const QString &columnKey)
{
    const auto normalizeKey = [this](QString value, const bool row)
    {
        value = value.trimmed();
        if (value.isEmpty()
            || value == evaluation::matrixAxisKey(evaluation::MatrixAxisKey::Total)
            || value == evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalseNegative)
            || value == evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalsePositive))
            return value;

        bool numeric = false;
        value.toInt(&numeric);
        if (numeric)
            return value;

        for (const EvaluationConfusionCell &cell : confusion_matrix_->records())
        {
            const QString label = row ? cell.row_label : cell.column_label;
            const QString key = row ? cell.row_key : cell.column_key;
            if (!key.isEmpty() && label.compare(value, Qt::CaseInsensitive) == 0)
                return key;
        }
        return value;
    };

    filtered_instances_->setMatrixRow(normalizeKey(rowKey, true));
    filtered_instances_->setMatrixColumn(normalizeKey(columnKey, false));
    selectInstance(-1);
}

bool ModelEvaluationViewModel::selectConfusionCell(const int row, const int column)
{
    if (confusion_matrix_ == nullptr || row < 0 || column < 0 || row >= confusion_matrix_->rowCount()
        || column >= confusion_matrix_->columnCount())
        return false;
    const QModelIndex index = confusion_matrix_->index(row, column);
    if (!index.isValid() || !index.data(EvaluationConfusionModel::SelectableRole).toBool())
        return false;
    selectMatrixCell(index.data(EvaluationConfusionModel::RowKeyRole).toString(),
                     index.data(EvaluationConfusionModel::ColumnKeyRole).toString());
    return true;
}

void ModelEvaluationViewModel::clearMatrixSelection()
{
    filtered_instances_->setMatrixRow({});
    filtered_instances_->setMatrixColumn({});
}

void ModelEvaluationViewModel::clearConfusionCellFilter()
{
    clearMatrixSelection();
}

void ModelEvaluationViewModel::setDatasetFilter(const QVariantList &datasetIds)
{
    global_filtered_instances_->setDatasetIds(datasetIds);
}

void ModelEvaluationViewModel::setClassFilter(const QVariantList &classIds)
{
    global_filtered_instances_->setClassIds(classIds);
}

void ModelEvaluationViewModel::setPredClassFilter(const qint64 classId)
{
    filtered_instances_->setPredClassIds(classId >= 0 ? QVariantList{classId} : QVariantList{});
}

void ModelEvaluationViewModel::clearPredClassFilter()
{
    filtered_instances_->setPredClassIds({});
}

void ModelEvaluationViewModel::setStatusFilter(const QString &status)
{
    filtered_instances_->setStatus(status);
}

void ModelEvaluationViewModel::clearFilters()
{
    global_filtered_instances_->setDatasetIds({});
    global_filtered_instances_->setClassIds({});
    filtered_instances_->setStatus({});
    filtered_instances_->setPredClassIds({});
    filtered_instances_->setMinScore(-std::numeric_limits<double>::infinity());
    filtered_instances_->setMaxScore(std::numeric_limits<double>::infinity());
    clearMatrixSelection();
}

void ModelEvaluationViewModel::setGlobalFilter(QObject *filter)
{
    if (global_filter_ != nullptr)
        disconnect(global_filter_, nullptr, this, nullptr);
    global_filter_ = filter;
    filtered_images_->setGlobalFilter(filter);
    global_filtered_instances_->setGlobalFilter(filter);
    if (global_filter_ != nullptr)
        connect(global_filter_, SIGNAL(filterChanged()), this, SIGNAL(filterStateChanged()));
    emit filterStateChanged();
}

} // namespace dltool::model
