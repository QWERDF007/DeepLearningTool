#include "data/DataIO.h"
#include "data/DataFormat.h"
#include "data/DatasetIO.h"

#include "ui/ProgressManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QImage>
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
    }

private:
    dltool::data::LabelMeIO exporter_;
};

QTEST_GUILESS_MAIN(DataIOExportTest)

#include "test_DataIOExport.moc"
