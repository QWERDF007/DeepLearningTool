#include "model/ModelEvaluationViewModel.h"

#include "model/AggregateEvaluation.h"
#include "model/EvaluationCommon.h"
#include "model/EvaluationDataset.h"
#include "model/EvaluationEngineRegistry.h"
#include "model/EvaluationMatching.h"
#include "model/EvaluationResult.h"
#include "model/ModelEvaluationProtocol.h"
#include "database/ModelTaskDataBase.h"
#include "ui/SignalHelper.h"

#include <spdlog/spdlog.h>

#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
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

EvaluationMetricRecord metricFromCounts(const QString &key, const QString &label, const int class_id,
                                        const EvaluationCounts &counts)
{
    EvaluationMetricRecord metric = metricFromMap(key, evaluationMetricMap(counts.tp, counts.fp, counts.fn), label);
    metric.class_id               = class_id;
    metric.class_name             = label;
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

bool validEvaluationResult(const EvaluationResult &result, QString *error)
{
    const auto fail = [error](const QString &message)
    {
        if (error != nullptr)
            *error = message;
        return false;
    };
    if (result.method == evaluation::Method::Unknown)
        return fail(QStringLiteral("评估结果缺少有效 method"));
    if (!std::isfinite(result.confidence_threshold) || !std::isfinite(result.iou_threshold))
        return fail(QStringLiteral("评估结果包含无效阈值"));
    if (result.official_metrics.isEmpty())
        return fail(QStringLiteral("评估结果缺少 official_metrics"));
    if (result.charts.size() != result.chart_kinds.size())
        return fail(QStringLiteral("评估结果的 charts 与 chart_kinds 数量不一致"));
    return true;
}

} // namespace

ModelEvaluationViewModel::ModelEvaluationViewModel(QObject *parent)
    : QObject(parent)
    , instance_metrics_(new EvaluationMetricModel(this))
    , image_metrics_(new EvaluationMetricModel(this))
    , per_class_metrics_(new EvaluationMetricModel(this))
    , confusion_matrix_(new EvaluationConfusionModel(this))
    , images_(new EvaluationImageModel(this))
    , filtered_images_(new EvaluationImageFilterProxyModel(this))
    , instances_(new EvaluationInstanceModel(this))
    , global_filtered_instances_(new EvaluationGlobalFilterProxyModel(this))
    , filtered_instances_(new EvaluationCellFilterProxyModel(this))
    , charts_(new EvaluationChartModel(this))
{
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
                // 代理发送 rowsRemoved 期间不操作源选择模型。
                // GridView 可能在该通知窗口内继续请求旧索引。
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

ModelEvaluationViewModel::~ModelEvaluationViewModel()
{
    if (cancel_token_ != nullptr)
        cancel_token_->store(true, std::memory_order_relaxed);
}

void ModelEvaluationViewModel::shutdownEvaluationWorkers()
{
    evaluationPool()->waitForDone();
}

bool ModelEvaluationViewModel::available() const
{
    return available_;
}

bool ModelEvaluationViewModel::loading() const
{
    return loading_;
}

bool ModelEvaluationViewModel::hasPredictionResults() const
{
    QElapsedTimer timer;
    timer.start();
    const auto finish = [this, &timer](const bool value)
    {
        spdlog::debug("[评估耗时] 任务 {} has-prediction-results 完成: {} ms, result={}",
                      evaluation_options_.test_task_uuid.toUtf8().constData(), timer.elapsed(), value);
        return value;
    };
    if (!has_evaluation_options_ || !QFileInfo::exists(evaluation_options_.dataset_file_list_path))
        return finish(false);

    if (evaluation::isAnomaly(evaluation_options_.method))
    {
        if (evaluation_options_.prediction_dir.trimmed().isEmpty())
            return finish(false);

        QList<QPair<qint64, QString>> rows;
        QString                      error;
        if (!readEvaluationImageList(evaluation_options_.dataset_file_list_path, rows, {}, &error))
            return finish(false);

        const QDir prediction_dir(evaluation_options_.prediction_dir);
        return finish(std::any_of(rows.cbegin(), rows.cend(), [&prediction_dir](const auto &row)
                                  {
                                      return QFileInfo(prediction_dir.filePath(
                                          QStringLiteral("%1.tiff").arg(row.first)))
                                          .isFile();
                                  }));
    }

    if (!QFileInfo(evaluation_options_.task_database_path).isFile())
        return finish(false);

    database::ModelTaskDataBase task_database(evaluation_options_.task_database_path);
    QHash<qint64, QVariant> predictions;
    QString                  error;
    return finish(task_database.readPredictions(predictions, &error) && !predictions.isEmpty());
}

int ModelEvaluationViewModel::method() const
{
    return method_;
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

double ModelEvaluationViewModel::confidenceThreshold() const
{
    return confidence_threshold_;
}

double ModelEvaluationViewModel::iouThreshold() const
{
    return iou_threshold_;
}

bool ModelEvaluationViewModel::hasBestThreshold() const
{
    return best_threshold_available_;
}

double ModelEvaluationViewModel::bestThreshold() const
{
    return best_threshold_available_ ? best_threshold_ : 0.0;
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
    aggregation_matches_evaluation_result_ = true;
    available_                     = false;
    error_                         = error;
    state_kind_ = error.isEmpty() && state == evaluation::ViewState::NotRun ? evaluation::ViewState::NotRun : state;
    if (!error.isEmpty() && state == evaluation::ViewState::NotRun)
        state_kind_ = evaluation::ViewState::Error;
    primary_metric_set_.clear();
    metric_scope_description_.clear();
    image_metric_definition_.clear();
    prediction_snapshot_.clear();
    threshold_search_ = {};
    confidence_threshold_ = 0.0;
    iou_threshold_        = 0.0;
    best_threshold_available_ = false;
    best_threshold_           = 0.0;
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
        && lhs.prediction_dir == rhs.prediction_dir && lhs.prediction_snapshot == rhs.prediction_snapshot
        && lhs.preprocessing_config == rhs.preprocessing_config
        && lhs.evaluation_config == rhs.evaluation_config
        && qFuzzyCompare(lhs.confidence_threshold + 1.0, rhs.confidence_threshold + 1.0)
        && qFuzzyCompare(lhs.iou_threshold + 1.0, rhs.iou_threshold + 1.0)
        && lhs.matching_strategy == rhs.matching_strategy
        && lhs.apply_best_threshold == rhs.apply_best_threshold;
}

void ModelEvaluationViewModel::adoptEvaluationThreshold(const double threshold, const QString &prediction_snapshot)
{
    if (!std::isfinite(threshold) || !has_evaluation_options_)
        return;

    const QString parameter_name
        = evaluation::isAnomaly(evaluation_options_.method) ? QStringLiteral("classification_threshold")
                                                             : QStringLiteral("conf");
    evaluation_options_.confidence_threshold = threshold;
    evaluation_options_.evaluation_config.insert(parameter_name, threshold);
    evaluation_options_.evaluation_config
        = evaluation::normalizedEvaluationConfig(evaluation_options_.evaluation_config);
    evaluation_options_.apply_best_threshold = false;
    if (!prediction_snapshot.isEmpty())
        evaluation_options_.prediction_snapshot = prediction_snapshot;
}

void ModelEvaluationViewModel::setEvaluationOptions(const ModelEvaluationOptions &options)
{
    if (has_evaluation_options_ && sameEvaluationInput(evaluation_options_, options))
        return;
    evaluation_options_     = options;
    has_evaluation_options_ = true;
    method_                 = static_cast<int>(options.method);
    if (loading_)
    {
        // Evaluation requests are serialized. Keep only the newest input and
        // let the active worker finish before starting it.
        pending_evaluation_ = true;
        return;
    }
    invalidate();
}

void ModelEvaluationViewModel::invalidate(const evaluation::ViewState state)
{
    const bool worker_active = evaluation_worker_active_;
    evaluation_attempted_ = false;
    notify_when_finished_ = false;
    pending_evaluation_   = false;
    pending_notify_when_finished_ = false;
    if (cancel_token_ != nullptr)
    {
        cancel_token_->store(true, std::memory_order_relaxed);
        discard_active_result_ = true;
    }
    clearEvaluation({}, state);
    if (!worker_active)
    {
        cancel_token_.reset();
        discard_active_result_ = false;
    }
    setLoading(worker_active);
    emit evaluationChanged();
    emit selectedInstanceChanged();
}

void ModelEvaluationViewModel::evaluate(const bool notify)
{
    if (!has_evaluation_options_)
    {
        if (loading_)
        {
            pending_evaluation_ = true;
            pending_notify_when_finished_ = pending_notify_when_finished_ || notify;
            return;
        }
        invalidate(evaluation::ViewState::MissingResult);
        return;
    }
    if (loading_)
    {
        pending_evaluation_          = true;
        pending_notify_when_finished_ = pending_notify_when_finished_ || notify;
        return;
    }
    if (evaluation_attempted_)
        return;

    startEvaluation(notify);
}

void ModelEvaluationViewModel::startEvaluation(const bool notify)
{
    if (evaluation_worker_active_)
    {
        pending_evaluation_           = true;
        pending_notify_when_finished_ = pending_notify_when_finished_ || notify;
        return;
    }
    if (!has_evaluation_options_)
    {
        clearEvaluation({}, evaluation::ViewState::MissingResult);
        setLoading(false);
        emit evaluationChanged();
        emit selectedInstanceChanged();
        return;
    }

    // 尚未开始测试或文件列表被清理时没有可评估的输入，显示“还没有可评估的预测结果”，
    // 而不是当作后台评估失败。
    if (!QFileInfo(evaluation_options_.dataset_file_list_path).isFile())
    {
        invalidate(evaluation::ViewState::MissingResult);
        return;
    }

    notify_when_finished_ = notify;
    clearEvaluation();
    discard_active_result_         = false;
    evaluation_worker_active_      = true;
    cancel_token_                  = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> request_token = cancel_token_;
    ModelEvaluationOptions options = evaluation_options_;
    options.cancel_token           = request_token;
    setLoading(true);

    const QPointer<ModelEvaluationViewModel> guard(this);
    // 评估提交到专用线程池，避免与数据集导出/任务准备抢占全局池。
    evaluationPool()->start(
        [guard, request_token, options, notify]()
        {
            if (guard.isNull())
                return;

            QElapsedTimer worker_timer;
            worker_timer.start();
            spdlog::debug("[评估耗时] 任务 {} worker 开始", options.test_task_uuid.toUtf8().constData());
            auto    result = std::make_shared<EvaluationResult>();
            QString error;
            bool    success = false;
            if (auto engine = EvaluationEngineRegistry::instance().createEngine(options.method))
                success = engine->evaluate(options, result.get(), &error);
            else
                error = QString("未注册的评估方法: %1").arg(static_cast<int>(options.method));
            const qint64 worker_elapsed = worker_timer.elapsed();
            spdlog::debug("[评估耗时] 任务 {} worker 完成: {} ms, success={}, images={}, events={}, charts={}",
                          options.test_task_uuid.toUtf8().constData(), worker_elapsed, success, result->images.size(),
                          result->instance_records.size(), result->charts.size());
            QMetaObject::invokeMethod(
                guard.data(),
                [guard, request_token, options, notify, success, worker_elapsed, result = std::move(result), error]() mutable
                {
                    if (guard.isNull() || !guard->evaluation_worker_active_ || guard->cancel_token_ != request_token)
                        return;

                    guard->evaluation_worker_active_ = false;
                    guard->cancel_token_.reset();
                    const auto startPendingEvaluation = [&guard]()
                    {
                        if (guard.isNull() || !guard->pending_evaluation_)
                            return false;
                        const bool next_notify
                            = guard->notify_when_finished_ || guard->pending_notify_when_finished_;
                        guard->pending_evaluation_           = false;
                        guard->pending_notify_when_finished_ = false;
                        guard->discard_active_result_         = false;
                        guard->startEvaluation(next_notify);
                        return true;
                    };

                    // A newer request owns the visible result. Do not publish
                    // an intermediate success or failure before starting it.
                    if (guard->pending_evaluation_)
                    {
                        startPendingEvaluation();
                        return;
                    }

                    if (guard->discard_active_result_ || request_token->load(std::memory_order_relaxed))
                    {
                        guard->discard_active_result_ = false;
                        guard->notify_when_finished_   = false;
                        guard->setLoading(false);
                        emit guard->evaluationChanged();
                        emit guard->selectedInstanceChanged();
                        return;
                    }

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

                    QString           validation_error;
                    if (!validEvaluationResult(*result, &validation_error))
                    {
                        guard->evaluation_attempted_ = true;
                        const QString message
                            = validation_error.isEmpty() ? QStringLiteral("评估结果格式无效") : validation_error;
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

                    QElapsedTimer gui_timer;
                    gui_timer.start();
                    guard->prediction_snapshot_ = options.prediction_snapshot;
                    guard->loadEvaluation(*result);
                    spdlog::debug("[评估耗时] 任务 {} GUI load-evaluation 完成: {} ms",
                                  options.test_task_uuid.toUtf8().constData(), gui_timer.elapsed());
                    gui_timer.restart();
                    guard->loadInstanceRecords(result->instance_records);
                    spdlog::debug("[评估耗时] 任务 {} GUI load-instance-records 完成: {} ms",
                                  options.test_task_uuid.toUtf8().constData(), gui_timer.elapsed());
                    guard->applyMethodSpecificData(*result);
                    guard->evaluation_attempted_ = true;
                    guard->available_            = true;
                    guard->state_kind_           = evaluation::ViewState::Ready;
                    guard->error_.clear();
                    guard->aggregation_matches_evaluation_result_ = true;
                    guard->scheduleRebuildFilteredAggregates();
                    guard->setLoading(false);
                    emit guard->evaluationChanged();
                    emit guard->selectedInstanceChanged();
                    emit guard->evaluationCompleted();
                    spdlog::debug("[评估耗时] 任务 {} GUI result-publish 完成: {} ms, worker={} ms",
                                  options.test_task_uuid.toUtf8().constData(), gui_timer.elapsed(), worker_elapsed);
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
    if (loading_)
    {
        pending_evaluation_ = true;
        return;
    }
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

void ModelEvaluationViewModel::loadEvaluation(const EvaluationResult &result)
{
    QElapsedTimer timer;
    timer.start();
    const bool official_available
        = result.official_metrics.value(evaluation::fieldName(evaluation::Field::Available)).toBool();
    primary_metric_set_ = official_available ? evaluation::metricSetKey(evaluation::MetricSet::Official)
                                             : evaluation::metricSetKey(evaluation::MetricSet::Diagnostic);
    metric_scope_description_ = official_available ? QStringLiteral("官方指标") : QStringLiteral("诊断匹配指标");
    image_metric_definition_   = result.image_metric_definition;
    anomaly_detection_         = evaluation::isAnomaly(result.method);
    confidence_threshold_      = result.confidence_threshold;
    iou_threshold_             = result.iou_threshold;
    threshold_search_          = result.threshold_search;
    best_threshold_available_  = threshold_search_.available;
    best_threshold_            = best_threshold_available_ ? threshold_search_.best_point.threshold : 0.0;
    matching_strategy_         = evaluation::matchingStrategyKey(result.matching_strategy);
    class_catalog_             = result.class_catalog;
    class_colors_              = result.class_colors;
    has_instance_metrics_      = result.has_instance_metrics;
    has_image_metrics_         = result.has_image_metrics;
    has_confusion_matrix_      = result.has_confusion_matrix || anomaly_detection_;
    has_instance_events_       = result.has_instance_events || anomaly_detection_;

    QMap<int, double> class_ap_map;
    for (const QVariantMap &chart : result.charts)
    {
        const QVariantList datasets
            = chart.value(evaluation::fieldName(evaluation::Field::Data)).toMap()
                  .value(evaluation::fieldName(evaluation::Field::Datasets))
                  .toList();
        for (const QVariant &value : datasets)
        {
            const QVariantMap dataset = value.toMap();
            if (dataset.contains(QStringLiteral("average_precision"))
                && dataset.contains(evaluation::fieldName(evaluation::Field::ClassId)))
                class_ap_map.insert(dataset.value(evaluation::fieldName(evaluation::Field::ClassId)).toInt(),
                                    dataset.value(QStringLiteral("average_precision")).toDouble());
        }
    }
    class_ap_map_ = class_ap_map;

    std::vector<EvaluationMetricRecord> per_class;
    if (has_instance_metrics_)
    {
        per_class.reserve(static_cast<size_t>(class_catalog_.size()));
        for (auto it = class_catalog_.cbegin(); it != class_catalog_.cend(); ++it)
        {
            EvaluationMetricRecord record = metricFromCounts(QString::number(it.key()), it.value(), it.key(),
                                                             result.per_class.value(it.key()));
            if (class_ap_map.contains(it.key()))
            {
                record.ap         = class_ap_map.value(it.key());
                record.ap_defined = true;
            }
            record.class_color = classColor(it.key());
            per_class.push_back(std::move(record));
        }
    }
    per_class_metrics_->setRecords(std::move(per_class));

    if (has_instance_metrics_)
        instance_metrics_->setRecords(
            {metricFromCounts(QStringLiteral("overall"), QStringLiteral("整体"), -1, result.overall)});
    else
        instance_metrics_->setRecords({});
    if (has_image_metrics_)
        image_metrics_->setRecords({metricFromCounts(QStringLiteral("image"), QStringLiteral("图像"), -1,
                                                     result.image_counts)});
    else
        image_metrics_->setRecords({});

    if (official_available)
    {
        const QVariantMap official_instance
            = result.official_metrics.value(evaluation::fieldName(evaluation::Field::Instance)).toMap();
        if (!official_instance.isEmpty())
            instance_metrics_->setRecords(
                {metricFromMap(QStringLiteral("overall"), official_instance, QStringLiteral("整体"))});
        const QVariantMap official_image
            = result.official_metrics.value(evaluation::fieldName(evaluation::Field::Image)).toMap();
        if (!official_image.isEmpty())
            image_metrics_->setRecords({metricFromMap(QStringLiteral("image"), official_image, QStringLiteral("图像"))});
    }

    std::vector<EvaluationImageRecord> image_records;
    image_records.reserve(static_cast<size_t>(result.images.size()));
    for (const EvaluationImageData &image : result.images)
        image_records.push_back(image);
    images_->setRecords(std::move(image_records));

    std::vector<EvaluationConfusionCell> cells;
    cells.reserve(static_cast<size_t>(result.matrix_cells.size()));
    for (const EvaluationConfusionCell &cell : result.matrix_cells)
        cells.push_back(cell);
    confusion_matrix_->setRecords(std::move(cells));
    charts_->setRecords(result.charts);
    spdlog::debug("[评估耗时] 任务 {} GUI loadEvaluation 完成: {} ms, images={}, matrix={}, charts={}",
                  evaluation_options_.test_task_uuid.toUtf8().constData(), timer.elapsed(), result.images.size(),
                  result.matrix_cells.size(), result.charts.size());
}

void ModelEvaluationViewModel::loadInstanceRecords(const QVector<EvaluationInstanceRecord> &records)
{
    QElapsedTimer timer;
    timer.start();
    QSet<QString>                                event_ids;
    QHash<qint64, const EvaluationImageRecord *> image_index;
    for (const EvaluationImageRecord &image : images_->records()) image_index.insert(image.id, &image);
    std::vector<EvaluationInstanceRecord> values;
    values.reserve(static_cast<size_t>(records.size()));

    for (const EvaluationInstanceRecord &entry : records)
    {
        EvaluationInstanceRecord value = entry;
        const auto               image = image_index.constFind(value.image_id);
        if (image != image_index.cend())
        {
            value.dataset_id   = image.value()->dataset_id;
            value.image_name   = image.value()->name;
            value.image_path   = image.value()->path;
            if (image.value()->width > 0 && image.value()->height > 0)
            {
                value.image_width  = image.value()->width;
                value.image_height = image.value()->height;
            }
        }
        if (value.event_uuid.isEmpty() || event_ids.contains(value.event_uuid))
            continue;
        event_ids.insert(value.event_uuid);
        if (value.gt_class_color.isEmpty())
            value.gt_class_color = classColor(value.gt_class_id);
        if (value.pred_class_color.isEmpty())
        {
            if (anomaly_detection_)
                value.pred_class_color = value.pred_class_id == 1 ? QStringLiteral("red") : QStringLiteral("green");
            else
                value.pred_class_color = classColor(value.pred_class_id);
        }
        value.thumbnail_url = thumbnailUrl(value);
        values.push_back(std::move(value));
    }
    const int loaded_count = static_cast<int>(values.size());
    instances_->setRecords(std::move(values));
    spdlog::debug("[评估耗时] 任务 {} GUI loadInstanceRecords 完成: {} ms, input={}, loaded={}",
                  evaluation_options_.test_task_uuid.toUtf8().constData(), timer.elapsed(), records.size(),
                  loaded_count);
}

void ModelEvaluationViewModel::scheduleRebuildFilteredAggregates()
{
    if (suppress_aggregation_rebuild_ || !available_ || aggregation_rebuild_scheduled_)
        return;

    if (!hasActiveAggregationFilters() && aggregation_matches_evaluation_result_)
    {
        spdlog::debug("[评估耗时] 任务 {} 跳过无筛选聚合：直接复用主评估结果",
                      evaluation_options_.test_task_uuid.toUtf8().constData());
        return;
    }

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

bool ModelEvaluationViewModel::hasActiveAggregationFilters() const
{
    if (global_filtered_instances_ != nullptr
        && (!global_filtered_instances_->datasetIds().isEmpty() || !global_filtered_instances_->classIds().isEmpty()))
        return true;

    if (global_filter_ == nullptr)
        return false;
    if (!hasInvokable(global_filter_, "isActive", 0))
        return true;

    bool active = false;
    QMetaObject::invokeMethod(global_filter_, "isActive", Qt::DirectConnection, Q_RETURN_ARG(bool, active));
    return active;
}

void ModelEvaluationViewModel::rebuildFilteredAggregates()
{
    if (!available_)
        return;

    QElapsedTimer aggregate_timer;
    aggregate_timer.start();
    const int                revision = ++aggregation_revision_;
    const QString            task_uuid = evaluation_options_.test_task_uuid;
    EvaluationAggregateInput input;
    input.class_catalog = class_catalog_;
    for (const EvaluationMetricRecord &metric : per_class_metrics_->records())
    {
        if (metric.class_id >= 0)
            input.class_catalog.insert(metric.class_id, metric.class_name.isEmpty() ? metric.label : metric.class_name);
    }
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
    input.threshold_search     = threshold_search_;

    // QSortFilterProxyModel 是 GUI 线程唯一的过滤边界。
    // 工作线程只接收脱离 QObject 的值记录，不访问代理、QModelIndex、
    // QObject 或 QML 对象。
    for (const EvaluationInstanceRecord &record : instances_->records())
    {
        if (global_filtered_instances_->acceptsRecord(record))
            input.instances.push_back(
                {record.status, record.gt_class, record.pred_class, record.gt_class_id, record.pred_class_id});
    }
    const QVariantList dataset_ids = global_filtered_instances_->datasetIds();
    const QVariantList class_ids   = global_filtered_instances_->classIds();

    // 在提交聚合前复制经过类别筛选的值记录。
    // 图像代理只决定图像是否可见，工作线程还必须剔除图像中未选中的
    // 类别，避免图像指标和阈值图表计入无关类别；代理访问仍限定在 GUI 线程。
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
    bool external_filter_active = false;
    if (global_filter_ != nullptr)
    {
        if (!hasInvokable(global_filter_, "isActive", 0))
            external_filter_active = true;
        else
            QMetaObject::invokeMethod(global_filter_, "isActive", Qt::DirectConnection,
                                      Q_RETURN_ARG(bool, external_filter_active));
    }
    input.threshold_search_is_complete
        = dataset_ids.isEmpty() && !class_filter_active && !external_filter_active;
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

        // 只保留选中类别及其对应的 GT/预测详情。
        // 图像可见性由 filtered_images_ 按 GT 优先决定，图像进入聚合后仍需排除无关预测。
        EvaluationImageRecord              filtered = record;
        QList<EvaluationGroundTruthRecord> filtered_gt;
        for (const EvaluationGroundTruthRecord &ground_truth : record.gt)
            if (classAllowed(ground_truth.class_id))
                filtered_gt.push_back(ground_truth);
        QList<EvaluationPredictionRecord> filtered_predictions;
        for (const EvaluationPredictionRecord &prediction : record.predictions)
            if (classAllowed(prediction.class_id))
                filtered_predictions.push_back(prediction);

        filtered.gt          = std::move(filtered_gt);
        filtered.predictions = std::move(filtered_predictions);
        rebuildImageDerivedValues(filtered);
        if (!hasGroundTruth(filtered) && !hasPredictions(filtered, confidence_threshold_))
            continue;
        input.images.push_back(std::move(filtered));
    }

    const qint64 input_build_elapsed = aggregate_timer.elapsed();
    spdlog::debug("[评估耗时] 任务 {} GUI aggregate-input 完成: {} ms, images={}, instances={}, class_filter={}, "
                  "dataset_filter={}",
                  task_uuid.toUtf8().constData(), input_build_elapsed, input.images.size(), input.instances.size(),
                  class_filter_active, !dataset_ids.isEmpty());

    const QPointer<ModelEvaluationViewModel> guard(this);
    spdlog::debug("[评估耗时] 任务 {} aggregate-submit", task_uuid.toUtf8().constData());
    QThreadPool::globalInstance()->start(
        [guard, revision, task_uuid, input = std::move(input)]() mutable
        {
            if (guard.isNull())
                return;
            spdlog::debug("[评估耗时] 任务 {} aggregate-worker 开始", task_uuid.toUtf8().constData());
            QElapsedTimer worker_timer;
            worker_timer.start();
            EvaluationAggregateOutput output = aggregateEvaluation(input);
            spdlog::debug("[评估耗时] 任务 {} aggregate-worker 完成: {} ms, images={}, instances={}, charts={}",
                          task_uuid.toUtf8().constData(), worker_timer.elapsed(), input.images.size(),
                          input.instances.size(), output.charts.size());
            QMetaObject::invokeMethod(guard,
                                      [guard, revision, task_uuid, output = std::move(output)]() mutable
                                      {
                                          if (guard.isNull() || guard->aggregation_revision_ != revision)
                                              return;
                                          QElapsedTimer apply_timer;
                                          apply_timer.start();
                                          for (auto &rec : output.per_class_metrics)
                                          {
                                              if (guard->class_ap_map_.contains(rec.class_id))
                                              {
                                                  rec.ap         = guard->class_ap_map_.value(rec.class_id);
                                                  rec.ap_defined = true;
                                              }
                                          }
                                          guard->instance_metrics_->setRecords(std::move(output.instance_metrics));
                                          guard->image_metrics_->setRecords(std::move(output.image_metrics));
                                          guard->per_class_metrics_->setRecords(std::move(output.per_class_metrics));
                                          guard->confusion_matrix_->setRecords(std::move(output.confusion));
                                          guard->charts_->setRecords(std::move(output.charts));
                                          guard->aggregation_matches_evaluation_result_
                                              = !guard->hasActiveAggregationFilters();
                                          spdlog::debug("[评估耗时] 任务 {} GUI aggregate-apply 完成: {} ms",
                                                        task_uuid.toUtf8().constData(), apply_timer.elapsed());
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
    query.addQueryItem(QStringLiteral("snapshot"), prediction_snapshot_);
    query.addQueryItem(QStringLiteral("path"), record.image_path);
    // 裁剪视口由 provider 根据 GT/PRED 绝对 bounds 在渲染时推导。
    // 评估阶段无需再次依赖图像宽高。
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

QString ModelEvaluationViewModel::heatmapThumbnailUrl(const qint64 imageId, const QString &imagePath,
                                                       const QString &scoreMapPath, double threshold) const
{
    if (imagePath.trimmed().isEmpty() || scoreMapPath.trimmed().isEmpty())
        return {};

    if (!std::isfinite(threshold) || threshold <= 0.0)
        threshold = 1.0;
    threshold = std::clamp(threshold, 0.0001, 1000.0);

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("snapshot"), prediction_snapshot_);
    query.addQueryItem(QStringLiteral("imageId"), QString::number(imageId));
    query.addQueryItem(QStringLiteral("path"), imagePath);
    query.addQueryItem(QStringLiteral("scorePath"), scoreMapPath);
    query.addQueryItem(QStringLiteral("heatmap"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("heatmapThreshold"), QString::number(threshold, 'f', 6));

    const QByteArray preprocessing = QJsonDocument::fromVariant(evaluation_options_.preprocessing_config)
                                         .toJson(QJsonDocument::Compact);
    query.addQueryItem(QStringLiteral("preprocessing"), QString::fromUtf8(preprocessing));

    const QString encoded_id = QString::fromLatin1(QUrl::toPercentEncoding(QString::number(imageId)));
    return QString("image://evaluationthumbnail/heatmap-%1?%2").arg(encoded_id, query.toString(QUrl::FullyEncoded));
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

QString ModelEvaluationViewModel::classColor(const int class_id) const
{
    if (class_colors_.contains(class_id))
        return class_colors_.value(class_id);
    return dltool::model::classColor(class_id);
}

} // namespace dltool::model
