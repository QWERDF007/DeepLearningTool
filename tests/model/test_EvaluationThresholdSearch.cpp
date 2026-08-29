#include "../test_runner.h"

#include "model/EvaluationThresholdSearch.h"

#include <QTest>

#include <cmath>
#include <limits>

using namespace dltool::model;

class EvaluationThresholdSearchTest : public QObject
{
    Q_OBJECT

private slots:
    void candidatesKeepFiniteUniqueScoresAndNoPredictionEndpoint()
    {
        const QVector<double> candidates = evaluationThresholdCandidates(
            {0.8, std::numeric_limits<double>::quiet_NaN(), 0.4, 0.8,
             std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()});
        QCOMPARE(candidates.size(), 3);
        QCOMPARE(candidates.at(0), 0.4);
        QCOMPARE(candidates.at(1), 0.8);
        QVERIFY(candidates.at(2) > 0.8);
        QVERIFY(std::isfinite(candidates.at(2)));
    }

    void searchVisitsEveryCandidateAndUsesScoreGreaterEqualSemantics()
    {
        const QVector<double> scores{0.2, 0.5, 0.9};
        QVector<double>       visited;
        const EvaluationThresholdSearchResult result = searchBestEvaluationThreshold(
            scores, 2,
            [&visited](const double threshold, EvaluationCounts &counts, QString *)
            {
                visited.push_back(threshold);
                counts.tp = 0;
                counts.fp = 0;
                counts.fn = 2;
                if (0.5 >= threshold)
                    ++counts.tp;
                if (0.9 >= threshold)
                    ++counts.tp;
                counts.fn = 2 - counts.tp;
                if (0.2 >= threshold)
                    ++counts.fp;
                return true;
            });

        QCOMPARE(visited.size(), 4);
        QCOMPARE(visited.at(0), 0.2);
        QCOMPARE(visited.at(1), 0.5);
        QCOMPARE(visited.at(2), 0.9);
        QVERIFY(visited.at(3) > 0.9);
        QVERIFY(result.available);
        QCOMPARE(result.best_point.threshold, 0.5);
        QCOMPARE(result.best_point.counts.tp, qint64(2));
        QCOMPARE(result.best_point.counts.fp, qint64(0));
        QCOMPARE(result.best_point.counts.fn, qint64(0));
    }

    void equalF1ChoosesHighestThreshold()
    {
        const EvaluationThresholdSearchResult result = searchBestEvaluationThreshold(
            {0.1, 0.3, 0.7}, 1,
            [](double, EvaluationCounts &counts, QString *)
            {
                counts = {1, 0, 0};
                return true;
            });
        QVERIFY(result.available);
        QVERIFY(result.best_point.threshold > 0.7);
        QCOMPARE(result.best_point.f1, 1.0);
        QCOMPARE(result.equivalent_best_threshold_min, 0.1);
        QVERIFY(result.equivalent_best_threshold_max > 0.7);
    }

    void mathematicallyEqualF1UsesHighestThreshold()
    {
        const EvaluationThresholdSearchResult result = searchBestEvaluationThreshold(
            {0.1, 0.2}, 1,
            [](const double threshold, EvaluationCounts &counts, QString *)
            {
                // Both count sets have the exact same F1 (2/3), while
                // precision/recall division can round them differently.
                counts = threshold <= 0.1 ? EvaluationCounts{1, 1, 0} : EvaluationCounts{3, 2, 1};
                return true;
            });
        QVERIFY(result.available);
        QCOMPARE(result.best_point.threshold, 0.2);
        QCOMPARE(result.equivalent_best_threshold_min, 0.1);
        QCOMPARE(result.equivalent_best_threshold_max, 0.2);
    }

    void maximumFiniteScoreDoesNotCreateInfiniteThreshold()
    {
        const QVector<double> candidates = evaluationThresholdCandidates(
            {std::numeric_limits<double>::max()});
        QCOMPARE(candidates.size(), 1);
        QCOMPARE(candidates.front(), std::numeric_limits<double>::max());
        QVERIFY(std::isfinite(candidates.front()));

        const EvaluationThresholdSearchResult result = searchBestEvaluationThreshold(
            {std::numeric_limits<double>::max()}, 1,
            [](double, EvaluationCounts &counts, QString *)
            {
                counts = {0, 0, 1};
                return true;
            });
        QVERIFY(result.available);
        QVERIFY(std::isfinite(result.best_point.threshold));
    }

    void equivalentBestThresholdRangeTracksAllTiedCandidates()
    {
        const EvaluationThresholdSearchResult result = searchBestEvaluationThreshold(
            {0.1, 0.2, 0.4}, 2,
            [](const double threshold, EvaluationCounts &counts, QString *)
            {
                // Thresholds 0.1 and 0.2 both yield F1=2/3; 0.4 is the
                // no-prediction endpoint and has F1=0.
                counts = {};
                if (threshold <= 0.1)
                    counts = {1, 1, 1};
                else if (threshold <= 0.2)
                    counts = {2, 0, 1};
                return true;
            });
        QVERIFY(result.available);
        QCOMPARE(result.best_point.threshold, 0.2);
        QCOMPARE(result.equivalent_best_threshold_min, 0.2);
        QCOMPARE(result.equivalent_best_threshold_max, 0.2);

        const EvaluationThresholdSearchResult tied = searchBestEvaluationThreshold(
            {0.1, 0.2, 0.4}, 2,
            [](double, EvaluationCounts &counts, QString *)
            {
                counts = {1, 1, 1};
                return true;
            });
        QVERIFY(tied.available);
        QCOMPARE(tied.equivalent_best_threshold_min, 0.1);
        QVERIFY(tied.equivalent_best_threshold_max > 0.4);
    }

    void invalidSearchInputsDoNotProduceBestThreshold()
    {
        const auto counter = [](double, EvaluationCounts &, QString *)
        {
            QTest::qFail("counter must not be called for invalid search input", __FILE__, __LINE__);
            return false;
        };
        QVERIFY(!searchBestEvaluationThreshold({0.1}, 0, counter).available);
        QVERIFY(!searchBestEvaluationThreshold({std::numeric_limits<double>::quiet_NaN()}, 1, counter).available);
    }

    void noPositiveGroundTruthDoesNotProduceBestThreshold()
    {
        const auto counter = [](double, EvaluationCounts &counts, QString *)
        {
            counts = {1, 0, 0};
            return true;
        };
        const EvaluationThresholdSearchResult result = searchBestEvaluationThreshold({0.2, 0.8}, 0, counter);
        QVERIFY(!result.available);
        QVERIFY(result.points.isEmpty());
    }

    void positiveGroundTruthWithZeroF1StillProducesBestThreshold()
    {
        const EvaluationThresholdSearchResult result = searchBestEvaluationThreshold(
            {0.2, 0.8}, 1,
            [](double, EvaluationCounts &counts, QString *)
            {
                counts = {0, 0, 1};
                return true;
            });
        QVERIFY(result.available);
        QCOMPARE(result.best_point.f1, 0.0);
        QVERIFY(result.best_point.threshold > 0.8);
        QCOMPARE(result.points.size(), 3);
    }

    void noFinitePredictionScoreDoesNotProduceBestThreshold()
    {
        const auto counter = [](double, EvaluationCounts &counts, QString *)
        {
            counts = {1, 0, 0};
            return true;
        };
        const EvaluationThresholdSearchResult result = searchBestEvaluationThreshold(
            {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}, 1, counter);
        QVERIFY(!result.available);
        QVERIFY(result.points.isEmpty());
    }

    void cancellationStopsBeforeEvaluatingNextCandidate()
    {
        auto cancel = std::make_shared<std::atomic_bool>(false);
        int  calls  = 0;
        QString error;
        const EvaluationThresholdSearchResult result = searchBestEvaluationThreshold(
            {0.2, 0.8}, 1,
            [&calls, &cancel](double, EvaluationCounts &counts, QString *)
            {
                ++calls;
                counts = {1, 0, 0};
                cancel->store(true, std::memory_order_relaxed);
                return true;
            },
            cancel, &error);
        QVERIFY(!result.available);
        QCOMPARE(calls, 1);
        QVERIFY(error.contains(QStringLiteral("取消")));
    }

    void counterFailureWithoutMessageIsReported()
    {
        QString error;
        const EvaluationThresholdSearchResult result = searchBestEvaluationThreshold(
            {0.2}, 1,
            [](double, EvaluationCounts &, QString *)
            {
                return false;
            },
            {}, &error);
        QVERIFY(!result.available);
        QVERIFY(error.contains(QStringLiteral("计数")));
    }
};

REGISTER_TEST(EvaluationThresholdSearchTest)

#include "test_EvaluationThresholdSearch.moc"
