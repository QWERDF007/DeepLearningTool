#include "../test_runner.h"

#include "TestFixture.h"

#include "database/ModelTaskDataBase.h"
#include "model/EvaluationDataset.h"
#include "model/ModelEvaluationProtocol.h"

#include <opencv2/imgcodecs.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <limits>

using namespace dltool::model;

class EvaluationDatasetTest : public QObject
{
    Q_OBJECT

private slots:
    void readsCsvFileList()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString path = QDir(temp.path()).filePath(QStringLiteral("test.txt"));
        QFile       file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("image_id,image_path\n1,F:/images/a.png\n2,F:/images/b.png\n");
        file.close();

        QList<QPair<qint64, QString>> rows;
        QString                      error;
        QVERIFY(readEvaluationImageList(path, rows, {}, &error));
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).first, 1);
        QCOMPARE(rows.at(0).second, QStringLiteral("F:/images/a.png"));
        QCOMPARE(rows.at(1).first, 2);
        QCOMPARE(rows.at(1).second, QStringLiteral("F:/images/b.png"));
    }

    void missingFileFails()
    {
        QList<QPair<qint64, QString>> rows;
        QString                      error;
        QVERIFY(!readEvaluationImageList(QStringLiteral("F:/no/such/file.txt"), rows, {}, &error));
        QVERIFY(!error.isEmpty());
    }

    void csvParserHandlesBomQuotesDuplicatesAndCancellation()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString path = QDir(temp.path()).filePath(QStringLiteral("quoted.csv"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("\xEF\xBB\xBFimage_id,image_path\n"
                   "7,\"F:/images/a,b.png\"\n"
                   "7,F:/images/duplicate.png\n"
                   "bad,F:/images/ignored.png\n"
                   "8,F:/images/c.png\n");
        file.close();

        QList<QPair<qint64, QString>> rows;
        QString error;
        QVERIFY2(readEvaluationImageList(path, rows, {}, &error), qPrintable(error));
        const QList<QPair<qint64, QString>> expected{{7, QStringLiteral("F:/images/a,b.png")},
                                                     {8, QStringLiteral("F:/images/c.png")}};
        QCOMPARE(rows, expected);

        auto cancelled = std::make_shared<std::atomic_bool>(true);
        error.clear();
        QVERIFY(!readEvaluationImageList(path, rows, cancelled, &error));
        QVERIFY(error.contains(QStringLiteral("取消")));
    }

    void loadsSelectedImagesCatalogDimensionsAndMissingCounters()
    {
        using namespace dltool::model::testsupport;
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 dog = fixture.addClass(QStringLiteral("Dog"), QStringLiteral("normal"));
        const qint64 cat_image = fixture.addImage(QStringLiteral("cat"));
        const qint64 dog_image = fixture.addImage(QStringLiteral("dog"));
        QVERIFY(fixture.addDetectionLabel(cat_image, cat, 1, 2, 10, 8) >= 0);
        QVERIFY(fixture.addDetectionLabel(dog_image, dog, 2, 3, 5, 5) >= 0);
        QVERIFY(fixture.writeImageList({{cat_image, QStringLiteral("listed-cat.png")},
                                        {dog_image, QStringLiteral("listed-dog.png")},
                                        {999999, QStringLiteral("missing.png")}}));
        QVERIFY(fixture.setTestSelection({cat}));

        QMap<qint64, EvaluationImageData> images;
        QMap<int, QString>                catalog;
        int                               missing = 0;
        int                               ignored = 0;
        QString                           error;
        QVERIFY2(loadEvaluationImages(fixture.fileListPath(), fixture.projectDatabasePath(),
                                       fixture.taskDatabasePath(), evaluation::Method::Detection, images, {}, &error,
                                       &missing, &ignored,
                                       [](qint64, int *width, int *height)
                                       {
                                           *width = 640;
                                           *height = 480;
                                           return true;
                                       },
                                       &catalog),
                  qPrintable(error));
        QCOMPARE(images.size(), 1);
        QVERIFY(images.contains(cat_image));
        QCOMPARE(images.value(cat_image).width, 640);
        QCOMPARE(images.value(cat_image).height, 480);
        QCOMPARE(images.value(cat_image).gt.size(), 1);
        QCOMPARE(images.value(cat_image).gt.front().class_id, static_cast<int>(cat));
        QCOMPARE(missing, 1);
        QCOMPARE(ignored, 1);
        QCOMPARE(catalog.value(static_cast<int>(cat)), QStringLiteral("Cat"));
        QCOMPARE(catalog.value(static_cast<int>(dog)), QStringLiteral("Dog"));
    }

    void loadsPredictionsNormalizesShapesAndRejectsInvalidRecords()
    {
        using namespace dltool::model::testsupport;
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::Detection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 cat = fixture.addClass(QStringLiteral("Cat"), QStringLiteral("normal"));
        const qint64 image = fixture.addImage(QStringLiteral("cat"));
        QVERIFY(fixture.addDetectionLabel(image, cat, 2, 3, 10, 8) >= 0);
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({cat}));

        QMap<qint64, EvaluationImageData> images;
        QString error;
        QVERIFY(loadEvaluationImages(fixture.fileListPath(), fixture.projectDatabasePath(),
                                      fixture.taskDatabasePath(), evaluation::Method::Detection, images, {}, &error));
        const QVariantMap prediction{{QStringLiteral("prediction_id"), QStringLiteral("p1")},
                                     {QStringLiteral("class_id"), cat},
                                     {QStringLiteral("class_name"), QStringLiteral("Cat")},
                                     {QStringLiteral("score"), 0.9},
                                     {QStringLiteral("x"), -5.0},
                                     {QStringLiteral("y"), -4.0},
                                     {QStringLiteral("w"), 20.0},
                                     {QStringLiteral("h"), 15.0}};
        QVERIFY(fixture.writePrediction(image, QVariantList{prediction}));
        QVERIFY(fixture.writePrediction(987654, prediction));

        int count = 0;
        int ignored = 0;
        QVERIFY2(loadEvaluationPredictions(fixture.taskDatabasePath(), fixture.predictionDirectory(), images, false,
                                            &count, {}, &error, &ignored),
                  qPrintable(error));
        QCOMPARE(count, 1);
        QCOMPARE(ignored, 1);
        QCOMPARE(images.value(image).predictions.size(), 1);
        const auto &loaded = images.value(image).predictions.front();
        QCOMPARE(loaded.prediction_id, QStringLiteral("p1"));
        QVERIFY(loaded.box.valid());
        QCOMPARE(loaded.box.x, 0.0);
        QCOMPARE(loaded.box.y, 0.0);
        QCOMPARE(loaded.box.w, 15.0);
        QCOMPARE(loaded.box.h, 11.0);
        QCOMPARE(loaded.geometry.value(evaluation::fieldName(evaluation::Field::Type)).toString(),
                 QStringLiteral("bbox"));

        QVERIFY(fixture.writePrediction(image, QVariantMap{{QStringLiteral("class_id"), cat},
                                                            {QStringLiteral("score"), QStringLiteral("invalid")}}));
        error.clear();
        QVERIFY(!loadEvaluationPredictions(fixture.taskDatabasePath(), fixture.predictionDirectory(), images, false,
                                           &count, {}, &error, &ignored));
        QVERIFY(error.contains(QStringLiteral("score")));
    }

    void anomalyImagesAndPredictionsUseImageLevelBinaryProtocol()
    {
        using namespace dltool::model::testsupport;
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::AnomalyDetection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 good = fixture.addClass(QStringLiteral("Good"), QStringLiteral("good"));
        const qint64 anomaly = fixture.addClass(QStringLiteral("Scratch"), QStringLiteral("anomaly"));
        const qint64 normal_image
            = fixture.addImage(QStringLiteral("normal"), {{QStringLiteral("image_label_class_id"), good},
                                                            {QStringLiteral("group"), QStringLiteral("good")} });
        const qint64 bad_image
            = fixture.addImage(QStringLiteral("bad"), {{QStringLiteral("image_label_class_id"), anomaly},
                                                        {QStringLiteral("group"), QStringLiteral("anomaly")} });
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({good, anomaly}));
        QVERIFY(fixture.writePrediction(normal_image, anomalyPrediction(0.1)));
        QVERIFY(fixture.writePrediction(bad_image, anomalyPrediction(0.9)));

        QMap<qint64, EvaluationImageData> images;
        QString error;
        QVERIFY2(loadEvaluationImages(fixture.fileListPath(), fixture.projectDatabasePath(),
                                       fixture.taskDatabasePath(), evaluation::Method::AnomalyDetection, images, {},
                                       &error),
                  qPrintable(error));
        QCOMPARE(images.value(normal_image).gt.front().anomaly, false);
        QCOMPARE(images.value(bad_image).gt.front().anomaly, true);
        int count = 0;
        QVERIFY2(loadEvaluationPredictions(fixture.taskDatabasePath(), fixture.predictionDirectory(), images, true,
                                            &count, {}, &error),
                  qPrintable(error));
        QCOMPARE(count, 2);
        QCOMPARE(images.value(bad_image).predictions.front().class_id, 1);
        QCOMPARE(images.value(bad_image).predictions.front().class_name,
                 evaluation::displayText(evaluation::DisplayText::Anomaly));
    }

    void anomalyPredictionsUseTiffWithoutTaskDatabaseRecords()
    {
        using namespace dltool::model::testsupport;
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::AnomalyDetection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 good = fixture.addClass(QStringLiteral("Good"), QStringLiteral("good"));
        const qint64 anomaly = fixture.addClass(QStringLiteral("Scratch"), QStringLiteral("anomaly"));
        const qint64 normal_image
            = fixture.addImage(QStringLiteral("normal"), {{QStringLiteral("image_label_class_id"), good}});
        const qint64 bad_image
            = fixture.addImage(QStringLiteral("bad"), {{QStringLiteral("image_label_class_id"), anomaly}});
        QVERIFY(good >= 0);
        QVERIFY(anomaly >= 0);
        QVERIFY(normal_image >= 0);
        QVERIFY(bad_image >= 0);
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({good, anomaly}));
        QVERIFY(fixture.writePrediction(normal_image, anomalyPrediction(0.1)));
        QVERIFY(fixture.writePrediction(bad_image, anomalyPrediction(0.9)));

        // The TIFF files are the anomaly prediction artifacts. Remove only
        // task.db prediction rows to prove the loader does not depend on them.
        QVERIFY(fixture.removePrediction(normal_image));
        QVERIFY(fixture.removePrediction(bad_image));

        QMap<qint64, EvaluationImageData> images;
        QString error;
        QVERIFY2(loadEvaluationImages(fixture.fileListPath(), fixture.projectDatabasePath(),
                                       fixture.taskDatabasePath(), evaluation::Method::AnomalyDetection, images, {},
                                       &error),
                  qPrintable(error));

        int count = 0;
        QVERIFY2(loadEvaluationPredictions(QDir(fixture.rootPath()).filePath(QStringLiteral("missing-task.db")),
                                            fixture.predictionDirectory(), images, true, &count, {}, &error),
                  qPrintable(error));
        QCOMPARE(count, 2);
        QCOMPARE(images.value(normal_image).predictions.size(), 1);
        QCOMPARE(images.value(bad_image).predictions.size(), 1);
        QVERIFY(qAbs(images.value(normal_image).predictions.front().score - 0.1) < 1e-6);
        QVERIFY(qAbs(images.value(bad_image).predictions.front().score - 0.9) < 1e-6);
        QVERIFY(images.value(normal_image).anomaly_score_map != nullptr);
        QVERIFY(images.value(bad_image).anomaly_score_map != nullptr);
    }

    void anomalyPredictionsCanLoadOnlyImageLevelScores()
    {
        using namespace dltool::model::testsupport;
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::AnomalyDetection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 good = fixture.addClass(QStringLiteral("Good"), QStringLiteral("good"));
        const qint64 anomaly = fixture.addClass(QStringLiteral("Scratch"), QStringLiteral("anomaly"));
        const qint64 normal_image
            = fixture.addImage(QStringLiteral("normal"), {{QStringLiteral("image_label_class_id"), good}});
        const qint64 bad_image
            = fixture.addImage(QStringLiteral("bad"), {{QStringLiteral("image_label_class_id"), anomaly}});
        QVERIFY(good >= 0);
        QVERIFY(anomaly >= 0);
        QVERIFY(normal_image >= 0);
        QVERIFY(bad_image >= 0);
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({good, anomaly}));
        QVERIFY(fixture.writePrediction(normal_image, anomalyPrediction(0.1)));
        QVERIFY(fixture.writePrediction(bad_image, anomalyPrediction(0.9)));

        QMap<qint64, EvaluationImageData> images;
        QString error;
        QVERIFY2(loadEvaluationImages(fixture.fileListPath(), fixture.projectDatabasePath(),
                                       fixture.taskDatabasePath(), evaluation::Method::AnomalyDetection, images, {},
                                       &error),
                  qPrintable(error));

        int count = 0;
        QVERIFY2(loadEvaluationPredictions(fixture.taskDatabasePath(), fixture.predictionDirectory(), images, true,
                                            &count, {}, &error, nullptr, false),
                  qPrintable(error));
        QCOMPARE(count, 2);
        QVERIFY(images.value(normal_image).anomaly_score_map == nullptr);
        QVERIFY(images.value(bad_image).anomaly_score_map == nullptr);
        QVERIFY(images.value(normal_image).has_anomaly_image_score);
        QVERIFY(images.value(bad_image).has_anomaly_image_score);
        QVERIFY(qAbs(images.value(normal_image).anomaly_image_score - 0.1) < 1e-6);
        QVERIFY(qAbs(images.value(bad_image).anomaly_image_score - 0.9) < 1e-6);
        QVERIFY(qAbs(images.value(normal_image).predictions.front().score - 0.1) < 1e-6);
        QVERIFY(qAbs(images.value(bad_image).predictions.front().score - 0.9) < 1e-6);
    }

    void anomalyPredictionsRetainMapsForPredictedImagesInTheSameRead()
    {
        using namespace dltool::model::testsupport;
        EvaluationFixture fixture(static_cast<int>(evaluation::Method::AnomalyDetection));
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));
        const qint64 good = fixture.addClass(QStringLiteral("Good"), QStringLiteral("good"));
        const qint64 anomaly = fixture.addClass(QStringLiteral("Scratch"), QStringLiteral("anomaly"));
        const qint64 normal_image
            = fixture.addImage(QStringLiteral("normal"), {{QStringLiteral("image_label_class_id"), good}});
        const qint64 bad_image
            = fixture.addImage(QStringLiteral("bad"), {{QStringLiteral("image_label_class_id"), anomaly}});
        QVERIFY(good >= 0);
        QVERIFY(anomaly >= 0);
        QVERIFY(normal_image >= 0);
        QVERIFY(bad_image >= 0);
        QVERIFY(fixture.writeImageList());
        QVERIFY(fixture.setTestSelection({good, anomaly}));
        QVERIFY(fixture.writePrediction(normal_image, anomalyPrediction(0.1)));
        QVERIFY(fixture.writePrediction(bad_image, anomalyPrediction(0.9)));

        QMap<qint64, EvaluationImageData> images;
        QString error;
        QVERIFY2(loadEvaluationImages(fixture.fileListPath(), fixture.projectDatabasePath(),
                                       fixture.taskDatabasePath(), evaluation::Method::AnomalyDetection, images, {},
                                       &error),
                  qPrintable(error));
        int count = 0;
        QVERIFY2(loadEvaluationPredictions(fixture.taskDatabasePath(), fixture.predictionDirectory(), images, true,
                                            &count, {}, &error, nullptr, false, 0.5),
                  qPrintable(error));
        QCOMPARE(count, 2);
        QVERIFY(images.value(normal_image).anomaly_score_map == nullptr);
        QVERIFY(images.value(bad_image).anomaly_score_map != nullptr);
        QVERIFY(qAbs(images.value(bad_image).anomaly_score_map->maximum_score - 0.9) < 1e-6);
    }

    void scoreMapReadCachesMaximum()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString path = QDir(temporary.path()).filePath(QStringLiteral("score.tiff"));

        cv::Mat values(2, 2, CV_32FC1);
        values.at<float>(0, 0) = 0.25F;
        values.at<float>(0, 1) = 3.5F;
        values.at<float>(1, 0) = -1.0F;
        values.at<float>(1, 1) = std::numeric_limits<float>::quiet_NaN();
        QVERIFY(cv::imwrite(path.toStdString(), values));

        EvaluationScoreMap score_map;
        QString            error;
        QVERIFY2(readEvaluationScoreMap(path, score_map, &error), qPrintable(error));
        QVERIFY(score_map.has_maximum_score);
        QCOMPARE(score_map.maximum_score, 3.5);

        double maximum = 0.0;
        QVERIFY(evaluationScoreMapMaximum(score_map, &maximum));
        QCOMPARE(maximum, 3.5);
    }
};

REGISTER_TEST(EvaluationDatasetTest)

#include "test_EvaluationDataset.moc"
