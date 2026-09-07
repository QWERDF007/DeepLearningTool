#include "../test_runner.h"

#include "model/EvaluationArtifactCache.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace dltool::model;

class EvaluationArtifactCacheTest : public QObject
{
    Q_OBJECT

private slots:
    void entriesAreIsolatedAndInvalidatedBySnapshotOrFileIdentity()
    {
        EvaluationArtifactCache first;
        EvaluationArtifactCache second;
        const EvaluationArtifactScope first_scope{QStringLiteral("project-a"), QStringLiteral("task-a"),
                                                 QStringLiteral("model-a"), QStringLiteral("test-a"),
                                                 QStringLiteral("prediction-a")};
        const EvaluationArtifactScope second_scope{QStringLiteral("project-a"), QStringLiteral("task-a"),
                                                  QStringLiteral("model-a"), QStringLiteral("test-a"),
                                                  QStringLiteral("prediction-a")};
        first.prepare(first_scope);
        second.prepare(second_scope);

        EvaluationThresholdSearchResult threshold;
        threshold.available            = true;
        threshold.best_point.threshold = 0.75;
        threshold.best_point.f1        = 0.8;
        first.storeThreshold(QStringLiteral("threshold-key"), threshold);

        EvaluationThresholdSearchResult loaded_threshold;
        QVERIFY(first.findThreshold(QStringLiteral("threshold-key"), &loaded_threshold));
        QCOMPARE(loaded_threshold.best_point.threshold, 0.75);
        QVERIFY(!second.findThreshold(QStringLiteral("threshold-key"), &loaded_threshold));

        EvaluationAnomalyRegionCacheValue regions;
        regions.model_polygons = {QVariantList{QVariantMap{{QStringLiteral("x"), 2.0}}}};
        regions.image_polygons = {QVariantList{QVariantMap{{QStringLiteral("x"), 3.0}}}};
        first.storeAnomalyRegions(QStringLiteral("region-key"), regions);

        EvaluationAnomalyRegionCacheValue loaded_regions;
        QVERIFY(first.findAnomalyRegions(QStringLiteral("region-key"), &loaded_regions));
        QCOMPARE(loaded_regions.model_polygons, regions.model_polygons);
        QVERIFY(!second.findAnomalyRegions(QStringLiteral("region-key"), &loaded_regions));

        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString score_path = QDir(temporary.path()).filePath(QStringLiteral("score.tiff"));
        QFile         score_file(score_path);
        QVERIFY(score_file.open(QIODevice::WriteOnly));
        QVERIFY(score_file.write("a") == 1);
        score_file.close();

        first.storeScoreMaximum(score_path, true, 4.5);
        EvaluationScoreMaximumCacheValue loaded_score;
        QVERIFY(first.findScoreMaximum(score_path, &loaded_score));
        QVERIFY(loaded_score.has_score);
        QCOMPARE(loaded_score.maximum, 4.5);
        QVERIFY(!second.findScoreMaximum(score_path, &loaded_score));

        QVERIFY(score_file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(score_file.write("changed") == 7);
        score_file.close();
        QVERIFY(!first.findScoreMaximum(score_path, &loaded_score));

        const EvaluationArtifactScope next_scope{QStringLiteral("project-a"), QStringLiteral("task-a"),
                                                 QStringLiteral("model-a"), QStringLiteral("test-a"),
                                                 QStringLiteral("prediction-b")};
        first.prepare(next_scope);
        QVERIFY(!first.findThreshold(QStringLiteral("threshold-key"), &loaded_threshold));
        QVERIFY(!first.findAnomalyRegions(QStringLiteral("region-key"), &loaded_regions));

        first.prepare(first_scope);
        first.storeThreshold(QStringLiteral("project-key"), threshold);
        const EvaluationArtifactScope other_project_scope{QStringLiteral("project-b"), QStringLiteral("task-a"),
                                                          QStringLiteral("model-a"), QStringLiteral("test-a"),
                                                          QStringLiteral("prediction-a")};
        first.prepare(other_project_scope);
        QVERIFY(!first.findThreshold(QStringLiteral("project-key"), &loaded_threshold));
    }
};

REGISTER_TEST(EvaluationArtifactCacheTest)

#include "test_EvaluationArtifactCache.moc"
