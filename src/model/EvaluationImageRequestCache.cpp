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

EvaluationImageRequestCache::EvaluationImageRequestCache(const int max_cost)
    : cache_(std::max(0, max_cost))
{
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
            return *cached;

        const auto pending_it = pending_.constFind(key);
        if (pending_it != pending_.cend())
        {
            pending = pending_it.value();
        }
        else
        {
            pending          = std::make_shared<PendingRequest>();
            pending_[key]    = pending;
            owns_generation  = true;
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
