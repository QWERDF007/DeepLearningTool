#include "../test_runner.h"

#include "model/detail/EvaluationImageRequestCache.h"

#include <QImage>
#include <QSemaphore>
#include <QTest>

#include <atomic>
#include <barrier>
#include <thread>
#include <vector>

using namespace dltool::model;

class EvaluationThumbnailImageProviderTest final : public QObject
{
    Q_OBJECT

private slots:
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
};

REGISTER_TEST(EvaluationThumbnailImageProviderTest)

#include "test_EvaluationThumbnailImageProvider.moc"
