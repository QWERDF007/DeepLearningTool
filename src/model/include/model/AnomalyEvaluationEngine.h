#pragma once

#include "dltool/model/Export.h"
#include "model/IEvaluationEngine.h"

namespace dltool::model {

/**
 * @brief 异常检测评估引擎。
 *
 * 图像级二元分类：按图像聚合 GT 异常；有像素异常分数图时，取其最大
 * 分数作为评估层 pred_score，并与同一阈值生成异常区域和图像级判定。
 * 评估不回退到模型输出的图像分数。产出图像级 TP/TN/FP/FN
 * 计数与每图像一条事件，不产出实例级指标。
 */
class MODEL_API AnomalyEvaluationEngine : public IEvaluationEngine
{
public:
    AnomalyEvaluationEngine();

    evaluation::Method method() const override;

protected:
    void buildClasses(const QMap<qint64, EvaluationImageData> &images, QMap<int, QString> &classes) override;

    bool supportsThresholdSearch() const override;

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
     * 图像级 GT 聚合、统一像素图最大分数、异常预测判定、TP/TN/FP/FN
     * 计数与每图像一条事件。结果写入共享 scratch_（image_counts/events），
     * 供 computeImageCounts/buildEvents 复用。
     * @param images 图像记录。
     * @param err_msg 可选错误信息输出。
     * @return 成功返回 true。
     */
    bool runAnomalyLoop(const QMap<qint64, EvaluationImageData> &images, QString *err_msg);
};

} // namespace dltool::model
