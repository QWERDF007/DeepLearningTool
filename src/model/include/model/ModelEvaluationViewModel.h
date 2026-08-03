#pragma once

#include "dltool/model/Export.h"
#include "model/ModelEvaluationModels.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml>
#include <QPointer>
#include <QList>

namespace dltool::model {

/**
 * @brief 评估报告的展示模型。
 *
 * 该类只解析稳定的 YAML 协议并把纯值记录交给 Qt Model。指标、混淆矩阵和实例
 * 匹配关系不在 QML 中计算；实例过滤由 QSortFilterProxyModel 完成。
 */
class MODEL_API ModelEvaluationViewModel : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ModelEvaluationViewModel)
    QML_UNCREATABLE("ModelEvaluationViewModel is owned by ModelTestTaskManager")

    Q_PROPERTY(bool available READ available NOTIFY reportChanged FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged FINAL)
    Q_PROPERTY(QString state READ state NOTIFY reportChanged FINAL)
    Q_PROPERTY(QString error READ error NOTIFY reportChanged FINAL)
    Q_PROPERTY(QString reportPath READ reportPath WRITE setReportPath NOTIFY reportPathChanged FINAL)
    Q_PROPERTY(QString resultPath READ resultPath WRITE setResultPath NOTIFY resultPathChanged FINAL)
    Q_PROPERTY(QString primaryMetricSet READ primaryMetricSet NOTIFY reportChanged FINAL)
    Q_PROPERTY(bool globalFilterActive READ globalFilterActive NOTIFY filterStateChanged FINAL)
    Q_PROPERTY(QString globalFilterDescription READ globalFilterDescription NOTIFY filterStateChanged FINAL)
    Q_PROPERTY(QString metricScopeDescription READ metricScopeDescription NOTIFY reportChanged FINAL)
    Q_PROPERTY(QVariantMap imageMetricDefinition READ imageMetricDefinition NOTIFY reportChanged FINAL)
    Q_PROPERTY(QString resultRevision READ resultRevision NOTIFY reportChanged FINAL)
    Q_PROPERTY(double confidenceThreshold READ confidenceThreshold NOTIFY reportChanged FINAL)
    Q_PROPERTY(double iouThreshold READ iouThreshold NOTIFY reportChanged FINAL)
    Q_PROPERTY(QString matchingStrategy READ matchingStrategy NOTIFY reportChanged FINAL)
    Q_PROPERTY(bool hasInstanceMetrics READ hasInstanceMetrics NOTIFY reportChanged FINAL)
    Q_PROPERTY(bool hasImageMetrics READ hasImageMetrics NOTIFY reportChanged FINAL)
    Q_PROPERTY(bool hasConfusionMatrix READ hasConfusionMatrix NOTIFY reportChanged FINAL)
    Q_PROPERTY(bool hasInstanceEvents READ hasInstanceEvents NOTIFY reportChanged FINAL)
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

    bool available() const;
    bool loading() const;
    QString state() const;
    QString error() const;
    QString reportPath() const;
    void setReportPath(const QString &path);
    QString resultPath() const;
    void setResultPath(const QString &path);
    void setPaths(const QString &reportPath, const QString &resultPath);
    QString primaryMetricSet() const;
    bool globalFilterActive() const;
    QString globalFilterDescription() const;
    QString metricScopeDescription() const;
    QVariantMap imageMetricDefinition() const;
    QString resultRevision() const;
    double confidenceThreshold() const;
    double iouThreshold() const;
    QString matchingStrategy() const;
    bool hasInstanceMetrics() const;
    bool hasImageMetrics() const;
    bool hasConfusionMatrix() const;
    bool hasInstanceEvents() const;
    EvaluationMetricModel *instanceMetrics() const;
    EvaluationMetricModel *imageMetrics() const;
    EvaluationMetricModel *perClassMetrics() const;
    EvaluationMetricSortProxyModel *sortedPerClassMetrics() const;
    EvaluationConfusionModel *confusionMatrix() const;
    EvaluationImageModel *images() const;
    EvaluationImageFilterProxyModel *filteredImages() const;
    EvaluationInstanceModel *instances() const;
    EvaluationGlobalFilterProxyModel *globalFilteredInstances() const;
    EvaluationCellFilterProxyModel *filteredInstances() const;
    EvaluationChartModel *charts() const;
    QVariantMap selectedInstance() const;
    QString selectedEventUuid() const;
    int selectedInstanceRow() const;

    Q_INVOKABLE void reload();
    Q_INVOKABLE void refresh();
    void setRuntimeState(const QString &state);
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
    void setGlobalFilter(QObject *filter);

signals:
    void reportPathChanged();
    void resultPathChanged();
    void reportChanged();
    void loadingChanged();
    void selectedInstanceChanged();
    void filterStateChanged();

private:
    void setLoading(bool value);
    void clearReport(const QString &error = {}, const QString &state = {});
    void loadReport(const QVariantMap &root);
    void loadInstanceRecords(const QVariantList &records);
    QVariantMap instanceToMap(const EvaluationInstanceRecord &record) const;
    QString thumbnailUrl(const EvaluationInstanceRecord &record) const;
    void rebuildFilteredAggregates();

    QString report_path_;
    QString result_path_;
    bool available_{false};
    bool loading_{false};
    QString error_;
    QString state_;
    QString primary_metric_set_;
    QString metric_scope_description_;
    QVariantMap image_metric_definition_;
    QString result_revision_;
    double confidence_threshold_{0.0};
    double iou_threshold_{0.0};
    QString matching_strategy_;
    bool has_instance_metrics_{false};
    bool has_image_metrics_{false};
    bool has_confusion_matrix_{false};
    bool has_instance_events_{false};
    bool anomaly_detection_{false};
    QPointer<QObject> global_filter_;
    EvaluationMetricModel *instance_metrics_{nullptr};
    EvaluationMetricModel *image_metrics_{nullptr};
    EvaluationMetricModel *per_class_metrics_{nullptr};
    EvaluationMetricSortProxyModel *sorted_per_class_metrics_{nullptr};
    EvaluationConfusionModel *confusion_matrix_{nullptr};
    EvaluationImageModel *images_{nullptr};
    EvaluationImageFilterProxyModel *filtered_images_{nullptr};
    EvaluationInstanceModel *instances_{nullptr};
    EvaluationGlobalFilterProxyModel *global_filtered_instances_{nullptr};
    EvaluationCellFilterProxyModel *filtered_instances_{nullptr};
    EvaluationChartModel *charts_{nullptr};
    int selected_proxy_row_{-1};
    QVariantMap selected_instance_;
    int reload_revision_{0};
    int aggregation_revision_{0};
    qint64 loaded_report_size_{-1};
    qint64 loaded_report_mtime_{-1};
    qint64 loaded_result_size_{-1};
    qint64 loaded_result_mtime_{-1};
};

} // namespace dltool::model
