#pragma once

#include "dltool/model/Export.h"
#include "model/IEvaluationEngine.h"

namespace dltool::model {

/**
 * @brief 异常检测评估引擎。
 *
 * 图像级二元分类：按图像聚合 GT 异常、取最大图像分数、class_id==1 且达到
 * 阈值判定预测异常，产出图像级 TP/TN/FP/FN 计数与每图像一条事件。不产
 * 实例级指标；实例混淆矩阵留待后续阶段（当前返回空）。
 */
class MODEL_API AnomalyEvaluationEngine : public IEvaluationEngine
{
public:
    AnomalyEvaluationEngine();

    evaluation::Method method() const override;

protected:
    void buildClasses(const QMap<qint64, EvaluationImageData> &images, QMap<int, QString> &classes) override;

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
     * @brief 单次异常图像级评估循环。
     *
     * 与旧 Service 异常分支逐字对齐：图像级 GT 聚合、最大图像分数、异常
     * 预测判定、TP/TN/FP/FN 计数与每图像一条事件。结果写入共享 scratch_
     *（image_counts/events），供 computeImageCounts/buildEvents 复用。
     * @param images 图像记录。
     * @param err_msg 可选错误信息输出。
     * @return 成功返回 true。
     */
    bool runAnomalyLoop(const QMap<qint64, EvaluationImageData> &images, QString *err_msg);
};

} // namespace dltool::model
