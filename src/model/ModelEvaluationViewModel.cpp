#include "model/ModelEvaluationViewModel.h"

#include "model/AggregateEvaluation.h"
#include "model/EvaluationCommon.h"
#include "model/EvaluationMatching.h"
#include "model/ModelEvaluationProtocol.h"
#include "model/ModelEvaluationService.h"
#include "ui/SignalHelper.h"

#include <spdlog/spdlog.h>

#include <QDateTime>
#include <QFileInfo>
#include <QMap>
#include <QMetaMethod>
#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <QThreadPool>
#include <QUrl>
#include <QUrlQuery>
#include <QVariantList>
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

int intValue(const QVariantMap &map, const QString &name, const int fallback = -1)
{
    bool      ok    = false;
    const int value = map.value(name).toInt(&ok);
    return ok ? value : fallback;
}

qint64 longValue(const QVariantMap &map, const QString &name, const qint64 fallback = 0)
{
    bool         ok    = false;
    const qint64 value = map.value(name).toLongLong(&ok);
    return ok ? value : fallback;
}

double realValue(const QVariantMap &map, const QString &name, const double fallback = 0.0)
{
    bool         ok    = false;
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
    metric.key               = key;
    metric.label             = fallback_label.isEmpty() ? key : fallback_label;
    metric.class_name        = textValue(map, evaluation::Field::ClassName);
    metric.class_id          = intValue(map, evaluation::Field::ClassId);
    metric.precision         = realValue(map, evaluation::Field::Precision);
    metric.recall            = realValue(map, evaluation::Field::Recall);
    metric.f1                = realValue(map, evaluation::Field::F1);
    metric.tp                = longValue(map, evaluation::Field::Tp);
    metric.fp                = longValue(map, evaluation::Field::Fp);
    metric.fn                = longValue(map, evaluation::Field::Fn);
    metric.precision_defined = map.contains(evaluation::fieldName(evaluation::Field::PrecisionDefined))
                                 ? map.value(evaluation::fieldName(evaluation::Field::PrecisionDefined)).toBool()
                                 : metric.tp + metric.fp > 0;
    metric.recall_defined    = map.contains(evaluation::fieldName(evaluation::Field::RecallDefined))
                                 ? map.value(evaluation::fieldName(evaluation::Field::RecallDefined)).toBool()
                                 : metric.tp + metric.fn > 0;
    metric.f1_defined        = map.contains(evaluation::fieldName(evaluation::Field::F1Defined))
                                 ? map.value(evaluation::fieldName(evaluation::Field::F1Defined)).toBool()
                                 : metric.precision_defined && metric.recall_defined;
    return metric;
}

/**
 * @brief 获取评估专用线程池。
 *
 * 评估耗时且频繁取消重建，与数据集导出、任务准备共用全局线程池会互相
 * 抢占；此处按硬件并发减半创建评估专用池，并用引用计数保持存活。
 * @return 评估专用线程池。
 */
QThreadPool *evaluationPool()
{
    static QThreadPool pool;
    static const int   thread_count = std::max(1, QThread::idealThreadCount() / 2);
    pool.setMaxThreadCount(thread_count);
    return &pool;
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
            [this]() { scheduleRebuildFilteredAggregates(); });
    connect(filtered_instances_, &EvaluationCellFilterProxyModel::filterChanged, this,
            [this]()
            {
                selected_proxy_row_ = -1;
                selected_instance_.clear();
                instances_->setSelectedEvent({});
                emit selectedInstanceChanged();
            });
    connect(filtered_instances_, &QAbstractItemModel::modelReset, this,
            [this]()
            {
                selected_instance_ = {};
                emit selectedInstanceChanged();
            });
    connect(filtered_instances_, &QAbstractItemModel::rowsRemoved, this,
            [this]()
            {
                // Do not touch the source selection model while a proxy is
                // still dispatching rowsRemoved.  GridView can also request
                // the old index during this notification window.
                if (selected_proxy_row_ < 0)
                    return;
                const int selected_row = selected_proxy_row_;
                QMetaObject::invokeMethod(
                    this,
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

bool ModelEvaluationViewModel::available() const
{
    return available_;
}

bool ModelEvaluationViewModel::loading() const
{
    return loading_;
}

QString ModelEvaluationViewModel::state() const
{
    return state_;
}

QString ModelEvaluationViewModel::error() const
{
    return error_;
}

QString ModelEvaluationViewModel::primaryMetricSet() const
{
    return primary_metric_set_;
}

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
                                         Q_RETURN_ARG(QString, description))
            && !description.isEmpty())
            return description;
    }
    return globalFilterActive() ? QString("当前已应用全局过滤") : QString("全部测试样本");
}

QString ModelEvaluationViewModel::metricScopeDescription() const
{
    return metric_scope_description_;
}

QVariantMap ModelEvaluationViewModel::imageMetricDefinition() const
{
    return image_metric_definition_;
}

QString ModelEvaluationViewModel::resultRevision() const
{
    return result_revision_;
}

double ModelEvaluationViewModel::confidenceThreshold() const
{
    return confidence_threshold_;
}

double ModelEvaluationViewModel::iouThreshold() const
{
    return iou_threshold_;
}

QString ModelEvaluationViewModel::matchingStrategy() const
{
    return matching_strategy_;
}

bool ModelEvaluationViewModel::hasInstanceMetrics() const
{
    return has_instance_metrics_;
}

bool ModelEvaluationViewModel::hasImageMetrics() const
{
    return has_image_metrics_;
}

bool ModelEvaluationViewModel::hasConfusionMatrix() const
{
    return has_confusion_matrix_;
}

bool ModelEvaluationViewModel::hasInstanceEvents() const
{
    return has_instance_events_;
}

EvaluationMetricModel *ModelEvaluationViewModel::instanceMetrics() const
{
    return instance_metrics_;
}

EvaluationMetricModel *ModelEvaluationViewModel::imageMetrics() const
{
    return image_metrics_;
}

EvaluationMetricModel *ModelEvaluationViewModel::perClassMetrics() const
{
    return per_class_metrics_;
}

EvaluationMetricSortProxyModel *ModelEvaluationViewModel::sortedPerClassMetrics() const
{
    return sorted_per_class_metrics_;
}

EvaluationConfusionModel *ModelEvaluationViewModel::confusionMatrix() const
{
    return confusion_matrix_;
}

EvaluationImageModel *ModelEvaluationViewModel::images() const
{
    return images_;
}

EvaluationImageFilterProxyModel *ModelEvaluationViewModel::filteredImages() const
{
    return filtered_images_;
}

EvaluationInstanceModel *ModelEvaluationViewModel::instances() const
{
    return instances_;
}

EvaluationGlobalFilterProxyModel *ModelEvaluationViewModel::globalFilteredInstances() const
{
    return global_filtered_instances_;
}

EvaluationCellFilterProxyModel *ModelEvaluationViewModel::filteredInstances() const
{
    return filtered_instances_;
}

EvaluationChartModel *ModelEvaluationViewModel::charts() const
{
    return charts_;
}

QVariantMap ModelEvaluationViewModel::selectedInstance() const
{
    return selected_instance_;
}

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
    available_                     = false;
    error_                         = error;
    state_ = state.isEmpty() ? (error.isEmpty() ? evaluation::viewStateKey(evaluation::ViewState::NotRun)
                                                : evaluation::viewStateKey(evaluation::ViewState::Error))
                             : state;
    primary_metric_set_.clear();
    metric_scope_description_.clear();
    image_metric_definition_.clear();
    result_revision_.clear();
    confidence_threshold_ = 0.0;
    iou_threshold_        = 0.0;
    matching_strategy_.clear();
    has_instance_metrics_                            = false;
    has_image_metrics_                               = false;
    has_confusion_matrix_                            = false;
    has_instance_events_                             = false;
    anomaly_detection_                               = false;
    const bool previous_suppress_aggregation_rebuild = suppress_aggregation_rebuild_;
    suppress_aggregation_rebuild_                    = true;
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
    return lhs.model_uuid == rhs.model_uuid && lhs.test_task_uuid == rhs.test_task_uuid
        && lhs.model_name == rhs.model_name && lhs.task_directory == rhs.task_directory && lhs.method == rhs.method
        && lhs.project_database_path == rhs.project_database_path
        && lhs.dataset_file_list_path == rhs.dataset_file_list_path && lhs.task_database_path == rhs.task_database_path
        && lhs.prediction_dir == rhs.prediction_dir && lhs.evaluation_config == rhs.evaluation_config
        && qFuzzyCompare(lhs.confidence_threshold + 1.0, rhs.confidence_threshold + 1.0)
        && qFuzzyCompare(lhs.iou_threshold + 1.0, rhs.iou_threshold + 1.0)
        && lhs.matching_strategy == rhs.matching_strategy;
}

void ModelEvaluationViewModel::setEvaluationOptions(const ModelEvaluationOptions &options)
{
    if (has_evaluation_options_ && sameEvaluationInput(evaluation_options_, options))
        return;
    evaluation_options_     = options;
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

    const int revision    = ++evaluation_revision_;
    notify_when_finished_ = notify;
    clearEvaluation();
    cancel_token_                  = std::make_shared<std::atomic_bool>(false);
    ModelEvaluationOptions options = evaluation_options_;
    options.cancel_token           = cancel_token_;
    setLoading(true);

    const QPointer<ModelEvaluationViewModel> guard(this);
    // 评估提交到专用线程池，避免与数据集导出/任务准备抢占全局池。
    evaluationPool()->start(
        [guard, revision, options, notify]()
        {
            if (guard.isNull())
                return;

            ModelEvaluationResult evaluation_result;
            QString               error;
            const bool            success = ModelEvaluationService::evaluate(options, &evaluation_result, &error);
            QMetaObject::invokeMethod(
                guard.data(),
                [guard, revision, options, notify, success, result = std::move(evaluation_result.evaluation_data),
                 error]() mutable
                {
                    if (guard.isNull() || guard->evaluation_revision_ != revision)
                        return;

                    const bool should_notify     = guard->notify_when_finished_ || notify;
                    guard->notify_when_finished_ = false;
                    if (!success)
                    {
                        guard->evaluation_attempted_ = true;
                        const QString message        = error.isEmpty() ? QString("C++ 评估失败") : error;
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
                    guard->loadInstanceRecords(
                        result.value(evaluation::fieldName(evaluation::Field::InstanceRecords)).toList());
                    guard->loadDerivedCharts();
                    guard->evaluation_attempted_ = true;
                    guard->available_            = true;
                    guard->state_                = evaluation::viewStateKey(evaluation::ViewState::Ready);
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
                },
                Qt::QueuedConnection);
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
    const evaluation::MetricSet metric_set = evaluation::metricSetFromKey(
        root.value(evaluation::fieldName(evaluation::Field::PrimaryMetricSet)).toString());
    primary_metric_set_ = evaluation::metricSetKey(metric_set);
    metric_scope_description_
        = metric_set == evaluation::MetricSet::Official ? QString("官方指标") : QString("诊断匹配指标");
    image_metric_definition_ = root.value(evaluation::fieldName(evaluation::Field::ImageMetricDefinition)).toMap();
    const QVariantMap evaluation_config
        = root.value(evaluation::fieldName(evaluation::Field::EvaluationConfig)).toMap();
    anomaly_detection_    = evaluation::isAnomaly(evaluation_options_.method);
    confidence_threshold_ = realValue(evaluation_config, evaluation::Field::ConfidenceThreshold);
    iou_threshold_        = realValue(evaluation_config, evaluation::Field::IouThreshold);
    matching_strategy_    = evaluation::matchingStrategyKey(
        evaluation::matchingStrategyFromKey(textValue(evaluation_config, evaluation::Field::MatchingStrategy)));
    const QVariantMap capabilities = root.value(evaluation::fieldName(evaluation::Field::Capabilities)).toMap();
    has_instance_metrics_ = capabilities.value(evaluation::fieldName(evaluation::Field::HasInstanceMetrics)).toBool();
    has_image_metrics_    = capabilities.value(evaluation::fieldName(evaluation::Field::HasImageMetrics)).toBool();
    has_confusion_matrix_ = capabilities.value(evaluation::fieldName(evaluation::Field::HasConfusionMatrix)).toBool()
                         || anomaly_detection_;
    // Anomaly results are image-level, but the instance grid still consumes
    // the one in-memory C++ event per image so matrix selections can show
    // GOOD/Anomaly samples, including true negatives with no original event.
    has_instance_events_ = capabilities.value(evaluation::fieldName(evaluation::Field::HasInstanceEvents)).toBool()
                        || anomaly_detection_;
    const QVariantMap diagnostic = root.value(evaluation::fieldName(evaluation::Field::DiagnosticMetrics)).toMap();
    const QVariantMap instance   = diagnostic.value(evaluation::fieldName(evaluation::Field::Instance)).toMap();
    const QVariantMap overall    = instance.value(evaluation::fieldName(evaluation::Field::Overall)).toMap();
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
        const QVariantMap official_instance
            = official.value(evaluation::fieldName(evaluation::Field::Instance)).toMap();
        if (!official_instance.isEmpty())
            instance_metrics_->setRecords(
                {metricFromMap(QStringLiteral("overall"), official_instance, QString("整体"))});
        const QVariantMap official_image = official.value(evaluation::fieldName(evaluation::Field::Image)).toMap();
        if (!official_image.isEmpty())
            image_metrics_->setRecords({metricFromMap(QStringLiteral("image"), official_image, QString("图像"))});
    }

    std::vector<EvaluationImageRecord> image_records;
    for (const QVariant &value : root.value(evaluation::fieldName(evaluation::Field::ImageRecords)).toList())
        image_records.push_back(imageFromMap(value.toMap()));
    images_->setRecords(std::move(image_records));

    std::vector<EvaluationMetricRecord> per_class;
    for (const QVariant &value : instance.value(evaluation::fieldName(evaluation::Field::PerClass)).toList())
    {
        const QVariantMap map = value.toMap();
        const QString     key = map.value(evaluation::fieldName(evaluation::Field::ClassId)).toString();
        per_class.push_back(
            metricFromMap(key, map, map.value(evaluation::fieldName(evaluation::Field::ClassName)).toString()));
    }
    per_class_metrics_->setRecords(std::move(per_class));

    std::vector<EvaluationConfusionCell> cells;
    const QVariantMap  matrix       = root.value(evaluation::fieldName(evaluation::Field::ConfusionMatrix)).toMap();
    const QVariantList matrix_cells = matrix.value(evaluation::fieldName(evaluation::Field::Cells)).toList();
    for (const QVariant &value : matrix_cells)
    {
        const QVariantMap       map = value.toMap();
        EvaluationConfusionCell cell;
        cell.row_key         = textValue(map, evaluation::Field::RowKey);
        cell.column_key      = textValue(map, evaluation::Field::ColumnKey);
        cell.row_label       = textValue(map, evaluation::Field::RowLabel);
        cell.column_label    = textValue(map, evaluation::Field::ColumnLabel);
        cell.count           = longValue(map, evaluation::Field::Count);
        cell.row_class_id    = intValue(map, evaluation::Field::RowClassId);
        cell.column_class_id = intValue(map, evaluation::Field::ColumnClassId);
        cell.cell_kind       = evaluation::cellKindFromKey(textValue(map, evaluation::Field::CellKind));
        cell.selectable      = map.value(evaluation::fieldName(evaluation::Field::Selectable)).toBool();
        cell.diagonal        = map.value(evaluation::fieldName(evaluation::Field::IsDiagonal)).toBool();
        cell.error           = map.value(evaluation::fieldName(evaluation::Field::IsError)).toBool();
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
    for (const EvaluationImageRecord &image : images_->records()) images.push_back(image);

    QList<QVariantMap> charts = charts_->records();
    charts.push_back(anomalyScoreChartForImages(images));
    charts_->setRecords(std::move(charts));
}

void ModelEvaluationViewModel::loadInstanceRecords(const QVariantList &records)
{
    QSet<QString>                                event_ids;
    QHash<qint64, const EvaluationImageRecord *> image_index;
    for (const EvaluationImageRecord &image : images_->records()) image_index.insert(image.image_id, &image);
    std::vector<EvaluationInstanceRecord> values;
    values.reserve(static_cast<size_t>(records.size()));

    for (const QVariant &entry : records)
    {
        EvaluationInstanceRecord value = instanceFromMap(entry.toMap());
        const auto               image = image_index.constFind(value.image_id);
        if (image != image_index.cend())
        {
            value.dataset_id   = image.value()->dataset_id;
            value.image_name   = image.value()->image_name;
            value.image_path   = image.value()->image_path;
            value.image_width  = image.value()->image_width;
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
    const int token                = ++aggregation_schedule_token_;
    QMetaObject::invokeMethod(
        this,
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

    const int                revision = ++aggregation_revision_;
    EvaluationAggregateInput input;
    for (const EvaluationMetricRecord &metric : per_class_metrics_->records())
    {
        if (metric.class_id >= 0)
            input.class_catalog.insert(metric.class_id, metric.class_name.isEmpty() ? metric.label : metric.class_name);
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
    input.chart_descriptors    = charts_->records();
    input.class_ids            = global_filtered_instances_->classIds();
    input.confidence_threshold = confidence_threshold_;
    input.iou_threshold        = iou_threshold_;
    input.matching_strategy    = evaluation::matchingStrategyFromKey(matching_strategy_);
    input.has_instance_metrics = has_instance_metrics_;
    input.has_image_metrics    = has_image_metrics_;
    input.has_confusion_matrix = has_confusion_matrix_;
    input.anomaly_detection    = anomaly_detection_;

    // QSortFilterProxyModel remains the single GUI-thread filter boundary.
    // The worker receives only detached value records and never touches a
    // proxy, QModelIndex, QObject or QML object.
    for (const EvaluationInstanceRecord &record : instances_->records())
    {
        if (global_filtered_instances_->acceptsRecord(record))
            input.instances.push_back(
                {record.status, record.gt_class, record.pred_class, record.gt_class_id, record.pred_class_id});
    }
    const QVariantList dataset_ids = global_filtered_instances_->datasetIds();
    const QVariantList class_ids   = global_filtered_instances_->classIds();

    // The image proxy decides whether an image has at least one class that
    // passes the external GlobalFilter, but the image still contains all of
    // its classes.  Detach a class-filtered value record before handing it to
    // the worker so image metrics and PR charts cannot count unrelated
    // classes.  This keeps all QObject/proxy access on the GUI thread.
    bool external_class_filter_enabled   = false;
    bool external_class_filter_available = false;
    if (global_filter_ != nullptr && hasInvokable(global_filter_, "isLabelClassFilterEnabled", 0))
    {
        external_class_filter_available
            = QMetaObject::invokeMethod(global_filter_, "isLabelClassFilterEnabled", Qt::DirectConnection,
                                        Q_RETURN_ARG(bool, external_class_filter_enabled));
    }
    const bool class_filter_active
        = !class_ids.isEmpty() || (external_class_filter_available && external_class_filter_enabled);
    const auto classAllowed
        = [this, &class_ids, external_class_filter_available, external_class_filter_enabled](const int class_id)
    {
        if (class_id < 0)
            return false;
        if (!class_ids.isEmpty())
        {
            bool selected = false;
            for (const QVariant &value : class_ids) selected = selected || value.toInt() == class_id;
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
            for (const QVariant &value : dataset_ids) match = match || value.toLongLong() == record.dataset_id;
            if (!match)
                continue;
        }
        if (!class_ids.isEmpty())
        {
            bool             match                = false;
            const QList<int> ground_truth_classes = gtClassIds(record);
            const QList<int> predicted_classes    = predClassIds(record, confidence_threshold_);
            const QList<int> relevant_classes
                = ground_truth_classes.isEmpty() ? predicted_classes : ground_truth_classes;
            for (const QVariant &value : class_ids) match = match || relevant_classes.contains(value.toInt());
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
        EvaluationImageRecord              filtered = record;
        QList<EvaluationGroundTruthRecord> filtered_gt_instances;
        for (const EvaluationGroundTruthRecord &ground_truth : record.gt_instances)
            if (classAllowed(ground_truth.class_id))
                filtered_gt_instances.push_back(ground_truth);
        QList<EvaluationPredictionRecord> filtered_predictions;
        for (const EvaluationPredictionRecord &prediction : record.predictions)
            if (classAllowed(prediction.class_id))
                filtered_predictions.push_back(prediction);

        filtered.gt_instances = std::move(filtered_gt_instances);
        filtered.predictions  = std::move(filtered_predictions);
        if (!hasGroundTruth(filtered) && !hasPredictions(filtered, confidence_threshold_))
            continue;
        input.images.push_back(std::move(filtered));
    }

    const QPointer<ModelEvaluationViewModel> guard(this);
    QThreadPool::globalInstance()->start(
        [guard, revision, input = std::move(input)]() mutable
        {
            if (guard.isNull())
                return;
            EvaluationAggregateOutput output = aggregateEvaluation(input);
            QMetaObject::invokeMethod(guard,
                                      [guard, revision, output = std::move(output)]() mutable
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
    return QString("image://evaluationthumbnail/%1?%2").arg(encoded_event, query.toString(QUrl::FullyEncoded));
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
        selected_proxy_row_           = proxyRow;
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
        if (value.isEmpty() || value == evaluation::matrixAxisKey(evaluation::MatrixAxisKey::Total)
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
            const QString key   = row ? cell.row_key : cell.column_key;
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
