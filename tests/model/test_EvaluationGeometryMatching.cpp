#include "../test_runner.h"

#include "model/EvaluationData.h"
#include "model/EvaluationGeometry.h"
#include "model/EvaluationMatching.h"
#include "model/ModelEvaluationProtocol.h"

#include <QTest>

#include <atomic>
#include <memory>

using namespace dltool::model;

class EvaluationGeometryMatchingTest : public QObject
{
    Q_OBJECT

private slots:
    void boxValidity()
    {
        const EvaluationBox valid{0, 0, 10, 10};
        const EvaluationBox zero_width{0, 0, 0, 10};
        const EvaluationBox negative_height{0, 0, 10, -1};
        QVERIFY(valid.valid());
        QVERIFY(!zero_width.valid());
        QVERIFY(!negative_height.valid());
    }

    void boxMapRoundTrip()
    {
        const EvaluationBox source{3, 4, 20, 30};
        EvaluationBox       target;
        QVERIFY(readBox(evaluationBoxMap(source), target));
        QCOMPARE(target.x, source.x);
        QCOMPARE(target.y, source.y);
        QCOMPARE(target.w, source.w);
        QCOMPARE(target.h, source.h);
    }

    void readBoxFromXywh()
    {
        EvaluationBox box;
        QVERIFY(readBox(QVariantMap{{QStringLiteral("x"), 1.0},
                                    {QStringLiteral("y"), 2.0},
                                    {QStringLiteral("width"), 8.0},
                                    {QStringLiteral("height"), 6.0}},
                        box));
        QCOMPARE(box.x, 1.0);
        QCOMPARE(box.y, 2.0);
        QCOMPARE(box.w, 8.0);
        QCOMPARE(box.h, 6.0);
    }

    void iouValues()
    {
        const EvaluationBox a{0, 0, 10, 10};
        const EvaluationBox b{5, 0, 10, 10};
        const EvaluationBox c{20, 20, 10, 10};
        QCOMPARE(intersectionOverUnion(a, a), 1.0);
        QCOMPARE(intersectionOverUnion(a, b), 1.0 / 3.0);
        QCOMPARE(intersectionOverUnion(a, c), 0.0);
        QCOMPARE(intersectionOverUnion(a, EvaluationBox{}), 0.0);
    }

    void greedyMatching()
    {
        // iou(0,0)=0.9, iou(0,1)=0.1, iou(1,0)=0.1, iou(1,1)=0.9
        const auto iou = [](int p, int g)
        {
            const double values[2][2] = {{0.9, 0.1}, {0.1, 0.9}};
            return values[p][g];
        };
        const QList<MatchPair> pairs = greedyIoUMatches(2, 2, iou, 0.5);
        QCOMPARE(pairs.size(), 2);
        QCOMPARE(pairs.at(0).prediction, 0);
        QCOMPARE(pairs.at(0).ground_truth, 0);
        QCOMPARE(pairs.at(1).prediction, 1);
        QCOMPARE(pairs.at(1).ground_truth, 1);
    }

    void hungarianMatching()
    {
        // 贪心在此矩阵会锁死次优分配，Hungarian 应得到全局最大和。
        const auto iou = [](int p, int g)
        {
            const double values[2][2] = {{0.8, 0.7}, {0.7, 0.05}};
            return values[p][g];
        };
        const QList<MatchPair> pairs = hungarianIoUMatches(2, 2, iou, 0.5);
        QCOMPARE(pairs.size(), 2);
        QCOMPARE(pairs.at(0).prediction, 0);
        QCOMPARE(pairs.at(0).ground_truth, 1);
        QCOMPARE(pairs.at(1).prediction, 1);
        QCOMPARE(pairs.at(1).ground_truth, 0);
    }

    void matchPredictionsBoxes()
    {
        EvaluationGroundTruthData gt;
        gt.class_id         = 1;
        gt.class_name       = QStringLiteral("cat");
        gt.box              = EvaluationBox{0, 0, 10, 10};
        gt.geometry         = evaluationBoxMap(gt.box);
        EvaluationPredictionData pred;
        pred.prediction_id  = QStringLiteral("p1");
        pred.class_id       = 1;
        pred.box            = EvaluationBox{2, 2, 8, 8};
        pred.geometry       = evaluationBoxMap(pred.box);

        const QList<MatchPair> pairs
            = matchPredictions({pred}, {gt}, 0.5, evaluation::MatchingStrategy::GreedyIoU);
        QCOMPARE(pairs.size(), 1);
        QCOMPARE(pairs.at(0).prediction, 0);
        QCOMPARE(pairs.at(0).ground_truth, 0);
        QVERIFY(pairs.at(0).iou > 0.0);
    }

    void matchingHonorsCancel()
    {
        const auto iou = [](int, int) { return 1.0; };
        auto       token = std::make_shared<std::atomic_bool>(true);
        QVERIFY(greedyIoUMatches(1, 1, iou, 0.5, token).isEmpty());
        QVERIFY(hungarianIoUMatches(1, 1, iou, 0.5, token).isEmpty());
    }
};

REGISTER_TEST(EvaluationGeometryMatchingTest)

#include "test_EvaluationGeometryMatching.moc"
