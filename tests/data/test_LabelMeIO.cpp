#include "data/DataIO.h"

#include "core/CoreDef.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTest>

#include <atomic>
#include <cmath>
#include <vector>

class LabelMeIOTest final : public QObject
{
    Q_OBJECT

private slots:
    void importsSubpixelRectangleAsPolygon()
    {
        QTemporaryDir temporary_dir;
        QVERIFY(temporary_dir.isValid());

        const QString image_dir      = QDir(temporary_dir.path()).filePath(QStringLiteral("images"));
        const QString annotation_dir = QDir(temporary_dir.path()).filePath(QStringLiteral("annotations"));
        QVERIFY(QDir().mkpath(image_dir));
        QVERIFY(QDir().mkpath(annotation_dir));

        const QString image_path = QDir(image_dir).filePath(QStringLiteral("sample.png"));
        QImage        image(QSize(1784, 2066), QImage::Format_RGB32);
        image.fill(Qt::white);
        QVERIFY(image.save(image_path));

        const QString annotation_path = QDir(annotation_dir).filePath(QStringLiteral("sample.json"));
        QFile         annotation_file(annotation_path);
        QVERIFY(annotation_file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream annotation_stream(&annotation_file);
        annotation_stream << R"({
            "imagePath": "S-57-1_2_1719225447778.jpg",
            "imageWidth": 1784,
            "imageHeight": 2066,
            "shapes": [{
                "label": "3",
                "shape_type": "rectangle",
                "points": [[388.61, 1712.88], [389.33000000000004, 1713.6000000000001]]
            }]
        })";
        annotation_file.close();

        dltool::data::LabelMeIO importer;
        importer.setTargetMethod(static_cast<int>(dltool::core::DeepLearningMethod::AnomalyDetection));

        std::vector<dltool::data::ImportedLabel> labels;
        std::atomic_bool                      finished{false};
        std::atomic_bool                      success{false};
        QObject::connect(&importer,
                         &dltool::data::DataIO::dataBatchReady,
                         &importer,
                         [&labels](int64_t,
                                   std::vector<QString>,
                                   std::vector<int64_t>,
                                   std::vector<int64_t>,
                                   std::map<QString, QString>,
                                   std::vector<dltool::data::ImportedLabel> batch,
                                   int64_t,
                                   int64_t)
                         { labels = std::move(batch); },
                         Qt::DirectConnection);
        QObject::connect(&importer,
                         &dltool::data::DataIO::importFinished,
                         &importer,
                         [&finished, &success](bool import_success, std::vector<int64_t>, std::vector<int64_t>)
                         {
                             success.store(import_success, std::memory_order_release);
                             finished.store(true, std::memory_order_release);
                         },
                         Qt::DirectConnection);

        importer.startImport(1, image_dir, annotation_dir);
        QTRY_VERIFY_WITH_TIMEOUT(finished.load(std::memory_order_acquire), 10000);
        QVERIFY(success.load(std::memory_order_acquire));
        QCOMPARE(labels.size(), size_t(1));
        QCOMPARE(labels.front().label_class_name, QStringLiteral("3"));

        const QVariantList points = labels.front().data.value(QStringLiteral("points")).toList();
        QCOMPARE(points.size(), 4);
        QVERIFY(std::abs(labels.front().data.value(QStringLiteral("width")).toDouble() - 0.72) < 1e-9);
        QVERIFY(std::abs(labels.front().data.value(QStringLiteral("height")).toDouble() - 0.72) < 1e-9);
    }

    void scalesRectangleCoordinatesFromLabelMeImageSize()
    {
        QTemporaryDir temporary_dir;
        QVERIFY(temporary_dir.isValid());

        const QString image_dir      = QDir(temporary_dir.path()).filePath(QStringLiteral("images"));
        const QString annotation_dir = QDir(temporary_dir.path()).filePath(QStringLiteral("annotations"));
        QVERIFY(QDir().mkpath(image_dir));
        QVERIFY(QDir().mkpath(annotation_dir));

        const QString image_path = QDir(image_dir).filePath(QStringLiteral("sample.png"));
        QImage        image(QSize(100, 100), QImage::Format_RGB32);
        image.fill(Qt::white);
        QVERIFY(image.save(image_path));

        const QString annotation_path = QDir(annotation_dir).filePath(QStringLiteral("sample.json"));
        QFile         annotation_file(annotation_path);
        QVERIFY(annotation_file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream annotation_stream(&annotation_file);
        annotation_stream << R"({
            "imagePath": "sample.png",
            "imageWidth": 200,
            "imageHeight": 200,
            "shapes": [{
                "label": "defect",
                "shape_type": "rectangle",
                "points": [[120, 40], [180, 80]]
            }]
        })";
        annotation_file.close();

        dltool::data::LabelMeIO importer;
        importer.setTargetMethod(static_cast<int>(dltool::core::DeepLearningMethod::AnomalyDetection));

        std::vector<dltool::data::ImportedLabel> labels;
        std::atomic_bool                      finished{false};
        std::atomic_bool                      success{false};
        QObject::connect(&importer,
                         &dltool::data::DataIO::dataBatchReady,
                         &importer,
                         [&labels](int64_t,
                                   std::vector<QString>,
                                   std::vector<int64_t>,
                                   std::vector<int64_t>,
                                   std::map<QString, QString>,
                                   std::vector<dltool::data::ImportedLabel> batch,
                                   int64_t,
                                   int64_t)
                         { labels = std::move(batch); },
                         Qt::DirectConnection);
        QObject::connect(&importer,
                         &dltool::data::DataIO::importFinished,
                         &importer,
                         [&finished, &success](bool import_success, std::vector<int64_t>, std::vector<int64_t>)
                         {
                             success.store(import_success, std::memory_order_release);
                             finished.store(true, std::memory_order_release);
                         },
                         Qt::DirectConnection);

        importer.startImport(1, image_dir, annotation_dir);
        QTRY_VERIFY_WITH_TIMEOUT(finished.load(std::memory_order_acquire), 10000);
        QVERIFY(success.load(std::memory_order_acquire));
        QCOMPARE(labels.size(), size_t(1));

        const QVariantList points = labels.front().data.value(QStringLiteral("points")).toList();
        QCOMPARE(points.size(), 4);
        QCOMPARE(points.at(0).toMap().value(QStringLiteral("x")).toDouble(), 60.0);
        QCOMPARE(points.at(0).toMap().value(QStringLiteral("y")).toDouble(), 20.0);
        QCOMPARE(points.at(2).toMap().value(QStringLiteral("x")).toDouble(), 90.0);
        QCOMPARE(points.at(2).toMap().value(QStringLiteral("y")).toDouble(), 40.0);
    }
};

QTEST_GUILESS_MAIN(LabelMeIOTest)

#include "test_LabelMeIO.moc"
