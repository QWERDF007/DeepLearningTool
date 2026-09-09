#include "model/detail/EvaluationImageRequestCache.h"

#include <algorithm>
#include <limits>

namespace dltool::model::detail {

struct EvaluationImageRequestCache::PendingRequest
{
    QWaitCondition condition;
    QImage         image;
    int            reserved_cost{0};
    bool           completed{false};
};

EvaluationImageRequestCache::EvaluationImageRequestCache(const int max_cost, const int max_pending)
    : max_cost_(std::max(0, max_cost))
    , cache_(std::max(0, max_cost))
    , max_pending_(std::max(0, max_pending))
{
}

int EvaluationImageRequestCache::estimatedPendingCost() const
{
    if (max_cost_ <= 0)
        return 0;
    const int effective_pending = std::max(1, max_pending_);
    return std::max(1, max_cost_ / effective_pending);
}

int EvaluationImageRequestCache::maxCost() const
{
    QMutexLocker locker(&mutex_);
    return max_cost_;
}

void EvaluationImageRequestCache::setMaxCost(const int max_cost)
{
    QMutexLocker locker(&mutex_);
    max_cost_ = std::max(0, max_cost);
    cache_.setMaxCost(std::max(0, max_cost_ - pending_cost_));
}

int EvaluationImageRequestCache::totalCost() const
{
    QMutexLocker locker(&mutex_);
    return cache_.totalCost() + pending_cost_;
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

QImage EvaluationImageRequestCache::getOrCreate(const QString &key, const Loader &loader, const int expected_cost)
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

        const auto pending_it = pending_.constFind(key);
        if (pending_it != pending_.cend())
        {
            ++hit_count_;
            pending = pending_it.value();
        }
        else
        {
            ++miss_count_;
            if (max_pending_ > 0 && static_cast<int>(pending_.size()) >= max_pending_)
                return {};

            const int reserved = expected_cost > 0 ? expected_cost : estimatedPendingCost();

            if (max_cost_ <= 0 || reserved > max_cost_ - pending_cost_)
                return {};

            if (max_cost_ > 0)
            {
                // 动态修剪已缓存对象，确保已缓存内存 + 进行中预留总和严格在 max_cost_ 预算内
                const int allowed_cache = std::max(0, max_cost_ - pending_cost_ - reserved);
                cache_.setMaxCost(allowed_cache);
                if (cache_.totalCost() + pending_cost_ + reserved > max_cost_)
                    return {};
            }

            pending                = std::make_shared<PendingRequest>();
            pending->reserved_cost = reserved;
            pending_cost_         += reserved;

            pending_[key]   = pending;
            owns_generation = true;
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
        pending_cost_ = std::max(0, pending_cost_ - pending->reserved_cost);
        cache_.setMaxCost(std::max(0, max_cost_ - pending_cost_));
        pending->completed = true;
        pending_.remove(key);
        pending->condition.wakeAll();
        throw;
    }

    {
        QMutexLocker locker(&mutex_);
        pending_cost_ = std::max(0, pending_cost_ - pending->reserved_cost);
        const int actual_cost = imageCost(image);
        if (!image.isNull() && max_cost_ > 0)
        {
            // 仅当图像真实大小能在剩余预算内容纳时才存入 cache_，超预算大图不入 cache_ 避免破坏总成本约束
            if (actual_cost <= max_cost_ - pending_cost_)
            {
                cache_.setMaxCost(std::max(0, max_cost_ - pending_cost_));
                cache_.insert(key, new QImage(image), actual_cost);
            }
        }
        pending->image     = image;
        pending->completed = true;
        pending_.remove(key);
        pending->condition.wakeAll();
    }
    return image;
}


} // namespace dltool::model::detail
