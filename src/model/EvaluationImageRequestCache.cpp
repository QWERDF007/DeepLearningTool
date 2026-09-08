#include "model/detail/EvaluationImageRequestCache.h"

#include <algorithm>
#include <limits>

namespace dltool::model::detail {

struct EvaluationImageRequestCache::PendingRequest
{
    QWaitCondition condition;
    QImage         image;
    bool           completed{false};
};

EvaluationImageRequestCache::EvaluationImageRequestCache(const int max_cost, const int max_pending)
    : cache_(std::max(0, max_cost))
    , max_pending_(std::max(0, max_pending))
{
}

int EvaluationImageRequestCache::maxCost() const
{
    QMutexLocker locker(&mutex_);
    return cache_.maxCost();
}

void EvaluationImageRequestCache::setMaxCost(const int max_cost)
{
    QMutexLocker locker(&mutex_);
    cache_.setMaxCost(std::max(0, max_cost));
}

int EvaluationImageRequestCache::totalCost() const
{
    QMutexLocker locker(&mutex_);
    return cache_.totalCost();
}

int EvaluationImageRequestCache::maxPending() const
{
    QMutexLocker locker(&mutex_);
    return max_pending_;
}

void EvaluationImageRequestCache::setMaxPending(const int max_pending)
{
    QMutexLocker locker(&mutex_);
    max_pending_ = std::max(0, max_pending);
}

int EvaluationImageRequestCache::pendingCount() const
{
    QMutexLocker locker(&mutex_);
    return static_cast<int>(pending_.size());
}

int EvaluationImageRequestCache::peakPendingCount() const
{
    QMutexLocker locker(&mutex_);
    return peak_pending_count_;
}

int EvaluationImageRequestCache::hitCount() const
{
    QMutexLocker locker(&mutex_);
    return hit_count_;
}

int EvaluationImageRequestCache::missCount() const
{
    QMutexLocker locker(&mutex_);
    return miss_count_;
}

void EvaluationImageRequestCache::clear()
{
    QMutexLocker locker(&mutex_);
    cache_.clear();
}

int EvaluationImageRequestCache::imageCost(const QImage &image)
{
    const qsizetype bytes = std::max<qsizetype>(1, image.sizeInBytes());
    return static_cast<int>(std::min<qsizetype>(bytes, std::numeric_limits<int>::max()));
}

QImage EvaluationImageRequestCache::getOrCreate(const QString &key, const Loader &loader)
{
    if (key.isEmpty() || !loader)
        return {};

    std::shared_ptr<PendingRequest> pending;
    bool                          owns_generation = false;
    {
        QMutexLocker locker(&mutex_);
        if (const QImage *cached = cache_.object(key))
        {
            ++hit_count_;
            return *cached;
        }

        ++miss_count_;
        const auto pending_it = pending_.constFind(key);
        if (pending_it != pending_.cend())
        {
            pending = pending_it.value();
        }
        else
        {
            if (max_pending_ > 0 && static_cast<int>(pending_.size()) >= max_pending_)
                return {};

            pending          = std::make_shared<PendingRequest>();
            pending_[key]    = pending;
            owns_generation  = true;
            if (static_cast<int>(pending_.size()) > peak_pending_count_)
                peak_pending_count_ = static_cast<int>(pending_.size());
        }

        if (!owns_generation)
        {
            while (!pending->completed)
                pending->condition.wait(&mutex_);
            return pending->image;
        }
    }

    QImage image;
    try
    {
        image = loader();
    }
    catch (...)
    {
        QMutexLocker locker(&mutex_);
        pending->completed = true;
        pending_.remove(key);
        pending->condition.wakeAll();
        throw;
    }

    {
        QMutexLocker locker(&mutex_);
        if (!image.isNull())
            cache_.insert(key, new QImage(image), imageCost(image));
        pending->image     = image;
        pending->completed = true;
        pending_.remove(key);
        pending->condition.wakeAll();
    }
    return image;
}

} // namespace dltool::model::detail
