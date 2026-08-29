#pragma once

#include "dltool/model/Export.h"
#include "model/IEvaluationEngine.h"

namespace dltool::model {

/**
 * @brief 检测/分割类评估引擎：实现共享的 per-image 实例匹配循环。
 *
 * 本类按旧 ModelEvaluationService 的检测分支计算实例级计数、图像级
 * presence 计数、矩阵与实例事件，并复用 EvaluationCharts 组装官方指标/
 * 图表与混淆矩阵。Detection/Segmentation 通过构造函数传入 method_ 复用
 * 同一套实现，为未来方法特异性行为保留载体。
 */
class MODEL_API InstanceMatchingEvaluationEngine : public IEvaluationEngine
{
public:
    /**
     * @brief 构造评估引擎。
     * @param method 评估方法（Detection / Segmentation）。
     */
    explicit InstanceMatchingEvaluationEngine(evaluation::Method method);

    evaluation::Method method() const override;

protected:
    void buildClasses(const QMap<qint64, EvaluationImageData> &images, QMap<int, QString> &classes) override;

    bool collectThresholdSearchData(const QMap<qint64, EvaluationImageData> &images, QVector<double> &scores,
                                    qint64 &positive_ground_truth_count, QString *err_msg) override;

    bool computeInstanceCounts(const QMap<qint64, EvaluationImageData> &images, const QMap<int, QString> &classes,
                               QMap<int, EvaluationCounts> &per_class, EvaluationCounts &overall,
                               QString *err_msg) override;

    bool computeImageCounts(const QMap<qint64, EvaluationImageData> &images, EvaluationCounts &image_counts,
                            QString *err_msg) override;

    bool buildEvents(const QMap<qint64, EvaluationImageData> &images, QList<EvaluationInstanceRecord> &events,
                     QString *err_msg) override;

    QList<QVariantMap> buildCharts(const QMap<qint64, EvaluationImageData> &images, const QMap<int, QString> &classes,
                                   const EvaluationCounts &overall, const EvaluationCounts &image_counts,
                                   const QMap<int, EvaluationCounts> &per_class, const QMap<QString, qint64> &matrix,
                                   const QList<EvaluationInstanceRecord> &events, QString *err_msg) override;

    QVector<EvaluationConfusionCell> buildConfusionMatrix(const QMap<int, QString>    &classes,
                                                          const QMap<QString, qint64> &matrix) override;

    bool hasConfusionMatrix() const override;

    QStringList chartKinds() const override;

private:
    /**
     * @brief 单次 per-image 实例匹配循环。
     *
     * 与旧 ModelEvaluationService 检测分支逐字对齐：过滤预测、稳定降序、
     * 匹配、TP/FP/FN/类别错误计数、矩阵键累计与实例事件。结果写入共享
     * scratch_（per_class/overall/matrix/events/image_counts），供
     * computeImageCounts/buildEvents/buildCharts/buildConfusionMatrix
     * 复用，保证只执行一次匹配。
     * @param images 图像记录。
     * @param err_msg 可选错误信息输出。
     * @return 成功返回 true。
     */
    bool runDetectionLoop(const QMap<qint64, EvaluationImageData> &images, QString *err_msg);

    evaluation::Method method_{evaluation::Method::Unknown};
};

} // namespace dltool::model
