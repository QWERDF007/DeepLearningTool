#include "data/DataIO.h"
#include "data/DataFormat.h"
#include "data/DatasetIO.h"

#include "common/Utils.h"
#include "ui/ProgressManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <QVector>

#include <algorithm>
#include <utility>

class DataIOExportTest final : public QObject
{
    Q_OBJECT

private slots:
    void exportValidationRejectsMissingArtifact()
    {
        QTemporaryDir temporary_dir;
        QVERIFY(temporary_dir.isValid());

        dltool::data::ExportDataset dataset;
        dataset.dataset_name = QStringLiteral("validation-test");
        dltool::data::ExportImage image;
        image.image_id = 1;
        dataset.images.push_back(image);

        const QString output_dir = QDir(temporary_dir.path()).filePath(QStringLiteral("output"));
        QVERIFY(QDir().mkpath(QDir(output_dir).filePath(QStringLiteral("images"))));
        QVERIFY(QDir().mkpath(QDir(output_dir).filePath(QStringLiteral("annotations"))));

        QImage exported_image(QSize(16, 16), QImage::Format_RGB32);
        exported_image.fill(Qt::white);
        QVERIFY(exported_image.save(QDir(output_dir).filePath(QStringLiteral("images/image.png"))));

        QString error;
        QVERIFY(!dltool::data::DataIO::validateExportOutput(dltool::data::DataFormat::LabelMe, dataset, output_dir,
                                                             {}, error));
        QVERIFY2(error.contains(QStringLiteral("标注")), qPrintable(error));
    }

    void labelMeReportsProgressDuringExport()
    {
        QTemporaryDir temporary_dir;
        QVERIFY(temporary_dir.isValid());

        dltool::data::ExportDataset dataset;
        dataset.dataset_name = QStringLiteral("progress-test");
        for (int index = 0; index < 10; ++index)
        {
            const QString image_path = QDir(temporary_dir.path()).filePath(QString("source_%1.png").arg(index));
            QImage        image(QSize(16, 16), QImage::Format_RGB32);
            image.fill(Qt::white);
            QVERIFY(image.save(image_path));

            dltool::data::ExportImage export_image;
            export_image.image_id = index + 1;
            export_image.path     = image_path;
            export_image.width    = image.width();
            export_image.height   = image.height();
            dataset.images.push_back(std::move(export_image));
        }

        auto *progress = dltool::ui::ProgressManager::getInstance();
        progress->reset();

        QVector<int> progress_values;
        bool         finished = false;
        bool         success  = false;
        QString      message;
        const QMetaObject::Connection progress_connection
            = connect(progress, &dltool::ui::ProgressManager::progressChanged, this,
                      [progress, &progress_values]() { progress_values.push_back(progress->getProgress()); });
        connect(&exporter_, &dltool::data::DataIO::exportFinished, this,
                [&finished, &success, &message](const bool export_success, const QString &export_message)
                {
                    finished = true;
                    success  = export_success;
                    message  = export_message;
                });

        exporter_.startExport(dataset, QDir(temporary_dir.path()).filePath(QStringLiteral("output")));
        QTRY_VERIFY_WITH_TIMEOUT(finished, 10000);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

        QVERIFY2(success, qPrintable(message));
        QVERIFY(std::any_of(progress_values.cbegin(), progress_values.cend(),
                            [](const int value) { return value > 1 && value < 45; }));

        disconnect(progress_connection);
        progress->reset();
    }

    void exportRejectsSourceAndTargetCollisionWithoutDeletingSource()
    {
        QTemporaryDir temporary_dir;
        QVERIFY(temporary_dir.isValid());

        const QString export_output_dir = QDir(temporary_dir.path()).filePath(QStringLiteral("colliding_export"));
        const QString colliding_source_dir = QDir(export_output_dir).filePath(QStringLiteral("images"));
        QVERIFY(QDir().mkpath(colliding_source_dir));

        const QString colliding_img = QDir(colliding_source_dir).filePath(QStringLiteral("collision_img.png"));
        QImage image(QSize(16, 16), QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(colliding_img));
        const qint64 original_file_size = QFileInfo(colliding_img).size();
        QVERIFY(original_file_size > 0);

        dltool::data::ExportDataset dataset;
        dataset.dataset_name = QStringLiteral("direct-collision");
        dltool::data::ExportImage direct_img;
        direct_img.image_id = 1;
        direct_img.path     = colliding_img;
        direct_img.width    = 16;
        direct_img.height   = 16;
        dataset.images.push_back(direct_img);

        bool finished = false;
        bool success  = false;
        QString message;
        dltool::data::LabelMeIO exporter;
        connect(&exporter, &dltool::data::DataIO::exportFinished, this,
                [&finished, &success, &message](const bool s, const QString &m)
                {
                    finished = true;
                    success  = s;
                    message  = m;
                });

        exporter.startExport(dataset, export_output_dir);
        QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
        QCOMPARE(success, false);
        QVERIFY2(message.contains(QStringLiteral("冲突")) || message.contains(QStringLiteral("同一文件")),
                 qPrintable(message));

        // Critical: source file must NOT be deleted or truncated!
        QVERIFY(QFile::exists(colliding_img));
        QCOMPARE(QFileInfo(colliding_img).size(), original_file_size);
    }

    void failedExportPreservesPreExistingTargetContents()
    {
        QTemporaryDir temporary_dir;
        QVERIFY(temporary_dir.isValid());

        const QString output_dir = QDir(temporary_dir.path()).filePath(QStringLiteral("existing_target"));
        QVERIFY(QDir().mkpath(output_dir));
        const QString sentinel_file = QDir(output_dir).filePath(QStringLiteral("pre_existing_data.txt"));
        QFile file(sentinel_file);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("important existing output");
        file.close();

        // Prepare dataset with a missing image to trigger failure during export
        dltool::data::ExportDataset dataset;
        dataset.dataset_name = QStringLiteral("failed-export");
        dltool::data::ExportImage image;
        image.image_id = 1;
        image.path     = QDir(temporary_dir.path()).filePath(QStringLiteral("non_existent_image.png"));
        image.width    = 16;
        image.height   = 16;
        dataset.images.push_back(image);

        bool finished = false;
        bool success  = false;
        QString message;
        dltool::data::LabelMeIO exporter;
        connect(&exporter, &dltool::data::DataIO::exportFinished, this,
                [&finished, &success, &message](const bool s, const QString &m)
                {
                    finished = true;
                    success  = s;
                    message  = m;
                });

        exporter.startExport(dataset, output_dir);
        QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
        QCOMPARE(success, false);

        // Pre-existing target content must be preserved intact!
        QVERIFY(QFile::exists(sentinel_file));
        QFile check_file(sentinel_file);
        QVERIFY(check_file.open(QIODevice::ReadOnly | QIODevice::Text));
        QCOMPARE(check_file.readAll(), QByteArray("important existing output"));
    }

    void successfulExportAtomicallyPublishesStagedOutput()
    {
        QTemporaryDir temporary_dir;
        QVERIFY(temporary_dir.isValid());

        const QString source_image_path = QDir(temporary_dir.path()).filePath(QStringLiteral("valid_source.png"));
        QImage image(QSize(16, 16), QImage::Format_RGB32);
        image.fill(Qt::blue);
        QVERIFY(image.save(source_image_path));

        const QString output_dir = QDir(temporary_dir.path()).filePath(QStringLiteral("overwrite_target"));
        QVERIFY(QDir().mkpath(output_dir));
        const QString old_file = QDir(output_dir).filePath(QStringLiteral("old_file.txt"));
        QFile file(old_file);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("old data");
        file.close();

        dltool::data::ExportDataset dataset;
        dataset.dataset_name = QStringLiteral("success-export");
        dltool::data::ExportImage export_image;
        export_image.image_id = 1;
        export_image.path     = source_image_path;
        export_image.width    = 16;
        export_image.height   = 16;
        dataset.images.push_back(export_image);

        bool finished = false;
        bool success  = false;
        QString message;
        dltool::data::LabelMeIO exporter;
        connect(&exporter, &dltool::data::DataIO::exportFinished, this,
                [&finished, &success, &message](const bool s, const QString &m)
                {
                    finished = true;
                    success  = s;
                    message  = m;
                });

        exporter.startExport(dataset, output_dir);
        QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
        QVERIFY2(success, qPrintable(message));

        QString error;
        QVERIFY(dltool::data::DataIO::validateExportOutput(dltool::data::DataFormat::LabelMe, dataset, output_dir, {},
                                                           error));
    }

    void pathResolutionValidatesExplicitRootAndUncSemantics()
    {
        QString err;
        QString resolved;

        // 1. Valid local absolute path
        QVERIFY(dltool::data::DatasetIO::resolveExportPath(QStringLiteral("C:/valid/local/path"), {}, resolved, err));
        QCOMPARE(resolved, QStringLiteral("C:/valid/local/path"));

        // 2. Relative path with explicit root
        QVariantMap options;
        options[QStringLiteral("base_dir")] = QStringLiteral("D:/my_project");
        QVERIFY(dltool::data::DatasetIO::resolveExportPath(QStringLiteral("relative/export"), options, resolved, err));
        QCOMPARE(resolved, QStringLiteral("D:/my_project/relative/export"));

        // 3. Relative path WITHOUT explicit root -> rejected
        QVERIFY(!dltool::data::DatasetIO::resolveExportPath(QStringLiteral("relative/export"), {}, resolved, err));
        QVERIFY(err.contains(QStringLiteral("绝对路径")));

        // 4. Valid UNC path -> retains UNC network semantics
        QVERIFY(dltool::data::DatasetIO::resolveExportPath(QStringLiteral("//192.168.1.100/share/export"), {}, resolved, err));
        QCOMPARE(resolved, QStringLiteral("//192.168.1.100/share/export"));

        // 5. Malformed UNC path (missing share name) -> rejected, never falls back to app dir
        const QString app_dir = QCoreApplication::applicationDirPath();
        QVERIFY(!dltool::data::DatasetIO::resolveExportPath(QStringLiteral("//invalid_server"), {}, resolved, err));
        QVERIFY(err.contains(QStringLiteral("UNC")));
        QVERIFY(!resolved.contains(app_dir));
        QVERIFY(!err.contains(app_dir));

        // 6. Windows native backslash UNC path -> retains UNC network semantics
        QVERIFY(dltool::data::DatasetIO::resolveExportPath(QStringLiteral("\\\\192.168.1.100\\share\\export"), {}, resolved, err));
        QVERIFY(resolved.contains(QStringLiteral("192.168.1.100/share/export")));

        // 7. Malformed backslash UNC paths -> rejected, never falls back to app dir
        QVERIFY(!dltool::data::DatasetIO::resolveExportPath(QStringLiteral("\\\\invalid_server"), {}, resolved, err));
        QVERIFY(err.contains(QStringLiteral("UNC")));
        QVERIFY(!resolved.contains(app_dir));
        QVERIFY(!err.contains(app_dir));

        QVERIFY(!dltool::data::DatasetIO::resolveExportPath(QStringLiteral("\\\\"), {}, resolved, err));
        QVERIFY(err.contains(QStringLiteral("UNC")));
        QVERIFY(!resolved.contains(app_dir));
        QVERIFY(!err.contains(app_dir));
    }

    void exportersUseConsistentContractOnCancellation()
    {
        const QVector<int> formats = {
            dltool::data::DataFormat::Folder,
            dltool::data::DataFormat::Mask,
            dltool::data::DataFormat::LabelMe,
            dltool::data::DataFormat::COCO,
        };

        for (const int format : formats)
        {
            QTemporaryDir temporary_dir;
            QVERIFY(temporary_dir.isValid());

            dltool::data::ExportDataset dataset;
            dataset.dataset_name = QStringLiteral("cancel-contract-test");
            for (int index = 0; index < 20; ++index)
            {
                const QString image_path = QDir(temporary_dir.path()).filePath(QString("source_%1.png").arg(index));
                QImage        image(QSize(32, 32), QImage::Format_RGB32);
                image.fill(Qt::blue);
                QVERIFY(image.save(image_path));

                dltool::data::ExportImage export_image;
                export_image.image_id = index + 1;
                export_image.path     = image_path;
                export_image.width    = image.width();
                export_image.height   = image.height();
                dataset.images.push_back(std::move(export_image));

                dltool::data::ExportLabel export_label;
                export_label.label_id       = index + 1;
                export_label.image_id       = index + 1;
                export_label.label_class_id = 1;
                export_label.data           = {
                    {QStringLiteral("x"),      0 },
                    {QStringLiteral("y"),      0 },
                    {QStringLiteral("width"),  10},
                    {QStringLiteral("height"), 10}
                };
                dataset.labels.push_back(std::move(export_label));
            }
            dataset.label_classes.push_back({1, QStringLiteral("defect"), QStringLiteral("#FF0000")});

            const QString target_dir = QDir(temporary_dir.path()).filePath(QString("target_%1").arg(format));

            auto *exporter = dltool::data::DataIO::createIO(format, this);
            QVERIFY(exporter != nullptr);

            bool    finished = false;
            bool    success  = true;
            QString message;

            connect(exporter, &dltool::data::DataIO::exportFinished, this,
                    [&finished, &success, &message](const bool s, const QString &m)
                    {
                        finished = true;
                        success  = s;
                        message  = m;
                    });

            exporter->startExport(dataset, target_dir);
            exporter->requestCancel();

            QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
            QCOMPARE(success, false);
            QVERIFY2(message.contains(QStringLiteral("取消")), qPrintable(message));

            exporter->deleteLater();
        }
    }

    void manifestValidationVerifiesFileExistenceAndContent()
    {
        QTemporaryDir temporary_dir;
        QVERIFY(temporary_dir.isValid());

        dltool::data::ExportDataset dataset;
        dataset.dataset_name = QStringLiteral("manifest-test");
        dltool::data::ExportImage image;
        image.image_id = 1;
        image.path     = QStringLiteral("sample.png");
        dataset.images.push_back(image);

        // 1. COCO with instances.json referencing non-existent image
        {
            const QString coco_dir = QDir(temporary_dir.path()).filePath(QStringLiteral("coco_test"));
            QVERIFY(QDir().mkpath(QDir(coco_dir).filePath(QStringLiteral("images"))));
            QVERIFY(QDir().mkpath(QDir(coco_dir).filePath(QStringLiteral("annotations"))));

            QImage dummy_img(QSize(16, 16), QImage::Format_RGB32);
            dummy_img.fill(Qt::white);
            QVERIFY(dummy_img.save(QDir(coco_dir).filePath(QStringLiteral("images/actual_sample.png"))));

            // write instances.json with mismatched file_name
            QFile json_file(QDir(coco_dir).filePath(QStringLiteral("annotations/instances.json")));
            QVERIFY(json_file.open(QIODevice::WriteOnly | QIODevice::Text));
            json_file.write(R"({
  "images": [
    {"id": 1, "file_name": "missing_file.png", "width": 16, "height": 16}
  ],
  "annotations": [],
  "categories": [
    {"id": 1, "name": "defect"}
  ]
})");
            json_file.close();

            QString err;
            QVERIFY(!dltool::data::DataIO::validateExportOutput(dltool::data::DataFormat::COCO, dataset, coco_dir, {}, err));
            QVERIFY2(err.contains(QStringLiteral("清单")) || err.contains(QStringLiteral("missing_file.png")) || err.contains(QStringLiteral("不存在")),
                     qPrintable(err));
        }

        // 2. Folder with empty 0-byte image
        {
            const QString folder_dir = QDir(temporary_dir.path()).filePath(QStringLiteral("folder_test"));
            QVERIFY(QDir().mkpath(folder_dir));
            QFile empty_file(QDir(folder_dir).filePath(QStringLiteral("sample.png")));
            QVERIFY(empty_file.open(QIODevice::WriteOnly));
            empty_file.close(); // 0 bytes

            QString err;
            QVERIFY(!dltool::data::DataIO::validateExportOutput(dltool::data::DataFormat::Folder, dataset, folder_dir, {}, err));
            QVERIFY2(err.contains(QStringLiteral("无效")) || err.contains(QStringLiteral("0")), qPrintable(err));
        }
    }

    void batchExportCancellationHaltsSubsequentDatasetsAndPreservesCompletedArtifacts()
    {
        QTemporaryDir temporary_dir;
        QVERIFY(temporary_dir.isValid());

        // Create Dataset 1 (5 images)
        dltool::data::ExportDataset dataset1;
        dataset1.dataset_name = QStringLiteral("batch_ds1");
        for (int i = 0; i < 5; ++i)
        {
            const QString img_path = QDir(temporary_dir.path()).filePath(QString("ds1_%1.png").arg(i));
            QImage img(16, 16, QImage::Format_RGB32);
            img.fill(Qt::blue);
            QVERIFY(img.save(img_path));
            dltool::data::ExportImage eimg;
            eimg.image_id = i + 1;
            eimg.path = img_path;
            eimg.width = 16;
            eimg.height = 16;
            dataset1.images.push_back(eimg);
        }

        // Export dataset 1 fully
        const QString out_dir1 = QDir(temporary_dir.path()).filePath(QStringLiteral("out_ds1"));
        dltool::data::LabelMeIO exp1;
        bool exp1_done = false;
        bool exp1_ok = false;
        connect(&exp1, &dltool::data::DataIO::exportFinished, this,
                [&exp1_done, &exp1_ok](bool ok, const QString &) {
                    exp1_done = true;
                    exp1_ok = ok;
                });
        exp1.startExport(dataset1, out_dir1);
        QTRY_VERIFY_WITH_TIMEOUT(exp1_done, 5000);
        QVERIFY(exp1_ok);

        // Verify dataset 1 output is valid
        QString err;
        QVERIFY(dltool::data::DataIO::validateExportOutput(dltool::data::DataFormat::LabelMe, dataset1, out_dir1, {}, err));

        // Create Dataset 2 (many images, to be cancelled)
        dltool::data::ExportDataset dataset2;
        dataset2.dataset_name = QStringLiteral("batch_ds2");
        for (int i = 0; i < 50; ++i)
        {
            const QString img_path = QDir(temporary_dir.path()).filePath(QString("ds2_%1.png").arg(i));
            QImage img(16, 16, QImage::Format_RGB32);
            img.fill(Qt::green);
            QVERIFY(img.save(img_path));
            dltool::data::ExportImage eimg;
            eimg.image_id = i + 100;
            eimg.path = img_path;
            eimg.width = 16;
            eimg.height = 16;
            dataset2.images.push_back(eimg);
        }

        const QString out_dir2 = QDir(temporary_dir.path()).filePath(QStringLiteral("out_ds2"));
        dltool::data::LabelMeIO exp2;
        bool exp2_done = false;
        bool exp2_ok = true;
        connect(&exp2, &dltool::data::DataIO::exportFinished, this,
                [&exp2_done, &exp2_ok](bool ok, const QString &) {
                    exp2_done = true;
                    exp2_ok = ok;
                });
        exp2.startExport(dataset2, out_dir2);
        // Cancel dataset 2 immediately
        exp2.requestCancel();
        QTRY_VERIFY_WITH_TIMEOUT(exp2_done, 5000);
        QVERIFY(!exp2_ok);

        // Verify dataset 1 remains 100% valid and untouched
        QVERIFY(dltool::data::DataIO::validateExportOutput(dltool::data::DataFormat::LabelMe, dataset1, out_dir1, {}, err));
    }

    void uncPathExportOverwritesAndRecoversSafelyWithReadbackValidation()
    {
        // 验证 Ticket 06 验收条件：
        // 1. 本地与 UNC 共享路径按实际目标验证，真实导出文件、目录与标注产物
        // 2. 覆盖导出成功发布，旧目标被安全替换
        // 3. 失败/异常导出时恢复原目标内容，不损坏既有成果
        // 4. 直接从 UNC 路径回读导出的图像与 JSON 标注文件，验证内容完整性

        QString unc_base = qEnvironmentVariable("DLTOOL_TEST_UNC_DIR");
        if (unc_base.trimmed().isEmpty())
        {
            const QString temp_dir = dltool::common::cleanPath(QDir::tempPath());
            if (temp_dir.length() >= 2 && temp_dir[1] == QLatin1Char(':'))
            {
                const QChar drive = temp_dir[0];
                unc_base = QStringLiteral("//127.0.0.1/%1$%2").arg(drive).arg(temp_dir.mid(2));
            }
            else
            {
                unc_base = temp_dir;
            }
        }

        if (!QDir(unc_base).exists())
        {
            QFAIL(qPrintable(QString("当前运行环境 UNC 共享路径不可用: %1。请确保 127.0.0.1 管理共享可用或配置 DLTOOL_TEST_UNC_DIR").arg(unc_base)));
            return;
        }

        const QString unc_output_dir = QDir(unc_base).filePath(
            QStringLiteral("dltool_unc_export_%1").arg(QCoreApplication::applicationPid()));

        // 清理旧残留
        if (QDir(unc_output_dir).exists())
            QDir(unc_output_dir).removeRecursively();

        QTemporaryDir local_source_dir;
        QVERIFY(local_source_dir.isValid());

        // 准备真实源图像
        const QString source_image_path = QDir(local_source_dir.path()).filePath(QStringLiteral("unc_test_img.png"));
        QImage source_image(QSize(32, 24), QImage::Format_RGB32);
        source_image.fill(Qt::green);
        QVERIFY(source_image.save(source_image_path));

        dltool::data::ExportDataset dataset1;
        dataset1.dataset_name = QStringLiteral("unc-dataset-v1");
        dltool::data::ExportImage image1;
        image1.image_id = 1;
        image1.path     = source_image_path;
        image1.width    = 32;
        image1.height   = 24;
        dltool::data::ExportLabel label1;
        label1.label_id       = 1;
        label1.image_id       = 1;
        label1.label_class_id = 1;
        label1.data           = {
            {QStringLiteral("x"),      2 },
            {QStringLiteral("y"),      2 },
            {QStringLiteral("width"),  10},
            {QStringLiteral("height"), 10}
        };
        dataset1.labels.push_back(label1);
        dataset1.label_classes.push_back({1, QStringLiteral("defect"), QStringLiteral("#FF0000")});
        dataset1.images.push_back(image1);

        // 阶段一：真实导出到 UNC 路径
        bool finished = false;
        bool success  = false;
        QString message;
        dltool::data::LabelMeIO exporter;
        connect(&exporter, &dltool::data::DataIO::exportFinished, this,
                [&finished, &success, &message](const bool s, const QString &m)
                {
                    finished = true;
                    success  = s;
                    message  = m;
                });

        exporter.startExport(dataset1, unc_output_dir);
        QTRY_VERIFY_WITH_TIMEOUT(finished, 10000);
        QVERIFY2(success, qPrintable(message));

        // 验证产物完整性与校验通过
        QString error;
        QVERIFY2(dltool::data::DataIO::validateExportOutput(dltool::data::DataFormat::LabelMe, dataset1,
                                                            unc_output_dir, {}, error),
                 qPrintable(error));

        // 真实回读校验：直接从 UNC 路径读取并解码图像与标注 JSON
        const QString exported_image_path = QDir(unc_output_dir).filePath(QStringLiteral("images/unc_test_img.png"));
        QVERIFY2(QFile::exists(exported_image_path), qPrintable(exported_image_path));
        QImage readback_image;
        QVERIFY(readback_image.load(exported_image_path));
        QCOMPARE(readback_image.size(), QSize(32, 24));

        const QString exported_json_path = QDir(unc_output_dir).filePath(QStringLiteral("annotations/unc_test_img.json"));
        QVERIFY2(QFile::exists(exported_json_path), qPrintable(exported_json_path));
        QFile json_file(exported_json_path);
        QVERIFY(json_file.open(QIODevice::ReadOnly));
        const QJsonDocument json_doc = QJsonDocument::fromJson(json_file.readAll());
        QVERIFY(!json_doc.isNull());
        QCOMPARE(json_doc.object().value(QStringLiteral("imageWidth")).toInt(), 32);
        QCOMPARE(json_doc.object().value(QStringLiteral("imageHeight")).toInt(), 24);
        json_file.close();

        // 阶段二：安全覆盖导出（写入 pre-existing sentinel 验证覆盖发布后旧目标被安全替换清除）
        const QString sentinel_file = QDir(unc_output_dir).filePath(QStringLiteral("sentinel_marker.txt"));
        QFile sentinel(sentinel_file);
        QVERIFY(sentinel.open(QIODevice::WriteOnly | QIODevice::Text));
        sentinel.write("pre_existing_data");
        sentinel.close();
        QVERIFY(QFile::exists(sentinel_file));

        finished = false;
        success  = false;
        message.clear();

        // 导出更新版数据集（包含新图像）
        const QString source_image_path2 = QDir(local_source_dir.path()).filePath(QStringLiteral("unc_test_img2.png"));
        QImage source_image2(QSize(16, 16), QImage::Format_RGB32);
        source_image2.fill(Qt::blue);
        QVERIFY(source_image2.save(source_image_path2));

        dltool::data::ExportDataset dataset2;
        dataset2.dataset_name = QStringLiteral("unc-dataset-v2");
        dltool::data::ExportImage image2;
        image2.image_id = 2;
        image2.path     = source_image_path2;
        image2.width    = 16;
        image2.height   = 16;
        dltool::data::ExportLabel label2;
        label2.label_id       = 2;
        label2.image_id       = 2;
        label2.label_class_id = 1;
        label2.data           = {
            {QStringLiteral("x"),      1 },
            {QStringLiteral("y"),      1 },
            {QStringLiteral("width"),  8 },
            {QStringLiteral("height"), 8 }
        };
        dataset2.labels.push_back(label2);
        dataset2.label_classes.push_back({1, QStringLiteral("defect"), QStringLiteral("#FF0000")});
        dataset2.images.push_back(image2);

        exporter.startExport(dataset2, unc_output_dir);
        QTRY_VERIFY_WITH_TIMEOUT(finished, 10000);
        QVERIFY2(success, qPrintable(message));

        // 验证覆写约定：旧目标的遗留文件 sentinel_marker.txt 必须已被彻底替换清除（原子目录替换，而非合并）
        QVERIFY2(!QFile::exists(sentinel_file), "覆写发布后，旧目标的遗留文件 sentinel_marker.txt 应已被彻底替换清除");
        QVERIFY2(dltool::data::DataIO::validateExportOutput(dltool::data::DataFormat::LabelMe, dataset2,
                                                            unc_output_dir, {}, error),
                 qPrintable(error));

        const QString exported_image_path2 = QDir(unc_output_dir).filePath(QStringLiteral("images/unc_test_img2.png"));
        QVERIFY2(QFile::exists(exported_image_path2), qPrintable(exported_image_path2));
        QImage readback_image2;
        QVERIFY(readback_image2.load(exported_image_path2));
        QCOMPARE(readback_image2.size(), QSize(16, 16));

        const QString exported_json_path2 = QDir(unc_output_dir).filePath(QStringLiteral("annotations/unc_test_img2.json"));
        QVERIFY2(QFile::exists(exported_json_path2), qPrintable(exported_json_path2));
        QFile json_file2(exported_json_path2);
        QVERIFY(json_file2.open(QIODevice::ReadOnly));
        const QJsonDocument json_doc2 = QJsonDocument::fromJson(json_file2.readAll());
        QVERIFY(!json_doc2.isNull());
        QCOMPARE(json_doc2.object().value(QStringLiteral("imageWidth")).toInt(), 16);
        QCOMPARE(json_doc2.object().value(QStringLiteral("imageHeight")).toInt(), 16);
        json_file2.close();

        // 阶段三：故障注入测试——真实模拟 UNC 路径在 staging publish（重命名生效）阶段发生故障
        // 验证 SafeExportScope 触发回滚机制，将 .backup 目录完全恢复为既有目标目录
        int rename_calls = 0;
        QString publish_err;
        const QString staging_canary = QStringLiteral("staging_new_data.txt");
        {
            dltool::data::SafeExportScope unc_scope(unc_output_dir,
                [&rename_calls](const QString &from, const QString &to) {
                    ++rename_calls;
                    // 第 1 次重命名：将 target_dir 备份为 .backup_xxx，允许成功
                    if (rename_calls == 1)
                    {
                        return QDir().rename(from, to);
                    }
                    // 第 2 次重命名：将 staging_dir 发布为 target_dir，故障注入：模拟发布失败
                    if (rename_calls == 2)
                    {
                        return false;
                    }
                    // 第 3 次重命名：回滚操作，将 .backup_xxx 还原为 target_dir，允许成功
                    return QDir().rename(from, to);
                });
            QVERIFY(unc_scope.isValid());

            // 在 staging 暂存目录写入新文件，验证失败后绝不泄露到 target_dir
            QFile canary(QDir(unc_scope.stagingDir()).filePath(staging_canary));
            QVERIFY(canary.open(QIODevice::WriteOnly | QIODevice::Text));
            canary.write("this_staging_file_must_not_leak_into_target");
            canary.close();

            // 执行 publish：预期在第 2 次重命名时注入失败，并自动执行第 3 次重命名回滚
            const bool publish_ok = unc_scope.publish(publish_err);
            QVERIFY(!publish_ok);
            QVERIFY2(publish_err.contains(QStringLiteral("已恢复原目标内容")), qPrintable(publish_err));
            QCOMPARE(rename_calls, 3); // 严格证明真实经历了: target->backup, staging->target(失败), backup->target(回滚)
        }

        // 阶段四：故障恢复后重新回读验证 UNC 目标下所有图片和标注，证明零数据损坏
        QVERIFY2(!QFile::exists(QDir(unc_output_dir).filePath(staging_canary)),
                 "staging 暂存文件不应出现在恢复后的目标目录中");

        // 重新调用生产级导出校验器验证恢复后的 UNC 目标目录
        error.clear();
        QVERIFY2(dltool::data::DataIO::validateExportOutput(dltool::data::DataFormat::LabelMe, dataset2,
                                                            unc_output_dir, {}, error),
                 qPrintable(error));

        // 重新回读并完整解码 UNC 目录中的图像与标注 JSON，证明零数据损坏
        QVERIFY(QFile::exists(exported_image_path2));
        QImage post_recovery_image;
        QVERIFY(post_recovery_image.load(exported_image_path2));
        QCOMPARE(post_recovery_image.size(), QSize(16, 16));

        QVERIFY(QFile::exists(exported_json_path2));
        QFile post_recovery_json(exported_json_path2);
        QVERIFY(post_recovery_json.open(QIODevice::ReadOnly));
        const QJsonDocument post_recovery_doc = QJsonDocument::fromJson(post_recovery_json.readAll());
        QVERIFY(!post_recovery_doc.isNull());
        QCOMPARE(post_recovery_doc.object().value(QStringLiteral("imageWidth")).toInt(), 16);
        QCOMPARE(post_recovery_doc.object().value(QStringLiteral("imageHeight")).toInt(), 16);
        post_recovery_json.close();

        // 验证 UNC 目标父目录下没有任何残留的 .backup_* 或 .staging_* 目录
        const QString unc_parent = QFileInfo(unc_output_dir).path();
        const QStringList leftover_dirs = QDir(unc_parent).entryList(
            {QStringLiteral(".backup_*"), QStringLiteral(".staging_*")},
            QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
        QVERIFY2(leftover_dirs.isEmpty(), qPrintable(QString("存在残留备份或暂存目录: %1").arg(leftover_dirs.join(QStringLiteral(", ")))));

        // 清理 UNC 临时测试目录
        QDir(unc_output_dir).removeRecursively();
    }

private:
    dltool::data::LabelMeIO exporter_;
};

QTEST_GUILESS_MAIN(DataIOExportTest)

#include "test_DataIOExport.moc"
