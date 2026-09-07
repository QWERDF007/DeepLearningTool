#include "model/EvaluationArtifactCache.h"

#include <QFileInfo>

#include <algorithm>
#include <limits>

namespace dltool::model {

namespace {

int anomalyRegionCost(const EvaluationAnomalyRegionCacheValue &value)
{
    qsizetype point_count = 0;
    for (const QVariant &polygon : value.model_polygons)
        point_count += polygon.toList().size();
    for (const QVariant &polygon : value.image_polygons)
        point_count += polygon.toList().size();

    constexpr qsizetype bytes_per_point = 64;
    const qsizetype     cost = std::max<qsizetype>(1, point_count * bytes_per_point);
    return static_cast<int>(std::min<qsizetype>(cost, std::numeric_limits<int>::max()));
}

int scoreMapCost(const std::shared_ptr<const EvaluationScoreMap> &score_map)
{
    if (score_map == nullptr)
        return 0;
    const qsizetype bytes
        = std::max<qsizetype>(1, static_cast<qsizetype>(score_map->values.size()) * sizeof(double));
    return static_cast<int>(std::min<qsizetype>(bytes, std::numeric_limits<int>::max()));
}

} // namespace

EvaluationArtifactCache::EvaluationArtifactCache()
    : threshold_cache_(32)
    , score_maximum_cache_(4096)
    , score_map_cache_(64 * 1024 * 1024)
    , anomaly_region_cache_(64 * 1024 * 1024)
{
}

void EvaluationArtifactCache::prepare(const EvaluationArtifactScope &scope)
{
    QMutexLocker locker(&mutex_);
    if (scope_ == scope)
        return;

    scope_ = scope;
    threshold_cache_.clear();
    score_maximum_cache_.clear();
    score_map_cache_.clear();
    anomaly_region_cache_.clear();
}

bool EvaluationArtifactCache::findThreshold(const QString &key, EvaluationThresholdSearchResult *result)
{
    if (key.isEmpty() || result == nullptr)
        return false;

    QMutexLocker locker(&mutex_);
    const EvaluationThresholdSearchResult *cached = threshold_cache_.object(key);
    if (cached == nullptr)
        return false;
    *result = *cached;
    return true;
}

void EvaluationArtifactCache::storeThreshold(const QString &key, const EvaluationThresholdSearchResult &result)
{
    if (key.isEmpty())
        return;

    QMutexLocker locker(&mutex_);
    threshold_cache_.insert(key, new EvaluationThresholdSearchResult(result));
}

bool EvaluationArtifactCache::findScoreMaximum(const QString &path, EvaluationScoreMaximumCacheValue *result)
{
    if (path.isEmpty() || result == nullptr)
        return false;

    const QFileInfo file_info(path);
    if (!file_info.isFile())
        return false;

    QMutexLocker locker(&mutex_);
    const EvaluationScoreMaximumCacheValue *cached = score_maximum_cache_.object(file_info.absoluteFilePath());
    if (cached == nullptr || cached->file_size != file_info.size()
        || cached->last_modified_ms != file_info.lastModified().toMSecsSinceEpoch())
        return false;
    *result = *cached;
    return true;
}

void EvaluationArtifactCache::storeScoreMaximum(const QString &path, const bool has_score, const double maximum)
{
    const QFileInfo file_info(path);
    if (!file_info.isFile())
        return;

    QMutexLocker locker(&mutex_);
    score_maximum_cache_.insert(file_info.absoluteFilePath(),
                                new EvaluationScoreMaximumCacheValue{
                                    file_info.size(), file_info.lastModified().toMSecsSinceEpoch(), has_score, maximum});
}

bool EvaluationArtifactCache::findScoreMap(const QString &path, std::shared_ptr<const EvaluationScoreMap> *score_map)
{
    if (path.isEmpty() || score_map == nullptr)
        return false;

    const QFileInfo file_info(path);
    if (!file_info.isFile())
        return false;

    QMutexLocker locker(&mutex_);
    const EvaluationScoreMapCacheValue *cached = score_map_cache_.object(file_info.absoluteFilePath());
    if (cached == nullptr || cached->file_size != file_info.size()
        || cached->last_modified_ms != file_info.lastModified().toMSecsSinceEpoch() || cached->score_map == nullptr)
        return false;
    *score_map = cached->score_map;
    return true;
}

void EvaluationArtifactCache::storeScoreMap(const QString &path,
                                             const std::shared_ptr<const EvaluationScoreMap> &score_map)
{
    const QFileInfo file_info(path);
    const int       cost = scoreMapCost(score_map);
    if (!file_info.isFile() || cost <= 0)
        return;

    QMutexLocker locker(&mutex_);
    score_map_cache_.insert(file_info.absoluteFilePath(),
                            new EvaluationScoreMapCacheValue{
                                file_info.size(), file_info.lastModified().toMSecsSinceEpoch(), score_map},
                            cost);
}

bool EvaluationArtifactCache::findAnomalyRegions(const QString &key, EvaluationAnomalyRegionCacheValue *value)
{
    if (key.isEmpty() || value == nullptr)
        return false;

    QMutexLocker locker(&mutex_);
    const EvaluationAnomalyRegionCacheValue *cached = anomaly_region_cache_.object(key);
    if (cached == nullptr)
        return false;
    *value = *cached;
    return true;
}

void EvaluationArtifactCache::storeAnomalyRegions(const QString &key,
                                                  const EvaluationAnomalyRegionCacheValue &value)
{
    if (key.isEmpty())
        return;

    QMutexLocker locker(&mutex_);
    anomaly_region_cache_.insert(key, new EvaluationAnomalyRegionCacheValue(value), anomalyRegionCost(value));
}

} // namespace dltool::model
