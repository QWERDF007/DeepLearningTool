#pragma once

#include "dltool/model/Export.h"
#include "model/EvaluationData.h"
#include "model/EvaluationThresholdSearch.h"

#include <QCache>
#include <QMutex>
#include <QVariantList>

namespace dltool::model {

/**
 * @brief 当前评估任务的可重建派生结果缓存。
 *
 * 缓存只属于创建它的项目/测试任务，不是进程级共享状态。调用方在预测
 * 快照变化前调用 prepare()，缓存会整体失效；分数图缓存还会校验文件大小
 * 和修改时间。缓存内容均可从任务数据库、预测产物和评估参数重建，不能
 * 作为持久化事实源。
 */
struct MODEL_API EvaluationScoreMaximumCacheValue
{
    qint64 file_size{0};
    qint64 last_modified_ms{0};
    bool   has_score{false};
    double maximum{0.0};
};

/** @brief 异常分割多边形的可重建缓存值。 */
struct MODEL_API EvaluationAnomalyRegionCacheValue
{
    QVariantList model_polygons;
    QVariantList image_polygons;
};

/** @brief 评估派生缓存的项目、任务和预测产物作用域。 */
struct MODEL_API EvaluationArtifactScope
{
    QString project_database_path;
    QString task_database_path;
    QString model_uuid;
    QString test_task_uuid;
    QString prediction_snapshot;

    bool operator==(const EvaluationArtifactScope &other) const
    {
        return project_database_path == other.project_database_path
            && task_database_path == other.task_database_path && model_uuid == other.model_uuid
            && test_task_uuid == other.test_task_uuid && prediction_snapshot == other.prediction_snapshot;
    }
};

class MODEL_API EvaluationArtifactCache final
{
public:
    EvaluationArtifactCache();

    /**
     * @brief 绑定当前项目/任务和预测快照；作用域变化时清空所有派生缓存。
     * @param scope 当前评估所属项目、任务和预测产物身份。
     */
    void prepare(const EvaluationArtifactScope &scope);

    bool findThreshold(const QString &key, EvaluationThresholdSearchResult *result);
    void storeThreshold(const QString &key, const EvaluationThresholdSearchResult &result);

    bool findScoreMaximum(const QString &path, EvaluationScoreMaximumCacheValue *result);
    void storeScoreMaximum(const QString &path, bool has_score, double maximum);

    bool findAnomalyRegions(const QString &key, EvaluationAnomalyRegionCacheValue *value);
    void storeAnomalyRegions(const QString &key, const EvaluationAnomalyRegionCacheValue &value);

private:
    mutable QMutex mutex_;
    EvaluationArtifactScope scope_;
    QCache<QString, EvaluationThresholdSearchResult>    threshold_cache_;
    QCache<QString, EvaluationScoreMaximumCacheValue>   score_maximum_cache_;
    QCache<QString, EvaluationAnomalyRegionCacheValue>  anomaly_region_cache_;
};

} // namespace dltool::model
