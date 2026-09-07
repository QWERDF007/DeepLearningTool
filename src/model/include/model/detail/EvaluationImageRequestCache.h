#pragma once

#include "dltool/model/Export.h"

#include <QCache>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QWaitCondition>

#include <functional>
#include <memory>

namespace dltool::model::detail {

/**
 * @brief 线程安全的图像请求缓存和同 key 生成协调器。
 *
 * 缓存命中和生成中的请求共享同一个结果。调用方只需提供稳定 key 和
 * 无副作用的 loader；loader 在同一个 key 的并发 miss 中最多执行一次。
 * 图像缓存按实际字节数计费，不能缓存的图像仍会交付给当前请求和等待者。
 */
class MODEL_API EvaluationImageRequestCache final
{
public:
    using Loader = std::function<QImage()>;

    explicit EvaluationImageRequestCache(int max_cost = 64 * 1024 * 1024);

    QImage getOrCreate(const QString &key, const Loader &loader);

private:
    struct PendingRequest;

    static int imageCost(const QImage &image);

    QMutex mutex_;
    QCache<QString, QImage> cache_;
    QHash<QString, std::shared_ptr<PendingRequest>> pending_;
};

} // namespace dltool::model::detail
