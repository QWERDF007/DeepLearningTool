#include "data/DataIO.h"
#include "data/DataFormat.h"

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

private:
    dltool::data::LabelMeIO exporter_;
};

QTEST_GUILESS_MAIN(DataIOExportTest)

#include "test_DataIOExport.moc"
