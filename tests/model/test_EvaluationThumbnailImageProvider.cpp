#include "../test_runner.h"

#include "model/EvaluationThumbnailImageProvider.h"
#include "model/detail/EvaluationImageRequestCache.h"

#include <QImage>
#include <QSemaphore>
#include <QTest>
#include <QTemporaryDir>
#include <QUrlQuery>

#include <atomic>
#include <barrier>
#include <thread>
#include <vector>

using namespace dltool::model;

class EvaluationThumbnailImageProviderTest final : public QObject
{
    Q_OBJECT

private slots:
    void providerRejectsGenerationWhoseWorkingBuffersExceedBudget()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("source.png"));
        QImage image(16, 16, QImage::Format_ARGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(path));
        EvaluationThumbnailImageProvider provider;
        provider.requestCache().setMaxCost(1024);
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("path"), path);
        QVERIFY(provider.requestImage(QStringLiteral("budget?") + query.toString(), nullptr, {}).isNull());
        QCOMPARE(provider.requestCache().pendingCount(), 0);
    }

    void shrinkingBudgetKeepsActiveReservationAndRejectsOversizedWork()
    {
        detail::EvaluationImageRequestCache cache(1024, 4);
        QSemaphore entered;
        QSemaphore release;
        QImage result;
        std::thread worker([&] {
            result = cache.getOrCreate(QStringLiteral("active"), [&] {
                QImage image(16, 8, QImage::Format_ARGB32);
                entered.release();
                release.acquire();
                return image;
            }, 512);
        });
        const bool started = entered.tryAcquire(1, 2000);
        cache.setMaxCost(128);
        const int during = cache.totalCost();
        bool extra_loaded = false;
        const QImage extra = cache.getOrCreate(QStringLiteral("extra"), [&] {
            extra_loaded = true;
            return QImage(8, 8, QImage::Format_ARGB32);
        }, 256);
        release.release();
        worker.join();
        QVERIFY(started);
        QCOMPARE(during, 512);
        QVERIFY(!extra_loaded);
        QVERIFY(extra.isNull());
        QVERIFY(!result.isNull());
        QCOMPARE(cache.totalCost(), 0);
        QCOMPARE(cache.maxCost(), 128);
    }

    void concurrentRequestsShareSingleLoader()
    {
        constexpr int request_count = 8;

        detail::EvaluationImageRequestCache cache;
        const QImage expected(QSize(8, 8), QImage::Format_ARGB32);
        std::atomic_int load_count{0};
        std::barrier  start(request_count);
        QSemaphore    first_loader_entered;
        QSemaphore    release_first_loader;
        std::vector<QImage> results(request_count);
        std::vector<std::thread> workers;
        workers.reserve(request_count);

        for (int index = 0; index < request_count; ++index)
        {
            workers.emplace_back([&, index]
            {
                start.arrive_and_wait();
                results[index] = cache.getOrCreate(QStringLiteral("same-request"), [&]
                {
                    const int call = load_count.fetch_add(1, std::memory_order_relaxed) + 1;
                    if (call == 1)
                    {
                        first_loader_entered.release();
                        release_first_loader.acquire();
                    }
                    return expected;
                });
            });
        }

        const bool loader_started = first_loader_entered.tryAcquire(1, 1000);
        QTest::qWait(50);
        const int load_count_before_release = load_count.load(std::memory_order_relaxed);
        release_first_loader.release(request_count);

        for (std::thread &worker : workers)
            worker.join();

        QVERIFY2(loader_started, "the first image loader did not start");
        QCOMPARE(load_count_before_release, 1);
        QCOMPARE(load_count.load(std::memory_order_relaxed), 1);
        for (const QImage &result : results)
        {
            QCOMPARE(result.size(), expected.size());
            QCOMPARE(result.format(), expected.format());
        }
    }

    void requestQueueBudgetAndCacheEvictionEnforced()
    {
        // 预算限制：最多 4 个排队请求，最大字节预算 1024 字节
        detail::EvaluationImageRequestCache cache(1024, 4);
        QCOMPARE(cache.maxCost(), 1024);
        QCOMPARE(cache.maxPending(), 4);

        // 1. 验证排队上限：启动 4 个阻塞中请求占满 pending 预算
        constexpr int slow_requests = 4;
        QSemaphore entered_loaders;
        QSemaphore release_loaders;
        std::atomic_int attempted_count{0};
        std::vector<std::thread> workers;
        workers.reserve(slow_requests + 2);

        for (int i = 0; i < slow_requests; ++i)
        {
            const QString key = QStringLiteral("slow-%1").arg(i);
            workers.emplace_back([&, key]
            {
                cache.getOrCreate(key, [&]
                {
                    attempted_count.fetch_add(1, std::memory_order_relaxed);
                    entered_loaders.release();
                    release_loaders.acquire();
                    return QImage(QSize(4, 4), QImage::Format_ARGB32);
                });
            });
        }

        QVERIFY(entered_loaders.tryAcquire(slow_requests, 1000));
        QCOMPARE(attempted_count.load(), slow_requests);

        // 第 5 个独立 key 请求发起：已超出 maxPending 预算，必须被拒绝并直接返回空，绝不无限制排队
        const QImage rejected = cache.getOrCreate(QStringLiteral("excess-request"), [&]
        {
            attempted_count.fetch_add(1, std::memory_order_relaxed);
            return QImage(QSize(4, 4), QImage::Format_ARGB32);
        });
        QVERIFY(rejected.isNull());
        QCOMPARE(attempted_count.load(), slow_requests); // loader 未被调用

        // 释放阻塞中的 loader 并等待工作线程结束
        release_loaders.release(slow_requests);
        for (auto &w : workers)
            w.join();

        QVERIFY(cache.peakPendingCount() <= 4);
        QCOMPARE(cache.pendingCount(), 0);

        // 2. 验证缓存命中与统计
        QCOMPARE(cache.hitCount(), 0);
        QCOMPARE(cache.missCount(), 5); // 4 个 slow + 1 个 excess
        const QImage hit = cache.getOrCreate(QStringLiteral("slow-0"), [] { return QImage(); });
        QVERIFY(!hit.isNull());
        QCOMPARE(cache.hitCount(), 1);

        // 3. 验证内存成本上限与 LRU 回收
        // 每个 16x16 ARGB32 图像约为 16*16*4 = 1024 字节，恰好占满预算
        for (int i = 0; i < 10; ++i)
        {
            cache.getOrCreate(QStringLiteral("heavy-%1").arg(i), []
            {
                QImage img(16, 16, QImage::Format_ARGB32);
                img.fill(Qt::black);
                return img;
            });
            QVERIFY(cache.totalCost() <= cache.maxCost());
        }
    }

    void heatmapFailureReturnsEmptyAndAllowsFallback()
    {
        EvaluationThumbnailImageProvider provider;
        QSize actual_size;

        // 构造指向不存在或损坏分数图的热力图请求 URL
        const QString invalid_url = QStringLiteral("heatmap-999?path=nonexistent_file.png&scorePath=nonexistent.tiff&heatmap=1&heatmapThreshold=0.5");
        const QImage result = provider.requestImage(invalid_url, &actual_size, QSize(64, 64));

        // 验收条件 3: 生成失败时返回空图像，允许 QML 捕获 Image.Error 退出 Busy 并安全回退原图
        QVERIFY(result.isNull());
        QVERIFY(!actual_size.isValid() || actual_size.isEmpty());
    }
};

REGISTER_TEST(EvaluationThumbnailImageProviderTest)

#include "test_EvaluationThumbnailImageProvider.moc"
