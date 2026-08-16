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
#include <QMetaType>
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

EvaluationMetricRecord metricFromMap(const QString &key, const QVariantMap &map, const QString &label_override = {})
{
    EvaluationMetricRecord metric;
    metric.key               = key;
    metric.label             = label_override.isEmpty() ? key : label_override;
    metric.class_name        = recordText(map, evaluation::Field::ClassName);
    metric.class_id          = recordInt(map, evaluation::Field::ClassId);
    metric.precision         = recordReal(map, evaluation::Field::Precision);
    metric.recall            = recordReal(map, evaluation::Field::Recall);
    metric.f1                = recordReal(map, evaluation::Field::F1);
    metric.tp                = recordLong(map, evaluation::Field::Tp);
    metric.fp                = recordLong(map, evaluation::Field::Fp);
    metric.fn                = recordLong(map, evaluation::Field::Fn);
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

/** @brief 将评估协议中的图像记录转换为界面模型使用的值对象。 */
EvaluationImageRecord imageRecordFromMap(const QVariantMap &map)
{
    EvaluationImageRecord record;
    record.id         = recordLong(map, evaluation::Field::ImageId, -1);
    record.dataset_id = recordLong(map, evaluation::Field::DatasetId, -1);
    record.name       = recordText(map, evaluation::Field::ImageName);
    record.path       = recordText(map, evaluation::Field::ImagePath);
    record.width      = recordInt(map, evaluation::Field::ImageWidth, 0);
    record.height     = recordInt(map, evaluation::Field::ImageHeight, 0);

    for (const QVariant &value : map.value(evaluation::fieldName(evaluation::Field::GtInstances)).toList())
    {
        const QVariantMap gt_map = value.toMap();
        EvaluationGroundTruthData gt;
        gt.label_id   = recordLong(gt_map, evaluation::Field::LabelId, -1);
        gt.class_id   = recordInt(gt_map, evaluation::Field::ClassId, -1);
        gt.class_name = recordText(gt_map, evaluation::Field::ClassName);
        gt.anomaly    = gt_map.value(evaluation::fieldName(evaluation::Field::IsAnomaly)).toBool();
        gt.geometry   = gt_map.value(evaluation::fieldName(evaluation::Field::Geometry)).toMap();
        if (readBox(gt.geometry, gt.box))
            gt.bounds = evaluationBoxMap(gt.box);
        record.gt.push_back(std::move(gt));
    }

    for (const QVariant &value : map.value(evaluation::fieldName(evaluation::Field::Predictions)).toList())
    {
        const QVariantMap prediction_map = value.toMap();
        EvaluationPredictionData prediction;
        prediction.prediction_id = recordText(prediction_map, evaluation::Field::PredictionId);
        prediction.image_id      = record.id;
        prediction.class_id      = recordInt(prediction_map, evaluation::Field::ClassId, -1);
        prediction.class_name    = recordText(prediction_map, evaluation::Field::ClassName);
        prediction.score         = recordReal(prediction_map, evaluation::Field::Score);
        prediction.geometry      = prediction_map.value(evaluation::fieldName(evaluation::Field::Geometry)).toMap();
        if (readBox(prediction.geometry, prediction.box))
            prediction.bounds = evaluationBoxMap(prediction.box);
        record.predictions.push_back(std::move(prediction));
    }
    return record;
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

bool validEvaluationResult(const QVariantMap &root, QString *error)
{
    const auto fail = [error](const QString &message)
    {
        if (error != nullptr)
            *error = message;
        return false;
    };
    if (root.isEmpty())
        return fail(QStringLiteral("评估结果为空"));

    const QString primary_metric_set = evaluation::fieldName(evaluation::Field::PrimaryMetricSet);
    if (!root.contains(primary_metric_set) || root.value(primary_metric_set).toString().trimmed().isEmpty())
        return fail(QStringLiteral("评估结果缺少 primary_metric_set"));

    const auto requireMap = [&root, &fail](const evaluation::Field field, const char *name)
    {
        const QString key = evaluation::fieldName(field);
        if (!root.contains(key) || root.value(key).metaType().id() != QMetaType::QVariantMap)
            return fail(QStringLiteral("评估结果缺少或无效字段: %1").arg(QString::fromLatin1(name)));
        return true;
    };
    const auto requireList = [&root, &fail](const evaluation::Field field, const char *name)
    {
        const QString key = evaluation::fieldName(field);
        if (!root.contains(key) || root.value(key).metaType().id() != QMetaType::QVariantList)
            return fail(QStringLiteral("评估结果缺少或无效字段: %1").arg(QString::fromLatin1(name)));
        return true;
    };

    if (!requireMap(evaluation::Field::EvaluationConfig, "evaluation_config")
        || !requireMap(evaluation::Field::DiagnosticMetrics, "diagnostic_metrics")
        || !requireMap(evaluation::Field::Capabilities, "capabilities")
        || !requireMap(evaluation::Field::ConfusionMatrix, "confusion_matrix")
        || !requireList(evaluation::Field::ImageRecords, "image_records")
        || !requireList(evaluation::Field::InstanceRecords, "instance_records")
        || !requireList(evaluation::Field::Charts, "charts"))
        return false;

    const QVariantMap confusion = root.value(evaluation::fieldName(evaluation::Field::ConfusionMatrix)).toMap();
    if (!confusion.contains(evaluation::fieldName(evaluation::Field::Cells))
        || confusion.value(evaluation::fieldName(evaluation::Field::Cells)).metaType().id() != QMetaType::QVariantList)
        return fail(QStringLiteral("评估结果缺少或无效字段: confusion_matrix.cells"));
    return true;
}

}

ModelEvaluationViewModel::ModelEvaluationViewModel(QObject *parent)
    : QObject(parent)
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
                /**
                 * @brief 代理发送 rowsRemoved 期间不操作源选择模型。
                 *
                 * GridView 可能在该通知窗口内继续请求旧索引。
                 */
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
    return evaluation::viewStateKey(state_kind_);
}

ModelEvaluationViewModel::StateKind ModelEvaluationViewModel::stateKind() const
{
    return static_cast<StateKind>(state_kind_);
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

bool ModelEvaluationViewModel::anomalyDetection() const
{
    return anomaly_detection_;
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
        state_kind_ = evaluation::ViewState::Loading;
    else if (state_kind_ == evaluation::ViewState::Loading)
        state_kind_ = available_ ? evaluation::ViewState::Ready
                                 : (error_.isEmpty() ? evaluation::ViewState::NotRun : evaluation::ViewState::Error);
    emit loadingChanged();
}

void ModelEvaluationViewModel::clearEvaluation(const QString &error, const evaluation::ViewState state)
{
    ++aggregation_revision_;
    ++aggregation_schedule_token_;
    aggregation_rebuild_scheduled_ = false;
    available_                     = false;
    error_                         = error;
    state_kind_ = error.isEmpty() && state == evaluation::ViewState::NotRun ? evaluation::ViewState::NotRun : state;
    if (!error.isEmpty() && state == evaluation::ViewState::NotRun)
        state_kind_ = evaluation::ViewState::Error;
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
    /*
     * confusion_matrix_ 是多个同步 TableView 共同观察的稳定展示快照。
     * available_/has_confusion_matrix_ 已经使旧结果不可见；保留旧快照直到
     * 新结果一次性替换，可避免重新评估时产生 4xN -> 0x0 -> 4xN 的布局抖动。
     */
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

void ModelEvaluationViewModel::invalidate(const evaluation::ViewState state)
{
    ++evaluation_revision_;
    evaluation_attempted_ = false;
    notify_when_finished_ = false;
    if (cancel_token_ != nullptr)
        cancel_token_->store(true, std::memory_order_relaxed);
    cancel_token_.reset();
    clearEvaluation({}, state);
    setLoading(false);
    emit evaluationChanged();
    emit selectedInstanceChanged();
}

void ModelEvaluationViewModel::evaluate(const bool notify)
{
    if (!has_evaluation_options_)
    {
        invalidate(evaluation::ViewState::MissingResult);
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
        invalidate(evaluation::ViewState::MissingResult);
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

            QVariantMap evaluation_result;
            QString      error;
            const bool   success = ModelEvaluationService::evaluate(options, &evaluation_result, &error);
            QMetaObject::invokeMethod(
                guard.data(),
                [guard, revision, options, notify, success, result = std::move(evaluation_result),
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
                        guard->clearEvaluation(message, evaluation::ViewState::Error);
                        guard->setLoading(false);
                        emit guard->evaluationChanged();
                        emit guard->selectedInstanceChanged();
                        if (should_notify)
                            ui::SignalHelper::notifyError(QString("模型评估失败"), message);
                        return;
                    }

                    QString validation_error;
                    if (!validEvaluationResult(result, &validation_error))
                    {
                        guard->evaluation_attempted_ = true;
                        const QString message = validation_error.isEmpty() ? QStringLiteral("评估结果格式无效")
                                                                            : validation_error;
                        spdlog::error("测试任务 {} 评估结果无效: {}", options.test_task_uuid.toUtf8().constData(),
                                      message.toUtf8().constData());
                        guard->clearEvaluation(message, evaluation::ViewState::InvalidResult);
                        guard->setLoading(false);
                        emit guard->evaluationChanged();
                        emit guard->selectedInstanceChanged();
                        if (should_notify)
                            ui::SignalHelper::notifyError(QStringLiteral("模型评估结果无效"), message);
                        return;
                    }

                    guard->result_revision_ = QString::number(QDateTime::currentMSecsSinceEpoch());
                    guard->loadEvaluation(result);
                    guard->loadInstanceRecords(
                        result.value(evaluation::fieldName(evaluation::Field::InstanceRecords)).toList());
                    guard->evaluation_attempted_ = true;
                    guard->available_            = true;
                    guard->state_kind_           = evaluation::ViewState::Ready;
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

void ModelEvaluationViewModel::setRuntimeState(const evaluation::ViewState state)
{
    if (state_kind_ == state && !available_ && error_.isEmpty())
        return;
    if (state == evaluation::ViewState::Running || state == evaluation::ViewState::Failed
        || state == evaluation::ViewState::NotRun)
        invalidate(state);
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
    confidence_threshold_ = recordReal(evaluation_config, evaluation::Field::ConfidenceThreshold);
    iou_threshold_        = recordReal(evaluation_config, evaluation::Field::IouThreshold);
    matching_strategy_    = evaluation::matchingStrategyKey(
        evaluation::matchingStrategyFromKey(recordText(evaluation_config, evaluation::Field::MatchingStrategy)));
    const QVariantMap capabilities = root.value(evaluation::fieldName(evaluation::Field::Capabilities)).toMap();
    has_instance_metrics_ = capabilities.value(evaluation::fieldName(evaluation::Field::HasInstanceMetrics)).toBool();
    has_image_metrics_    = capabilities.value(evaluation::fieldName(evaluation::Field::HasImageMetrics)).toBool();
    has_confusion_matrix_ = capabilities.value(evaluation::fieldName(evaluation::Field::HasConfusionMatrix)).toBool()
                         || anomaly_detection_;
    /**
     * @brief 异常结果按图像评估，但实例表仍消费每图像一条内存事件。
     *
     * 这样矩阵选择可以展示正常/异常样本，也能覆盖没有原始事件的真负图像。
     */
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

    /**
     * @brief 官方指标和诊断指标保持分离。
     *
     * 主指标集合只改变总览面板显示的数值，矩阵和事件始终使用诊断记录。
     */
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
    const QVariantList serialized_images
        = root.value(evaluation::fieldName(evaluation::Field::ImageRecords)).toList();
    image_records.reserve(static_cast<size_t>(serialized_images.size()));
    for (const QVariant &value : serialized_images)
        image_records.push_back(imageRecordFromMap(value.toMap()));
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
        cell.row_key         = recordText(map, evaluation::Field::RowKey);
        cell.column_key      = recordText(map, evaluation::Field::ColumnKey);
        cell.row_label       = recordText(map, evaluation::Field::RowLabel);
        cell.column_label    = recordText(map, evaluation::Field::ColumnLabel);
        cell.count           = recordLong(map, evaluation::Field::Count);
        cell.row_class_id    = recordInt(map, evaluation::Field::RowClassId);
        cell.column_class_id = recordInt(map, evaluation::Field::ColumnClassId);
        cell.cell_kind       = evaluation::cellKindFromKey(recordText(map, evaluation::Field::CellKind));
        cell.selectable      = map.value(evaluation::fieldName(evaluation::Field::Selectable)).toBool();
        cell.diagonal        = map.value(evaluation::fieldName(evaluation::Field::IsDiagonal)).toBool();
        cell.error           = map.value(evaluation::fieldName(evaluation::Field::IsError)).toBool();
        cells.push_back(cell);
    }
    QList<QVariantMap> charts;
    for (const QVariant &value : root.value(evaluation::fieldName(evaluation::Field::Charts)).toList())
        charts.push_back(value.toMap());
    confusion_matrix_->setRecords(std::move(cells));
    charts_->setRecords(std::move(charts));
}

void ModelEvaluationViewModel::loadInstanceRecords(const QVariantList &records)
{
    QSet<QString>                                event_ids;
    QHash<qint64, const EvaluationImageRecord *> image_index;
    for (const EvaluationImageRecord &image : images_->records()) image_index.insert(image.id, &image);
    std::vector<EvaluationInstanceRecord> values;
    values.reserve(static_cast<size_t>(records.size()));

    for (const QVariant &entry : records)
    {
        EvaluationInstanceRecord value = instanceFromMap(entry.toMap());
        const auto               image = image_index.constFind(value.image_id);
        if (image != image_index.cend())
        {
            value.dataset_id   = image.value()->dataset_id;
            value.image_name   = image.value()->name;
            value.image_path   = image.value()->path;
            value.image_width  = image.value()->width;
            value.image_height = image.value()->height;
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
    // 异常检测的矩阵行是内部二元预测类别（正常/异常），GT 类别目录只取
    // Service 返回的全局按类别指标，避免过滤重算时把 0/1 混入 GT 列。
    if (!anomaly_detection_)
    {
        for (const EvaluationConfusionCell &cell : confusion_matrix_->records())
        {
            if (cell.row_class_id >= 0 && !cell.row_label.isEmpty())
                input.class_catalog.insert(cell.row_class_id, cell.row_label);
            if (cell.column_class_id >= 0 && !cell.column_label.isEmpty())
                input.class_catalog.insert(cell.column_class_id, cell.column_label);
        }
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

    /**
     * @brief QSortFilterProxyModel 是 GUI 线程唯一的过滤边界。
     *
     * 工作线程只接收脱离 QObject 的值记录，不访问代理、QModelIndex、
     * QObject 或 QML 对象。
     */
    for (const EvaluationInstanceRecord &record : instances_->records())
    {
        if (global_filtered_instances_->acceptsRecord(record))
            input.instances.push_back(
                {record.status, record.gt_class, record.pred_class, record.gt_class_id, record.pred_class_id});
    }
    const QVariantList dataset_ids = global_filtered_instances_->datasetIds();
    const QVariantList class_ids   = global_filtered_instances_->classIds();

    /**
     * @brief 在提交聚合前复制经过类别筛选的值记录。
     *
     * 图像代理只决定图像是否可见，工作线程还必须剔除图像中未选中的
     * 类别，避免图像指标和阈值图表计入无关类别；代理访问仍限定在 GUI 线程。
     */
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

        /**
         * @brief 只保留选中类别及其对应的 GT/预测详情。
         *
         * 图像可见性由 filtered_images_ 按 GT 优先决定，图像进入聚合后
         * 仍需排除无关预测。
         */
        EvaluationImageRecord              filtered = record;
        QList<EvaluationGroundTruthRecord> filtered_gt;
        for (const EvaluationGroundTruthRecord &ground_truth : record.gt)
            if (classAllowed(ground_truth.class_id))
                filtered_gt.push_back(ground_truth);
        QList<EvaluationPredictionRecord> filtered_predictions;
        for (const EvaluationPredictionRecord &prediction : record.predictions)
            if (classAllowed(prediction.class_id))
                filtered_predictions.push_back(prediction);

        filtered.gt           = std::move(filtered_gt);
        filtered.predictions  = std::move(filtered_predictions);
        rebuildImageDerivedValues(filtered);
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
    /**
     * @brief 裁剪视口由 provider 根据 GT/PRED 绝对 bounds 在渲染时推导。
     *
     * 评估阶段无需再次依赖图像宽高。
     */
    const auto addBounds = [&query](const QString &prefix, const QVariantMap &bounds)
    {
        const QVariant x = bounds.value(QStringLiteral("x"));
        const QVariant y = bounds.value(QStringLiteral("y"));
        const QVariant w = bounds.value(QStringLiteral("width"));
        const QVariant h = bounds.value(QStringLiteral("height"));
        if (!x.isValid() || !y.isValid() || !w.isValid() || !h.isValid())
            return;
        query.addQueryItem(prefix + QStringLiteral("_x"), QString::number(x.toDouble(), 'f', 6));
        query.addQueryItem(prefix + QStringLiteral("_y"), QString::number(y.toDouble(), 'f', 6));
        query.addQueryItem(prefix + QStringLiteral("_w"), QString::number(w.toDouble(), 'f', 6));
        query.addQueryItem(prefix + QStringLiteral("_h"), QString::number(h.toDouble(), 'f', 6));
    };
    addBounds(QStringLiteral("gt"), record.gt_bounds);
    addBounds(QStringLiteral("pd"), record.pred_bounds);

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
            || value == evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalsePositive)
            || value == evaluation::matrixAxisKey(evaluation::MatrixAxisKey::UnmatchedGroundTruth)
            || value == evaluation::matrixAxisKey(evaluation::MatrixAxisKey::UnmatchedPrediction))
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

}
