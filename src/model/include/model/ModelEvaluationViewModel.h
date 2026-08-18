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
 * @brief 单一模型测试任务的评估视图模型抽象基类。
 *
 * 负责管理模型评估生命周期、异步评测任务调度、多级过滤筛选（数据集/类别/状态/混淆矩阵单元格）、
 * 指标重聚合（宏平均/图像级二分类）、实例详情选择与图表联动。
 * 评估输入从测试任务的 test.txt、task.db、项目数据库及预测结果文件读取，
 * 计算在后台线程完成，结果只保留在当前进程的模型缓存中。
 */
class MODEL_API ModelEvaluationViewModel : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ModelEvaluationViewModel)
    QML_UNCREATABLE("ModelEvaluationViewModel is owned by ModelTestTaskManager")

public:
    /**
     * @brief 视图模型运行状态枚举值（导出至 QML）。
     */
    enum StateKind
    {
        NotRun        = static_cast<int>(evaluation::ViewState::NotRun),        ///< 尚未运行评估。
        Loading       = static_cast<int>(evaluation::ViewState::Loading),       ///< 正在加载/解析数据中。
        Running       = static_cast<int>(evaluation::ViewState::Running),       ///< 正在计算评估中。
        Failed        = static_cast<int>(evaluation::ViewState::Failed),        ///< 评估计算失败。
        MissingResult = static_cast<int>(evaluation::ViewState::MissingResult), ///< 评估结果缺失或不存在。
        InvalidResult = static_cast<int>(evaluation::ViewState::InvalidResult), ///< 评估结果损坏或版本不匹配。
        Error         = static_cast<int>(evaluation::ViewState::Error),         ///< 发生运行时异常错误。
        Ready         = static_cast<int>(evaluation::ViewState::Ready),         ///< 评估完成且数据就绪。
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

    /** @brief 评估数据是否有效且可用。 */
    bool        available() const;
    /** @brief 当前是否正在加载或评估中。 */
    bool        loading() const;
    /** @brief 对应的视觉任务方法类型（Method 枚举整型值）。 */
    int         method() const;
    /** @brief 状态字符串描述。 */
    QString     state() const;
    /** @brief 强类型状态枚举。 */
    StateKind   stateKind() const;
    /** @brief 错误提示文本。 */
    QString     error() const;
    /** @brief 主指标集类型（"instance" 或 "image"）。 */
    QString     primaryMetricSet() const;
    /** @brief 全局过滤是否处于激活生效状态。 */
    bool        globalFilterActive() const;
    /** @brief 全局过滤生效项描述文本。 */
    QString     globalFilterDescription() const;
    /** @brief 当前指标统计范围说明文本。 */
    QString     metricScopeDescription() const;
    /** @brief 图像级指标定义字典（OK/NG 定义）。 */
    QVariantMap imageMetricDefinition() const;
    /** @brief 评估结果修订版本标识。 */
    QString     resultRevision() const;
    /** @brief 当前生效的置信度阈值。 */
    double      confidenceThreshold() const;
    /** @brief 当前生效的 IoU 重叠度阈值。 */
    double      iouThreshold() const;
    /** @brief 当前匹配策略名称。 */
    QString     matchingStrategy() const;
    /** @brief 是否具备实例级指标。 */
    bool        hasInstanceMetrics() const;
    /** @brief 是否具备图像级指标。 */
    bool        hasImageMetrics() const;
    /** @brief 是否具备混淆矩阵。 */
    bool        hasConfusionMatrix() const;
    /** @brief 是否具备实例级事件列表。 */
    bool        hasInstanceEvents() const;
    /** @brief 是否为异常检测评估。 */
    bool        anomalyDetection() const;

    /** @brief 获取实例级总体指标模型。 */
    EvaluationMetricModel            *instanceMetrics() const;
    /** @brief 获取图像级总体指标模型。 */
    EvaluationMetricModel            *imageMetrics() const;
    /** @brief 获取按类别明细指标模型。 */
    EvaluationMetricModel            *perClassMetrics() const;
    /** @brief 获取混淆矩阵模型。 */
    EvaluationConfusionModel         *confusionMatrix() const;
    /** @brief 获取原始评估图像列表模型。 */
    EvaluationImageModel             *images() const;
    /** @brief 获取经全局过滤后的图像列表代理模型。 */
    EvaluationImageFilterProxyModel  *filteredImages() const;
    /** @brief 获取原始实例事件列表模型。 */
    EvaluationInstanceModel          *instances() const;
    /** @brief 获取第一级（全局数据集/类别）过滤后的实例事件代理模型。 */
    EvaluationGlobalFilterProxyModel *globalFilteredInstances() const;
    /** @brief 获取第二级（状态/混淆矩阵/置信度）过滤后的实例事件代理模型。 */
    EvaluationCellFilterProxyModel   *filteredInstances() const;
    /** @brief 获取评估图表列表模型。 */
    EvaluationChartModel             *charts() const;
    /** @brief 获取当前选中的实例事件详细字典。 */
    QVariantMap                       selectedInstance() const;
    /** @brief 获取当前选中的事件 UUID。 */
    QString                           selectedEventUuid() const;
    /** @brief 获取当前选中的实例在过滤代理模型中的行号。 */
    int                               selectedInstanceRow() const;

    /**
     * @brief 配置评估输入选项。
     * @param options 包含任务路径、数据集列表、阈值等配置。
     */
    void setEvaluationOptions(const ModelEvaluationOptions &options);

    /**
     * @brief 触发异步评估计算。
     * @param notify 完成时是否通过全局消息总线通知。
     */
    Q_INVOKABLE void evaluate(bool notify = false);

    /**
     * @brief 刷新重算当前评估。
     */
    Q_INVOKABLE void refreshEvaluation();

    /**
     * @brief 清空当前评估结果并将状态置为失效。
     * @param state 失效后的视图状态。
     */
    Q_INVOKABLE void invalidate(evaluation::ViewState state = evaluation::ViewState::NotRun);

    /**
     * @brief 设置运行时视图状态。
     * @param state 目标状态。
     */
    Q_INVOKABLE void setRuntimeState(evaluation::ViewState state);

    /**
     * @brief 按代理模型行索引选中实例。
     * @param proxyRow 过滤后的行号。
     */
    Q_INVOKABLE void selectInstance(int proxyRow);

    /**
     * @brief 按事件 UUID 选中实例。
     * @param eventUuid 事件唯一标识。
     * @return 成功选中返回 true。
     */
    Q_INVOKABLE bool selectInstance(const QString &eventUuid);

    /**
     * @brief 选中混淆矩阵中的指定行与列并应用实例过滤。
     * @param rowKey 预测类别行标识。
     * @param columnKey 标注类别列标识。
     */
    Q_INVOKABLE void selectMatrixCell(const QString &rowKey, const QString &columnKey);

    /**
     * @brief 按混淆矩阵行列索引选中单元格。
     * @param row 行号。
     * @param column 列号。
     * @return 成功选中返回 true。
     */
    Q_INVOKABLE bool selectConfusionCell(int row, int column);

    /**
     * @brief 获取指定类别 ID 的显示颜色。
     * @param classId 类别 ID。
     * @return 颜色十六进制字符串。
     */
    Q_INVOKABLE QString classColor(int classId) const;

    /**
     * @brief 清除混淆矩阵选中项。
     */
    Q_INVOKABLE void clearMatrixSelection();

    /**
     * @brief 清除混淆矩阵单元格筛选条件。
     */
    Q_INVOKABLE void clearConfusionCellFilter();

    /**
     * @brief 设置数据集过滤列表。
     * @param datasetIds 数据集 ID 数组。
     */
    Q_INVOKABLE void setDatasetFilter(const QVariantList &datasetIds);

    /**
     * @brief 设置 GT 类别过滤列表。
     * @param classIds 类别 ID 数组。
     */
    Q_INVOKABLE void setClassFilter(const QVariantList &classIds);

    /**
     * @brief 设置预测类别过滤。
     * @param classId 预测类别 ID（-1 为清除）。
     */
    Q_INVOKABLE void setPredClassFilter(qint64 classId);

    /**
     * @brief 清除预测类别过滤。
     */
    Q_INVOKABLE void clearPredClassFilter();

    /**
     * @brief 设置实例匹配状态过滤（如 "TP", "FP", "FN" 等）。
     * @param status 状态标识。
     */
    Q_INVOKABLE void setStatusFilter(const QString &status);

    /**
     * @brief 清除所有明细与单元格筛选条件。
     */
    Q_INVOKABLE void clearFilters();

    /**
     * @brief 绑定外部全局过滤器对象。
     * @param filter 过滤器 QObject 实例。
     */
    void setGlobalFilter(QObject *filter);

protected:
    /**
     * @brief 方法特有数据填充钩子（纯虚虚函数）。
     *
     * 基类完成公共协议加载后调用；子类在此从强类型结果派生并发出自己的
     * 方法数据变化信号。QML 面板通过运行时子类访问这些扩展属性。
     * @param result 已完成的强类型评估结果。
     */
    virtual void applyMethodSpecificData(const EvaluationResult &result) = 0;

signals:
    /** @brief 评估数据发生变化信号。 */
    void evaluationChanged();
    /** @brief 加载状态变更信号。 */
    void loadingChanged();
    /** @brief 选中实例发生变化信号。 */
    void selectedInstanceChanged();
    /** @brief 过滤器状态（激活/描述）变更信号。 */
    void filterStateChanged();

private:
    void setLoading(bool value);
    void clearEvaluation(const QString &error = {}, evaluation::ViewState state = evaluation::ViewState::NotRun);
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
    EvaluationConfusionModel         *confusion_matrix_{nullptr};
    EvaluationImageModel             *images_{nullptr};
    EvaluationImageFilterProxyModel  *filtered_images_{nullptr};
    EvaluationInstanceModel          *instances_{nullptr};
    EvaluationGlobalFilterProxyModel *global_filtered_instances_{nullptr};
    EvaluationCellFilterProxyModel   *filtered_instances_{nullptr};
    EvaluationChartModel             *charts_{nullptr};
    int                               selected_proxy_row_{-1};
    QVariantMap                       selected_instance_;
    QMap<int, double>                 class_ap_map_;
    QMap<int, QString>                class_catalog_;
    QMap<int, QString>                class_colors_;
    int                               evaluation_revision_{0};
    int                               aggregation_revision_{0};
    bool                              aggregation_rebuild_scheduled_{false};
    int                               aggregation_schedule_token_{0};
    bool                              suppress_aggregation_rebuild_{false};
};

} // namespace dltool::model
