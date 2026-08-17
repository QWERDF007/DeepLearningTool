#pragma once

#include "dltool/model/Export.h"
#include "model/EvaluationCommon.h"
#include "model/ModelEvaluationModels.h"
#include "model/ModelEvaluationOptions.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml>

namespace dltool::model {

struct EvaluationResult;

/**
 * @brief 单一测试任务的内存评估模型。
 *
 * 评估输入始终从当前测试任务的 test.txt、task.db、项目数据库和 pred 文件读取，
 * 计算在后台线程完成，结果只保留在当前进程的模型缓存中。指标、混淆矩阵和实例
 * 匹配关系不在 QML 中计算；实例过滤由 QSortFilterProxyModel 完成。
 */
class MODEL_API ModelEvaluationViewModel : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ModelEvaluationViewModel)
    QML_UNCREATABLE("ModelEvaluationViewModel is owned by ModelTestTaskManager")

public:
    /**
     * @brief QML 使用的类型化状态值。
     *
     * 字符串状态属性与枚举状态保持一致，状态分支统一依据该枚举。
     */
    enum StateKind
    {
        NotRun = static_cast<int>(evaluation::ViewState::NotRun),
        Loading = static_cast<int>(evaluation::ViewState::Loading),
        Running = static_cast<int>(evaluation::ViewState::Running),
        Failed = static_cast<int>(evaluation::ViewState::Failed),
        MissingResult = static_cast<int>(evaluation::ViewState::MissingResult),
        InvalidResult = static_cast<int>(evaluation::ViewState::InvalidResult),
        Error = static_cast<int>(evaluation::ViewState::Error),
        Ready = static_cast<int>(evaluation::ViewState::Ready),
    };
    Q_ENUM(StateKind)

    Q_PROPERTY(bool available READ available NOTIFY evaluationChanged FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged FINAL)
    Q_PROPERTY(int method READ method CONSTANT FINAL)
    Q_PROPERTY(QString state READ state NOTIFY evaluationChanged FINAL)
    Q_PROPERTY(StateKind stateKind READ stateKind NOTIFY evaluationChanged FINAL)
    Q_PROPERTY(QString error READ error NOTIFY evaluationChanged FINAL)
    Q_PROPERTY(QString primaryMetricSet READ primaryMetricSet NOTIFY evaluationChanged FINAL)
    Q_PROPERTY(bool globalFilterActive READ globalFilterActive NOTIFY filterStateChanged FINAL)
    Q_PROPERTY(QString globalFilterDescription READ globalFilterDescription NOTIFY filterStateChanged FINAL)
    Q_PROPERTY(QString metricScopeDescription READ metricScopeDescription NOTIFY evaluationChanged FINAL)
    Q_PROPERTY(QVariantMap imageMetricDefinition READ imageMetricDefinition NOTIFY evaluationChanged FINAL)
    Q_PROPERTY(QString resultRevision READ resultRevision NOTIFY evaluationChanged FINAL)
    Q_PROPERTY(double confidenceThreshold READ confidenceThreshold NOTIFY evaluationChanged FINAL)
    Q_PROPERTY(double iouThreshold READ iouThreshold NOTIFY evaluationChanged FINAL)
    Q_PROPERTY(QString matchingStrategy READ matchingStrategy NOTIFY evaluationChanged FINAL)
    Q_PROPERTY(bool hasInstanceMetrics READ hasInstanceMetrics NOTIFY evaluationChanged FINAL)
    Q_PROPERTY(bool hasImageMetrics READ hasImageMetrics NOTIFY evaluationChanged FINAL)
    Q_PROPERTY(bool hasConfusionMatrix READ hasConfusionMatrix NOTIFY evaluationChanged FINAL)
    Q_PROPERTY(bool hasInstanceEvents READ hasInstanceEvents NOTIFY evaluationChanged FINAL)
    Q_PROPERTY(bool anomalyDetection READ anomalyDetection NOTIFY evaluationChanged FINAL)
    Q_PROPERTY(EvaluationMetricModel *instanceMetrics READ instanceMetrics CONSTANT FINAL)
    Q_PROPERTY(EvaluationMetricModel *imageMetrics READ imageMetrics CONSTANT FINAL)
    Q_PROPERTY(EvaluationMetricModel *perClassMetrics READ perClassMetrics CONSTANT FINAL)
    Q_PROPERTY(EvaluationMetricSortProxyModel *sortedPerClassMetrics READ sortedPerClassMetrics CONSTANT FINAL)
    Q_PROPERTY(EvaluationConfusionModel *confusionMatrix READ confusionMatrix CONSTANT FINAL)
    Q_PROPERTY(EvaluationImageModel *images READ images CONSTANT FINAL)
    Q_PROPERTY(EvaluationImageFilterProxyModel *filteredImages READ filteredImages CONSTANT FINAL)
    Q_PROPERTY(EvaluationInstanceModel *instances READ instances CONSTANT FINAL)
    Q_PROPERTY(EvaluationGlobalFilterProxyModel *globalFilteredInstances READ globalFilteredInstances CONSTANT FINAL)
    Q_PROPERTY(EvaluationCellFilterProxyModel *filteredInstances READ filteredInstances CONSTANT FINAL)
    Q_PROPERTY(EvaluationChartModel *charts READ charts CONSTANT FINAL)
    Q_PROPERTY(QVariantMap selectedInstance READ selectedInstance NOTIFY selectedInstanceChanged FINAL)
    Q_PROPERTY(QString selectedEventUuid READ selectedEventUuid NOTIFY selectedInstanceChanged FINAL)
    Q_PROPERTY(int selectedInstanceRow READ selectedInstanceRow NOTIFY selectedInstanceChanged FINAL)

public:
    explicit ModelEvaluationViewModel(QObject *parent = nullptr);

    bool                              available() const;
    bool                              loading() const;
    int                               method() const;
    QString                           state() const;
    StateKind                         stateKind() const;
    QString                           error() const;
    QString                           primaryMetricSet() const;
    bool                              globalFilterActive() const;
    QString                           globalFilterDescription() const;
    QString                           metricScopeDescription() const;
    QVariantMap                       imageMetricDefinition() const;
    QString                           resultRevision() const;
    double                            confidenceThreshold() const;
    double                            iouThreshold() const;
    QString                           matchingStrategy() const;
    bool                              hasInstanceMetrics() const;
    bool                              hasImageMetrics() const;
    bool                              hasConfusionMatrix() const;
    bool                              hasInstanceEvents() const;
    bool                              anomalyDetection() const;
    EvaluationMetricModel            *instanceMetrics() const;
    EvaluationMetricModel            *imageMetrics() const;
    EvaluationMetricModel            *perClassMetrics() const;
    EvaluationMetricSortProxyModel   *sortedPerClassMetrics() const;
    EvaluationConfusionModel         *confusionMatrix() const;
    EvaluationImageModel             *images() const;
    EvaluationImageFilterProxyModel  *filteredImages() const;
    EvaluationInstanceModel          *instances() const;
    EvaluationGlobalFilterProxyModel *globalFilteredInstances() const;
    EvaluationCellFilterProxyModel   *filteredInstances() const;
    EvaluationChartModel             *charts() const;
    QVariantMap                       selectedInstance() const;
    QString                           selectedEventUuid() const;
    int                               selectedInstanceRow() const;

    void             setEvaluationOptions(const ModelEvaluationOptions &options);
    Q_INVOKABLE void evaluate(bool notify = false);
    Q_INVOKABLE void refreshEvaluation();
    void             invalidate(evaluation::ViewState state = evaluation::ViewState::NotRun);
    void             setRuntimeState(evaluation::ViewState state);
    Q_INVOKABLE void selectInstance(int proxyRow);
    Q_INVOKABLE bool selectInstance(const QString &eventUuid);
    Q_INVOKABLE void selectMatrixCell(const QString &rowKey, const QString &columnKey);
    Q_INVOKABLE bool selectConfusionCell(int row, int column);
    Q_INVOKABLE void clearMatrixSelection();
    Q_INVOKABLE void clearConfusionCellFilter();
    Q_INVOKABLE void setDatasetFilter(const QVariantList &datasetIds);
    Q_INVOKABLE void setClassFilter(const QVariantList &classIds);
    Q_INVOKABLE void setPredClassFilter(qint64 classId);
    Q_INVOKABLE void clearPredClassFilter();
    Q_INVOKABLE void setStatusFilter(const QString &status);
    Q_INVOKABLE void clearFilters();
    void             setGlobalFilter(QObject *filter);

protected:
    /**
     * @brief 方法特有数据填充钩子（纯虚）。
     *
     * 基类完成公共协议加载后调用；子类在此从强类型结果派生并发出自己的
     * 方法数据变化信号。QML 面板通过运行时子类访问这些扩展属性。
     * @param result 已完成的强类型评估结果。
     */
    virtual void applyMethodSpecificData(const EvaluationResult &result) = 0;

signals:
    void evaluationChanged();
    void loadingChanged();
    void selectedInstanceChanged();
    void filterStateChanged();

private:
    void setLoading(bool value);
    void clearEvaluation(const QString &error = {},
                         evaluation::ViewState state = evaluation::ViewState::NotRun);
    bool sameEvaluationInput(const ModelEvaluationOptions &lhs, const ModelEvaluationOptions &rhs) const;
    void loadEvaluation(const QVariantMap &root);
    void loadInstanceRecords(const QVariantList &records);

    /** 将实例记录序列化为 QML 映射，委托 EvaluationCommon 公共实现。 */
    QVariantMap instanceToMap(const EvaluationInstanceRecord &record) const
    {
        return dltool::model::instanceToMap(record);
    }

    QString thumbnailUrl(const EvaluationInstanceRecord &record) const;
    void    scheduleRebuildFilteredAggregates();
    void    rebuildFilteredAggregates();

    ModelEvaluationOptions            evaluation_options_;
    bool                              has_evaluation_options_{false};
    bool                              evaluation_attempted_{false};
    std::shared_ptr<std::atomic_bool> cancel_token_;
    bool                              notify_when_finished_{false};
    bool                              available_{false};
    bool                              loading_{false};
    int                               method_{static_cast<int>(evaluation::Method::Unknown)};
    QString                           error_;
    evaluation::ViewState             state_kind_{evaluation::ViewState::NotRun};
    QString                           primary_metric_set_;
    QString                           metric_scope_description_;
    QVariantMap                       image_metric_definition_;
    QString                           result_revision_;
    double                            confidence_threshold_{0.0};
    double                            iou_threshold_{0.0};
    QString                           matching_strategy_;
    bool                              has_instance_metrics_{false};
    bool                              has_image_metrics_{false};
    bool                              has_confusion_matrix_{false};
    bool                              has_instance_events_{false};
    bool                              anomaly_detection_{false};
    QPointer<QObject>                 global_filter_;
    EvaluationMetricModel            *instance_metrics_{nullptr};
    EvaluationMetricModel            *image_metrics_{nullptr};
    EvaluationMetricModel            *per_class_metrics_{nullptr};
    EvaluationMetricSortProxyModel   *sorted_per_class_metrics_{nullptr};
    EvaluationConfusionModel         *confusion_matrix_{nullptr};
    EvaluationImageModel             *images_{nullptr};
    EvaluationImageFilterProxyModel  *filtered_images_{nullptr};
    EvaluationInstanceModel          *instances_{nullptr};
    EvaluationGlobalFilterProxyModel *global_filtered_instances_{nullptr};
    EvaluationCellFilterProxyModel   *filtered_instances_{nullptr};
    EvaluationChartModel             *charts_{nullptr};
    int                               selected_proxy_row_{-1};
    QVariantMap                       selected_instance_;
    int                               evaluation_revision_{0};
    int                               aggregation_revision_{0};
    bool                              aggregation_rebuild_scheduled_{false};
    int                               aggregation_schedule_token_{0};
    bool                              suppress_aggregation_rebuild_{false};
};

}
