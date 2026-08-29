#pragma once

#include "dltool/model/Export.h"
#include "model/EvaluationCharts.h"
#include "model/EvaluationData.h"
#include "model/EvaluationThresholdSearch.h"
#include "model/ModelEvaluationModels.h"
#include "model/ModelEvaluationOptions.h"
#include "model/ModelEvaluationProtocol.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
#include <atomic>
#include <memory>

namespace dltool::model {

struct EvaluationResult;

/**
 * @brief 评估引擎基类：模板方法骨架 + 纯虚子类钩子。
 *
 * IEvaluationEngine::evaluate() 实现评估的共享流水线（加载图像/预测、
 * 源文件校验、取消检查、能力声明与 EvaluationResult 组装），把方法特异
 * 的计算（实例/图像计数、事件、混淆矩阵、图表）委托给子类钩子。子类
 * 只关心当前方法的差异化逻辑，无需重复公共骨架。
 *
 * 该类是纯 C++ 类型，不导出到 QML。
 */
class MODEL_API IEvaluationEngine
{
public:
    virtual ~IEvaluationEngine() = default;

    /**
     * @brief 当前引擎服务的评估方法。
     * @return 评估方法。
     */
    virtual evaluation::Method method() const = 0;

    /**
     * @brief 模板方法骨架：执行一次完整评估。
     *
     * 流程与旧 ModelEvaluationService::evaluate 保持一致：取消检查 →
     * 路径校验 → 加载图像/预测 → 源文件过滤 → 类别目录 → 实例/图像计数
     * → 事件 → 图表 → 混淆矩阵 → 组装 EvaluationResult。
     * @param options 评估输入。
     * @param result 可选强类型结果输出。
     * @param err_msg 可选错误信息输出。
     * @return 成功返回 true。
     */
    bool evaluate(const ModelEvaluationOptions &options, EvaluationResult *result = nullptr,
                  QString *err_msg = nullptr);

protected:
    /**
     * @brief 子类共享的临时计算暂存区。
     *
     * 在 evaluate() 开始时重置。检测方法在单个 per-image 匹配循环中同时
     * 产出实例计数、矩阵、事件与图像级计数，并把它们暂存在这里供后续
     * 钩子（computeImageCounts/buildEvents/buildCharts/buildConfusionMatrix）
     * 共享，避免重复匹配。输入标量（阈值/策略/取消令牌/路径）也暂存在此，
     * 使钩子无需感知 ModelEvaluationOptions 全貌。
     */
    struct ComputeScratch
    {
        QMap<QString, qint64>           matrix;       ///< 矩阵键（行\x1f列）-> 计数。
        QList<EvaluationInstanceRecord> events;       ///< 已生成的事件记录。
        QMap<int, EvaluationCounts>     per_class;    ///< 类别级实例计数（检测路径）。
        EvaluationCounts                overall;      ///< 全局实例计数。
        EvaluationCounts                image_counts; ///< 图像级计数。
        QMap<qint64, double>             anomaly_image_scores; ///< 异常检测使用的图像级分数。
        QVariantMap                       official_metrics; ///< 当前阈值工作点的官方指标。
        QVariantMap                       image_metric_definition; ///< 图像级指标定义。
        EvaluationThresholdSearchResult threshold_search; ///< 当前评估的进程内阈值搜索结果。

        QString                           dataset_root;    ///< GT mask 解析根目录（项目库绝对路径）。
        QString                           prediction_root; ///< 预测 mask 解析根目录。
        double                            confidence{0.5}; ///< 置信度阈值。
        double                            iou{0.5};        ///< IoU 阈值。
        evaluation::MatchingStrategy      matching_strategy{evaluation::MatchingStrategy::GreedyIoU};
        QVariantMap                       preprocessing_config; ///< 模型空间预处理参数（仅用于展示坐标）。
        std::shared_ptr<std::atomic_bool> cancel_token;
        bool                              collect_events{true}; ///< 搜索计数阶段关闭事件/几何构造。
    };

    /**
     * @brief 类别目录构建钩子（默认空实现）。
     *
     * 检测方法在此填充图像 GT/预测中的类别；异常检测保持全局目录。
     * @param images 图像记录。
     * @param classes 类别目录（输入为全局目录，可追加）。
     */
    virtual void buildClasses(const QMap<qint64, EvaluationImageData> &images, QMap<int, QString> &classes);

    /**
     * @brief 收集当前方法的全量阈值搜索输入。
     *
     * 默认返回 false，供不支持阈值搜索的自定义引擎保持旧行为。内建异常
     * 检测、目标检测和语义分割引擎返回 true，并只收集有限原始分数。
     */
    virtual bool collectThresholdSearchData(const QMap<qint64, EvaluationImageData> &images,
                                            QVector<double> &scores, qint64 &positive_ground_truth_count,
                                            QString *err_msg);

    /**
     * @brief 实例级计数钩子（纯虚）。
     * @param images 图像记录。
     * @param classes 类别目录。
     * @param per_class 输出：类别级实例计数。
     * @param overall 输出：全局实例计数。
     * @param err_msg 可选错误信息输出。
     * @return 成功返回 true。
     */
    virtual bool computeInstanceCounts(const QMap<qint64, EvaluationImageData> &images,
                                       const QMap<int, QString> &classes, QMap<int, EvaluationCounts> &per_class,
                                       EvaluationCounts &overall, QString *err_msg) = 0;

    /**
     * @brief 图像级计数钩子（纯虚）。
     * @param images 图像记录。
     * @param image_counts 输出：图像级计数。
     * @param err_msg 可选错误信息输出。
     * @return 成功返回 true。
     */
    virtual bool computeImageCounts(const QMap<qint64, EvaluationImageData> &images, EvaluationCounts &image_counts,
                                    QString *err_msg) = 0;

    /**
     * @brief 实例事件钩子（纯虚）。
     * @param images 图像记录。
     * @param events 输出：实例事件值对象列表。
     * @param err_msg 可选错误信息输出。
     * @return 成功返回 true。
     */
    virtual bool buildEvents(const QMap<qint64, EvaluationImageData> &images, QList<EvaluationInstanceRecord> &events,
                             QString *err_msg) = 0;

    /**
     * @brief 图表构建钩子（纯虚）。
     * @param images 图像记录。
     * @param classes 类别目录。
     * @param overall 全局实例计数。
     * @param image_counts 图像级计数。
     * @param per_class 类别级实例计数。
     * @param matrix 矩阵键 -> 计数。
     * @param events 实例事件列表。
     * @param err_msg 可选错误信息输出。
     * @return 图表描述符列表。
     */
    virtual QList<QVariantMap> buildCharts(const QMap<qint64, EvaluationImageData> &images,
                                           const QMap<int, QString> &classes, const EvaluationCounts &overall,
                                           const EvaluationCounts                &image_counts,
                                           const QMap<int, EvaluationCounts>     &per_class,
                                           const QMap<QString, qint64>           &matrix,
                                           const QList<EvaluationInstanceRecord> &events, QString *err_msg) = 0;

    /**
     * @brief 混淆矩阵构建钩子（纯虚）。
     * @param classes 类别目录。
     * @param matrix 矩阵键 -> 计数。
     * @return 混淆矩阵单元格列表。
     */
    virtual QVector<EvaluationConfusionCell> buildConfusionMatrix(const QMap<int, QString>    &classes,
                                                                  const QMap<QString, qint64> &matrix) = 0;

    /**
     * @brief 当前方法是否产出混淆矩阵。
     * @return 产出返回 true。
     */
    virtual bool hasConfusionMatrix() const = 0;

    /**
     * @brief 当前方法支持的图表渲染类型 key 列表。
     * @return 图表类型 key 列表。
     */
    virtual QStringList chartKinds() const = 0;

    /**
     * @brief 当前方法是否产出图像级指标。
     * @return 产出返回 true。
     */
    virtual bool hasImageLevelStats() const;

    /**
     * @brief 清空一次阈值工作点的派生计算结果并设置工作阈值。
     *
     * 原始异常分数快照与阈值搜索结果会保留，供后续工作点和图表复用。
     */
    void resetComputationScratch(double threshold, bool collect_events);

    /** @brief 当前工作点用于搜索比较的正式计数。 */
    EvaluationCounts thresholdSearchCounts() const;

    /**
     * @brief 子类可访问的共享暂存区。
     */
    ComputeScratch scratch_;

protected:
    /**
     * @brief 协作取消令牌检查（供子类钩子轮询）。
     * @param cancel_token 协作取消令牌，可为空。
     * @return 已置位返回 true。
     */
    bool cancelled(const std::shared_ptr<std::atomic_bool> &cancel_token) const;
};

} // namespace dltool::model
