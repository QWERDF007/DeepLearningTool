#include "data/DataFormat.h"
#include "data/DataIO.h"
#include "data/DatasetIO.h"
#include "core/CoreDef.h"
#include "common/GeometryKernel.h"
#include "common/MaskPolygonUtils.h"

#include <json.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

#include <atomic>
#include <cmath>
#include <vector>

class DataGeometryFormatsTest final : public QObject
{
    Q_OBJECT

private slots:
    void cocoImportsSegmentationPolygonsAndClipsToImageBounds()
    {
        QTemporaryDir temporary_dir;
        QVERIFY(temporary_dir.isValid());

        const QString image_dir = QDir(temporary_dir.path()).filePath(QStringLiteral("images"));
        const QString data_dir  = QDir(temporary_dir.path()).filePath(QStringLiteral("annotations"));
        QVERIFY(QDir().mkpath(image_dir));
        QVERIFY(QDir().mkpath(data_dir));

        const QString image_path = QDir(image_dir).filePath(QStringLiteral("sample.png"));
        QImage        image(QSize(100, 100), QImage::Format_RGB32);
        image.fill(Qt::white);
        QVERIFY(image.save(image_path));

        const QString coco_json_path = QDir(data_dir).filePath(QStringLiteral("instances.json"));
        QFile         coco_file(coco_json_path);
        QVERIFY(coco_file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream stream(&coco_file);
        // Annotation polygon has points partially outside [0, 100]
        stream << R"({
            "images": [{"id": 1, "file_name": "sample.png", "width": 100, "height": 100}],
            "categories": [{"id": 1, "name": "defect", "supercategory": ""}],
            "annotations": [{
                "id": 1,
                "image_id": 1,
                "category_id": 1,
                "bbox": [0, 10, 50, 60],
                "area": 2500,
                "iscrowd": 0,
                "segmentation": [[-10.0, 10.0, 50.0, 10.0, 50.0, 50.0, 10.0, 70.0, -10.0, 50.0]]
            }]
        })";
        coco_file.close();

        dltool::data::COCOIO importer;
        importer.setTargetMethod(static_cast<int>(dltool::core::DeepLearningMethod::Segmentation));

        std::vector<dltool::data::ImportedLabel> labels;
        std::atomic_bool                        finished{false};
        std::atomic_bool                        success{false};

        QObject::connect(&importer, &dltool::data::DataIO::dataBatchReady, &importer,
                         [&labels](int64_t, std::vector<QString>, std::vector<int64_t>, std::vector<int64_t>,
                                   std::map<QString, QString>, std::vector<dltool::data::ImportedLabel> batch,
                                   int64_t, int64_t)
                         { labels = std::move(batch); },
                         Qt::DirectConnection);
        QObject::connect(&importer, &dltool::data::DataIO::importFinished, &importer,
                         [&finished, &success](bool ok, std::vector<int64_t>, std::vector<int64_t>)
                         {
                             success.store(ok, std::memory_order_release);
                             finished.store(true, std::memory_order_release);
                         },
                         Qt::DirectConnection);

        importer.startImport(1, image_dir, data_dir);
        QTRY_VERIFY_WITH_TIMEOUT(finished.load(std::memory_order_acquire), 10000);
        QVERIFY(importer.waitForDone(5000));
        QVERIFY(success.load(std::memory_order_acquire));
        QCOMPARE(labels.size(), size_t(1));

        const QVariantList points = labels.front().data.value(QStringLiteral("points")).toList();
        QCOMPARE(points.size(), 5);
        for (const QVariant &pt : points)
        {
            const QVariantMap map = pt.toMap();
            const double x = map.value(QStringLiteral("x")).toDouble();
            const double y = map.value(QStringLiteral("y")).toDouble();
            QVERIFY(x >= 0.0 && x <= 100.0);
            QVERIFY(y >= 0.0 && y <= 100.0);
        }
    }

    void cocoImportsBboxAsFourPointPolygonInSegmentationProject()
    {
        QTemporaryDir temporary_dir;
        QVERIFY(temporary_dir.isValid());

        const QString image_dir = QDir(temporary_dir.path()).filePath(QStringLiteral("images"));
        const QString data_dir  = QDir(temporary_dir.path()).filePath(QStringLiteral("annotations"));
        QVERIFY(QDir().mkpath(image_dir));
        QVERIFY(QDir().mkpath(data_dir));

        const QString image_path = QDir(image_dir).filePath(QStringLiteral("sample.png"));
        QImage        image(QSize(100, 100), QImage::Format_RGB32);
        image.fill(Qt::white);
        QVERIFY(image.save(image_path));

        const QString coco_json_path = QDir(data_dir).filePath(QStringLiteral("instances.json"));
        QFile         coco_file(coco_json_path);
        QVERIFY(coco_file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream stream(&coco_file);
        // Annotation only has bbox, no segmentation field
        stream << R"({
            "images": [{"id": 1, "file_name": "sample.png", "width": 100, "height": 100}],
            "categories": [{"id": 1, "name": "scratch", "supercategory": ""}],
            "annotations": [{
                "id": 1,
                "image_id": 1,
                "category_id": 1,
                "bbox": [10.0, 20.0, 30.0, 40.0],
                "area": 1200.0,
                "iscrowd": 0
            }]
        })";
        coco_file.close();

        dltool::data::COCOIO importer;
        importer.setTargetMethod(static_cast<int>(dltool::core::DeepLearningMethod::Segmentation));

        std::vector<dltool::data::ImportedLabel> labels;
        std::atomic_bool                        finished{false};
        std::atomic_bool                        success{false};

        QObject::connect(&importer, &dltool::data::DataIO::dataBatchReady, &importer,
                         [&labels](int64_t, std::vector<QString>, std::vector<int64_t>, std::vector<int64_t>,
                                   std::map<QString, QString>, std::vector<dltool::data::ImportedLabel> batch,
                                   int64_t, int64_t)
                         { labels = std::move(batch); },
                         Qt::DirectConnection);
        QObject::connect(&importer, &dltool::data::DataIO::importFinished, &importer,
                         [&finished, &success](bool ok, std::vector<int64_t>, std::vector<int64_t>)
                         {
                             success.store(ok, std::memory_order_release);
                             finished.store(true, std::memory_order_release);
                         },
                         Qt::DirectConnection);

        importer.startImport(1, image_dir, data_dir);
        QTRY_VERIFY_WITH_TIMEOUT(finished.load(std::memory_order_acquire), 10000);
        QVERIFY(importer.waitForDone(5000));
        QVERIFY(success.load(std::memory_order_acquire));
        QCOMPARE(labels.size(), size_t(1));

        // In Segmentation, bbox must be converted to a 4-point polygon
        const QVariantList points = labels.front().data.value(QStringLiteral("points")).toList();
        QCOMPARE(points.size(), 4);
        QCOMPARE(labels.front().data.value(QStringLiteral("point_count")).toInt(), 4);
        QCOMPARE(labels.front().data.value(QStringLiteral("x")).toDouble(), 10.0);
        QCOMPARE(labels.front().data.value(QStringLiteral("y")).toDouble(), 20.0);
        QCOMPARE(labels.front().data.value(QStringLiteral("width")).toDouble(), 30.0);
        QCOMPARE(labels.front().data.value(QStringLiteral("height")).toDouble(), 40.0);
    }

    void cocoImportsBboxAsBoundingBoxInDetectionProject()
    {
        QTemporaryDir temporary_dir;
        QVERIFY(temporary_dir.isValid());

        const QString image_dir = QDir(temporary_dir.path()).filePath(QStringLiteral("images"));
        const QString data_dir  = QDir(temporary_dir.path()).filePath(QStringLiteral("annotations"));
        QVERIFY(QDir().mkpath(image_dir));
        QVERIFY(QDir().mkpath(data_dir));

        const QString image_path = QDir(image_dir).filePath(QStringLiteral("sample.png"));
        QImage        image(QSize(100, 100), QImage::Format_RGB32);
        image.fill(Qt::white);
        QVERIFY(image.save(image_path));

        const QString coco_json_path = QDir(data_dir).filePath(QStringLiteral("instances.json"));
        QFile         coco_file(coco_json_path);
        QVERIFY(coco_file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream stream(&coco_file);
        stream << R"({
            "images": [{"id": 1, "file_name": "sample.png", "width": 100, "height": 100}],
            "categories": [{"id": 1, "name": "scratch", "supercategory": ""}],
            "annotations": [{
                "id": 1,
                "image_id": 1,
                "category_id": 1,
                "bbox": [10.0, 20.0, 30.0, 40.0],
                "area": 1200.0,
                "iscrowd": 0
            }]
        })";
        coco_file.close();

        dltool::data::COCOIO importer;
        importer.setTargetMethod(static_cast<int>(dltool::core::DeepLearningMethod::Detection));

        std::vector<dltool::data::ImportedLabel> labels;
        std::atomic_bool                        finished{false};
        std::atomic_bool                        success{false};

        QObject::connect(&importer, &dltool::data::DataIO::dataBatchReady, &importer,
                         [&labels](int64_t, std::vector<QString>, std::vector<int64_t>, std::vector<int64_t>,
                                   std::map<QString, QString>, std::vector<dltool::data::ImportedLabel> batch,
                                   int64_t, int64_t)
                         { labels = std::move(batch); },
                         Qt::DirectConnection);
        QObject::connect(&importer, &dltool::data::DataIO::importFinished, &importer,
                         [&finished, &success](bool ok, std::vector<int64_t>, std::vector<int64_t>)
                         {
                             success.store(ok, std::memory_order_release);
                             finished.store(true, std::memory_order_release);
                         },
                         Qt::DirectConnection);

        importer.startImport(1, image_dir, data_dir);
        QTRY_VERIFY_WITH_TIMEOUT(finished.load(std::memory_order_acquire), 10000);
        QVERIFY(importer.waitForDone(5000));
        QVERIFY(success.load(std::memory_order_acquire));
        QCOMPARE(labels.size(), size_t(1));

        // In Detection, points should be empty
        const QVariantList points = labels.front().data.value(QStringLiteral("points")).toList();
        QCOMPARE(points.size(), 0);
        QCOMPARE(labels.front().data.value(QStringLiteral("x")).toDouble(), 10.0);
        QCOMPARE(labels.front().data.value(QStringLiteral("y")).toDouble(), 20.0);
        QCOMPARE(labels.front().data.value(QStringLiteral("width")).toDouble(), 30.0);
        QCOMPARE(labels.front().data.value(QStringLiteral("height")).toDouble(), 40.0);
    }

    void cocoExportsFourPointSegmentationForBboxLabelInSegmentationProject()
    {
        QTemporaryDir temporary_dir;
        QVERIFY(temporary_dir.isValid());

        const QString image_path = QDir(temporary_dir.path()).filePath(QStringLiteral("source.png"));
        QImage        image(QSize(100, 100), QImage::Format_RGB32);
        image.fill(Qt::white);
        QVERIFY(image.save(image_path));

        dltool::data::ExportDataset dataset;
        dataset.dataset_name = QStringLiteral("coco-seg-export");
        dltool::data::ExportImage export_image;
        export_image.image_id = 1;
        export_image.path     = image_path;
        export_image.width    = 100;
        export_image.height   = 100;
        dataset.images.push_back(export_image);

        dltool::data::ExportLabelClass export_class;
        export_class.id   = 1;
        export_class.name = QStringLiteral("defect");
        dataset.label_classes.push_back(export_class);

        dltool::data::ExportLabel label;
        label.label_id       = 1;
        label.image_id       = 1;
        label.label_class_id = 1;
        // Label only has bbox (no points)
        label.data = QVariantMap{
            {     "x", 15.0},
            {     "y", 25.0},
            { "width", 40.0},
            {"height", 50.0},
        };
        dataset.labels.push_back(label);

        dltool::data::COCOIO exporter;
        exporter.setTargetMethod(static_cast<int>(dltool::core::DeepLearningMethod::Segmentation));

        const QString output_dir = QDir(temporary_dir.path()).filePath(QStringLiteral("output"));
        bool          export_ok  = false;
        QString       export_msg;
        std::atomic_bool finished{false};
        QObject::connect(&exporter, &dltool::data::DataIO::exportFinished, &exporter,
                         [&export_ok, &export_msg, &finished](bool ok, const QString &msg)
                         {
                             export_ok = ok;
                             export_msg = msg;
                             finished.store(true, std::memory_order_release);
                         },
                         Qt::DirectConnection);

        exporter.startExport(dataset, output_dir);
        QTRY_VERIFY_WITH_TIMEOUT(finished.load(std::memory_order_acquire), 10000);
        QVERIFY(exporter.waitForDone(5000));
        QVERIFY2(export_ok, qPrintable(export_msg));

        // Read exported instances.json
        const QString instances_path = QDir(output_dir).filePath(QStringLiteral("annotations/instances.json"));
        QFile file(instances_path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto json = nlohmann::json::parse(file.readAll().toStdString());
        QVERIFY(json.contains("annotations"));
        QCOMPARE(json["annotations"].size(), size_t(1));
        const auto &ann = json["annotations"][0];
        QVERIFY(ann.contains("segmentation"));
        QVERIFY(ann["segmentation"].is_array());
        // In Segmentation project, segmentation must not be empty []
        QCOMPARE(ann["segmentation"].size(), size_t(1));
        const auto &seg = ann["segmentation"][0];
        QCOMPARE(seg.size(), size_t(8));
        QCOMPARE(seg[0].get<double>(), 15.0);
        QCOMPARE(seg[1].get<double>(), 25.0);
        QCOMPARE(seg[2].get<double>(), 55.0);
        QCOMPARE(seg[3].get<double>(), 25.0);
        QCOMPARE(seg[4].get<double>(), 55.0);
        QCOMPARE(seg[5].get<double>(), 75.0);
        QCOMPARE(seg[6].get<double>(), 15.0);
        QCOMPARE(seg[7].get<double>(), 75.0);
    }

    void maskImportsSmallSinglePixelRegionWithoutDropping()
    {
        QTemporaryDir temporary_dir;
        QVERIFY(temporary_dir.isValid());

        const QString image_dir = QDir(temporary_dir.path()).filePath(QStringLiteral("images"));
        const QString mask_dir  = QDir(temporary_dir.path()).filePath(QStringLiteral("masks"));
        QVERIFY(QDir().mkpath(image_dir));
        QVERIFY(QDir().mkpath(mask_dir));

        const QString image_path = QDir(image_dir).filePath(QStringLiteral("sample.png"));
        QImage        image(QSize(50, 50), QImage::Format_RGB32);
        image.fill(Qt::white);
        QVERIFY(image.save(image_path));

        // Mask with a single pixel at (20, 25)
        const QString mask_path = QDir(mask_dir).filePath(QStringLiteral("sample.png"));
        QImage        mask(QSize(50, 50), QImage::Format_Grayscale8);
        mask.fill(0);
        mask.setPixel(20, 25, qRgb(255, 255, 255));
        QVERIFY(mask.save(mask_path));

        dltool::data::MaskIO importer;
        importer.setTargetMethod(static_cast<int>(dltool::core::DeepLearningMethod::AnomalyDetection));

        std::vector<dltool::data::ImportedLabel> labels;
        std::atomic_bool                        finished{false};
        std::atomic_bool                        success{false};

        QObject::connect(&importer, &dltool::data::DataIO::dataBatchReady, &importer,
                         [&labels](int64_t, std::vector<QString>, std::vector<int64_t>, std::vector<int64_t>,
                                   std::map<QString, QString>, std::vector<dltool::data::ImportedLabel> batch,
                                   int64_t, int64_t)
                         { labels = std::move(batch); },
                         Qt::DirectConnection);
        QObject::connect(&importer, &dltool::data::DataIO::importFinished, &importer,
                         [&finished, &success](bool ok, std::vector<int64_t>, std::vector<int64_t>)
                         {
                             success.store(ok, std::memory_order_release);
                             finished.store(true, std::memory_order_release);
                         },
                         Qt::DirectConnection);

        importer.startImport(1, image_dir, mask_dir);
        QTRY_VERIFY_WITH_TIMEOUT(finished.load(std::memory_order_acquire), 10000);
        QVERIFY(importer.waitForDone(5000));
        QVERIFY(success.load(std::memory_order_acquire));
        // Single pixel region must not be discarded
        QCOMPARE(labels.size(), size_t(1));

        const auto &lbl = labels.front();
        QCOMPARE(lbl.data.value(QStringLiteral("x")).toDouble(), 20.0);
        QCOMPARE(lbl.data.value(QStringLiteral("y")).toDouble(), 25.0);
        QCOMPARE(lbl.data.value(QStringLiteral("width")).toDouble(), 1.0);
        QCOMPARE(lbl.data.value(QStringLiteral("height")).toDouble(), 1.0);
        const QVariantList points = lbl.data.value(QStringLiteral("points")).toList();
        QCOMPARE(points.size(), 4);
    }

    void maskImportsHoleAsOuterBoundaryAndReExportFillsHole()
    {
        QTemporaryDir temporary_dir;
        QVERIFY(temporary_dir.isValid());

        const QString image_dir = QDir(temporary_dir.path()).filePath(QStringLiteral("images"));
        const QString mask_dir  = QDir(temporary_dir.path()).filePath(QStringLiteral("masks"));
        QVERIFY(QDir().mkpath(image_dir));
        QVERIFY(QDir().mkpath(mask_dir));

        const QString image_path = QDir(image_dir).filePath(QStringLiteral("sample.png"));
        QImage        image(QSize(50, 50), QImage::Format_RGB32);
        image.fill(Qt::white);
        QVERIFY(image.save(image_path));

        // Mask with outer [10..30, 10..30] and inner hole [18..22, 18..22]
        const QString mask_path = QDir(mask_dir).filePath(QStringLiteral("sample.png"));
        QImage        mask(QSize(50, 50), QImage::Format_Grayscale8);
        mask.fill(0);
        for (int y = 10; y <= 30; ++y)
        {
            for (int x = 10; x <= 30; ++x)
            {
                if (x >= 18 && x <= 22 && y >= 18 && y <= 22)
                    continue;
                mask.setPixel(x, y, qRgb(255, 255, 255));
            }
        }
        QVERIFY(mask.save(mask_path));

        dltool::data::MaskIO importer;
        importer.setTargetMethod(static_cast<int>(dltool::core::DeepLearningMethod::Segmentation));

        std::vector<dltool::data::ImportedLabel> labels;
        std::atomic_bool                        finished{false};
        std::atomic_bool                        success{false};

        QObject::connect(&importer, &dltool::data::DataIO::dataBatchReady, &importer,
                         [&labels](int64_t, std::vector<QString>, std::vector<int64_t>, std::vector<int64_t>,
                                   std::map<QString, QString>, std::vector<dltool::data::ImportedLabel> batch,
                                   int64_t, int64_t)
                         { labels = std::move(batch); },
                         Qt::DirectConnection);
        QObject::connect(&importer, &dltool::data::DataIO::importFinished, &importer,
                         [&finished, &success](bool ok, std::vector<int64_t>, std::vector<int64_t>)
                         {
                             success.store(ok, std::memory_order_release);
                             finished.store(true, std::memory_order_release);
                         },
                         Qt::DirectConnection);

        importer.startImport(1, image_dir, mask_dir);
        QTRY_VERIFY_WITH_TIMEOUT(finished.load(std::memory_order_acquire), 10000);
        QVERIFY(importer.waitForDone(5000));
        QVERIFY(success.load(std::memory_order_acquire));
        QCOMPARE(labels.size(), size_t(1));

        // Outer boundary bounds covers [10, 30]
        const auto &lbl = labels.front();
        QVERIFY(lbl.data.value(QStringLiteral("x")).toDouble() <= 10.0);
        QVERIFY(lbl.data.value(QStringLiteral("y")).toDouble() <= 10.0);
        QVERIFY(lbl.data.value(QStringLiteral("x")).toDouble() + lbl.data.value(QStringLiteral("width")).toDouble() >= 30.0);
        QVERIFY(lbl.data.value(QStringLiteral("y")).toDouble() + lbl.data.value(QStringLiteral("height")).toDouble() >= 30.0);

        // Export this back to Mask
        dltool::data::ExportDataset export_dataset;
        export_dataset.dataset_name = QStringLiteral("hole-verify");
        dltool::data::ExportImage exp_img;
        exp_img.image_id = 1;
        exp_img.path     = image_path;
        exp_img.width    = 50;
        exp_img.height   = 50;
        export_dataset.images.push_back(exp_img);

        dltool::data::ExportLabelClass exp_cls;
        exp_cls.id   = 1;
        exp_cls.name = lbl.label_class_name;
        export_dataset.label_classes.push_back(exp_cls);

        dltool::data::ExportLabel exp_lbl;
        exp_lbl.label_id       = 1;
        exp_lbl.image_id       = 1;
        exp_lbl.label_class_id = 1;
        exp_lbl.data           = lbl.data;
        export_dataset.labels.push_back(exp_lbl);

        dltool::data::MaskIO exporter;
        exporter.setTargetMethod(static_cast<int>(dltool::core::DeepLearningMethod::Segmentation));
        const QString export_output_dir = QDir(temporary_dir.path()).filePath(QStringLiteral("mask_output"));
        bool export_ok = false;
        std::atomic_bool export_finished{false};
        QObject::connect(&exporter, &dltool::data::DataIO::exportFinished, &exporter,
                         [&export_ok, &export_finished](bool ok, const QString &)
                         {
                             export_ok = ok;
                             export_finished.store(true, std::memory_order_release);
                         },
                         Qt::DirectConnection);

        exporter.startExport(export_dataset, export_output_dir);
        QTRY_VERIFY_WITH_TIMEOUT(export_finished.load(std::memory_order_acquire), 10000);
        QVERIFY(exporter.waitForDone(5000));
        QVERIFY(export_ok);

        // Re-exported mask: the center pixel (20, 20) inside the original hole is now filled (not 0)
        const QString reexported_mask_path = QDir(export_output_dir).filePath(QStringLiteral("masks/sample.png"));
        QImage reexported(reexported_mask_path);
        QVERIFY(!reexported.isNull());
        // The hole was filled because simple polygon cannot represent holes
        QVERIFY(qGray(reexported.pixel(20, 20)) > 0);
    }

    void maskScalesCoordinatesWhenMaskSizeDiffersFromImage()
    {
        QTemporaryDir temporary_dir;
        QVERIFY(temporary_dir.isValid());

        const QString image_dir = QDir(temporary_dir.path()).filePath(QStringLiteral("images"));
        const QString mask_dir  = QDir(temporary_dir.path()).filePath(QStringLiteral("masks"));
        QVERIFY(QDir().mkpath(image_dir));
        QVERIFY(QDir().mkpath(mask_dir));

        // Non-square image 200x100
        const QString image_path = QDir(image_dir).filePath(QStringLiteral("sample.png"));
        QImage        image(QSize(200, 100), QImage::Format_RGB32);
        image.fill(Qt::white);
        QVERIFY(image.save(image_path));

        // Half-size mask 100x50
        const QString mask_path = QDir(mask_dir).filePath(QStringLiteral("sample.png"));
        QImage        mask(QSize(100, 50), QImage::Format_Grayscale8);
        mask.fill(0);
        // Box in mask [10..29] x [10..24] (width 20, height 15)
        for (int y = 10; y < 25; ++y)
        {
            for (int x = 10; x < 30; ++x)
            {
                mask.setPixel(x, y, qRgb(255, 255, 255));
            }
        }
        QVERIFY(mask.save(mask_path));

        dltool::data::MaskIO importer;
        importer.setTargetMethod(static_cast<int>(dltool::core::DeepLearningMethod::Detection));

        std::vector<dltool::data::ImportedLabel> labels;
        std::atomic_bool                        finished{false};
        std::atomic_bool                        success{false};

        QObject::connect(&importer, &dltool::data::DataIO::dataBatchReady, &importer,
                         [&labels](int64_t, std::vector<QString>, std::vector<int64_t>, std::vector<int64_t>,
                                   std::map<QString, QString>, std::vector<dltool::data::ImportedLabel> batch,
                                   int64_t, int64_t)
                         { labels = std::move(batch); },
                         Qt::DirectConnection);
        QObject::connect(&importer, &dltool::data::DataIO::importFinished, &importer,
                         [&finished, &success](bool ok, std::vector<int64_t>, std::vector<int64_t>)
                         {
                             success.store(ok, std::memory_order_release);
                             finished.store(true, std::memory_order_release);
                         },
                         Qt::DirectConnection);

        importer.startImport(1, image_dir, mask_dir);
        QTRY_VERIFY_WITH_TIMEOUT(finished.load(std::memory_order_acquire), 10000);
        QVERIFY(importer.waitForDone(5000));
        QVERIFY(success.load(std::memory_order_acquire));
        QCOMPARE(labels.size(), size_t(1));

        const auto &lbl = labels.front();
        // Scaled to image size (x*2, y*2) within rasterization discretization tolerance
        QVERIFY(std::abs(lbl.data.value(QStringLiteral("x")).toDouble() - 20.0) <= 2.0);
        QVERIFY(std::abs(lbl.data.value(QStringLiteral("y")).toDouble() - 20.0) <= 2.0);
        QVERIFY(std::abs(lbl.data.value(QStringLiteral("width")).toDouble() - 40.0) <= 2.0);
        QVERIFY(std::abs(lbl.data.value(QStringLiteral("height")).toDouble() - 30.0) <= 2.0);
    }
};

QTEST_GUILESS_MAIN(DataGeometryFormatsTest)

#include "test_DataGeometryFormats.moc"
