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

    void exhaustiveDeduplicatedThresholdCandidatesCompareMicroF1AndSelectHighestOnTie()
    {
        // 5 个正类预测样本分数与 5 个负类预测样本分数，包含 NaN、Inf、重复值
        struct Sample {
            double score;
            bool is_positive;
        };
        const std::vector<Sample> dataset = {
            {0.15, false},
            {0.25, false},
            {0.35, true},
            {0.45, false},
            {0.55, true},
            {0.65, true},
            {0.75, false},
            {0.85, true},
            {0.95, true},
            {0.45, false}, // 重复值
            {0.85, true},  // 重复值
        };
        QVector<double> raw_scores;
        for (const auto &item : dataset)
            raw_scores.push_back(item.score);
        raw_scores.push_back(std::numeric_limits<double>::quiet_NaN());
        raw_scores.push_back(std::numeric_limits<double>::infinity());

        const qint64 total_positives = 5;

        // 穷举手动在所有去重后的候选切分点上计算微观 micro-F1
        const QVector<double> candidates = evaluationThresholdCandidates(raw_scores);
        QVERIFY(candidates.size() >= 8);

        double manual_best_f1 = -1.0;
        double manual_best_threshold = -1.0;
        double manual_tie_min = 1e9;
        double manual_tie_max = -1e9;

        for (const double thresh : candidates)
        {
            qint64 tp = 0;
            qint64 fp = 0;
            for (const auto &item : dataset)
            {
                if (item.score >= thresh)
                {
                    if (item.is_positive)
                        ++tp;
                    else
                        ++fp;
                }
            }
            const qint64 fn = total_positives - tp;
            const double f1 = (tp + fp > 0 && tp + fn > 0)
                                ? (2.0 * tp) / (2.0 * tp + fp + fn)
                                : 0.0;
            if (f1 > manual_best_f1 + 1e-12)
            {
                manual_best_f1 = f1;
                manual_best_threshold = thresh;
                manual_tie_min = thresh;
                manual_tie_max = thresh;
            }
            else if (std::abs(f1 - manual_best_f1) <= 1e-12)
            {
                manual_tie_min = std::min(manual_tie_min, thresh);
                manual_tie_max = std::max(manual_tie_max, thresh);
                if (thresh > manual_best_threshold)
                    manual_best_threshold = thresh;
            }
        }

        // 使用 searchBestEvaluationThreshold 搜索
        const EvaluationThresholdSearchResult result = searchBestEvaluationThreshold(
            raw_scores, total_positives,
            [&dataset, total_positives](const double threshold, EvaluationCounts &counts, QString *)
            {
                counts = {0, 0, total_positives};
                for (const auto &item : dataset)
                {
                    if (item.score >= threshold)
                    {
                        if (item.is_positive)
                        {
                            ++counts.tp;
                            --counts.fn;
                        }
                        else
                        {
                            ++counts.fp;
                        }
                    }
                }
                return true;
            });

        QVERIFY(result.available);
        // 验收条件 1: 穷举样例对照全部有限去重切分点的 micro-F1，同分选择最高阈值
        QCOMPARE(result.best_point.f1, manual_best_f1);
        QCOMPARE(result.best_point.threshold, manual_best_threshold);
        QCOMPARE(result.equivalent_best_threshold_min, manual_tie_min);
        QCOMPARE(result.equivalent_best_threshold_max, manual_tie_max);
    }
};

REGISTER_TEST(EvaluationThresholdSearchTest)

#include "test_EvaluationThresholdSearch.moc"
