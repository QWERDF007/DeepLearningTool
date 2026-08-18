#include "../test_runner.h"

#include "data/DatasetExportSource.h"
#include "core/CoreDef.h"
#include "model/ModelDatasetOrganizer.h"
#include "model/ModelTaskTypes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <map>
#include <vector>

using namespace dltool::model;

namespace {

struct ExportImage
{
    qint64      id{0};
    qint64      dataset_id{0};
    QString     path;
    QVariantMap image_level;
    std::vector<qint64> labels;
};

struct ExportLabel
{
    qint64      id{0};
    qint64      class_id{0};
    QVariantMap data;
};

class InMemoryExportSource final : public dltool::data::DatasetExportSource
{
public:
    qint64 addImage(const QString &root, qint64 id, qint64 dataset_id, const QVariantMap &image_level = {})
    {
        const QString path = QDir(root).filePath(QStringLiteral("source-%1.png").arg(id));
        QImage image(32, 24, QImage::Format_RGBA8888);
        image.fill(QColor(60 + static_cast<int>(id), 90, 140));
        if (!image.save(path, "PNG"))
            return -1;
        images_.push_back({id, dataset_id, path, image_level, {}});
        return id;
    }

    void addLabel(qint64 image_id, qint64 label_id, qint64 class_id, const QVariantMap &data)
    {
        labels_.insert(label_id, {label_id, class_id, data});
        auto image = std::find_if(images_.begin(), images_.end(), [image_id](const ExportImage &value)
                                  { return value.id == image_id; });
        if (image != images_.end())
            image->labels.push_back(label_id);
    }

    void addClass(qint64 class_id, const QString &name, const QString &group)
    {
        classes_.insert(class_id, {name, group});
    }

    std::vector<int64_t> allImageIds() const override
    {
        std::vector<int64_t> result;
        for (const ExportImage &image : images_)
            result.push_back(image.id);
        std::sort(result.begin(), result.end());
        return result;
    }

    qint64 imageDatasetId(qint64 image_id) const override { return image(image_id).dataset_id; }
    QString imagePath(qint64 image_id) const override { return image(image_id).path; }
    QVariantMap imageLevelLabelData(qint64 image_id) const override { return image(image_id).image_level; }

    std::vector<int64_t> imageLabelIds(qint64 image_id) const override
    {
        return image(image_id).labels;
    }

    qint64 labelClassId(qint64 label_id) const override { return labels_.value(label_id).class_id; }
    QVariantMap labelData(qint64 label_id) const override { return labels_.value(label_id).data; }
    QString labelClassName(qint64 label_class_id) const override { return classes_.value(label_class_id).first; }
    QString labelClassColor(qint64) const override { return QStringLiteral("#3366cc"); }
    QString labelClassGroup(qint64 label_class_id) const override { return classes_.value(label_class_id).second; }
    QString datasetName(qint64 dataset_id) const override { return QStringLiteral("dataset-%1").arg(dataset_id); }

private:
    const ExportImage &image(qint64 image_id) const
    {
        const auto found = std::find_if(images_.cbegin(), images_.cend(), [image_id](const ExportImage &value)
                                        { return value.id == image_id; });
        Q_ASSERT(found != images_.cend());
        return *found;
    }

    std::vector<ExportImage> images_;
    QMap<qint64, ExportLabel> labels_;
    QMap<qint64, QPair<QString, QString>> classes_;
};

QVariantMap box(double x, double y, double width, double height)
{
    return {{QStringLiteral("x"), x},
            {QStringLiteral("y"), y},
            {QStringLiteral("width"), width},
            {QStringLiteral("height"), height}};
}

ModelDatasetExportRequest baseRequest(const QString &root, const InMemoryExportSource &source, const QString &framework,
                                      int method, ModelTaskType task_type)
{
    ModelDatasetExportRequest request;
    request.method             = method;
    request.framework_name     = framework;
    request.model_architecture = QStringLiteral("TestArchitecture");
    request.model_uuid         = QStringLiteral("model-uuid");
    request.task_type          = task_type;
    request.dataset_dir        = QDir(root).filePath(QStringLiteral("datasets"));
    request.train_dir          = QDir(root).filePath(QStringLiteral("train"));
    request.test_file_list_path = QDir(root).filePath(QStringLiteral("test-images.txt"));
    request.source             = &source;
    request.selections.train.dataset_ids.insert(1);
    request.selections.validation.dataset_ids.insert(1);
    request.selections.test.dataset_ids.insert(1);
    return request;
}

void populateSource(InMemoryExportSource &source, const QString &root)
{
    source.addClass(10, QStringLiteral("Cat"), QStringLiteral("good"));
    source.addClass(20, QStringLiteral("Scratch"), QStringLiteral("anomaly"));
    source.addClass(30, QStringLiteral("Ignore"), QStringLiteral("unlabeled"));
    source.addImage(root, 101, 1);
    source.addImage(root, 102, 1, {{QStringLiteral("label_class_id"), 20},
                                   {QStringLiteral("label_class_name"), QStringLiteral("Scratch")},
                                   {QStringLiteral("group"), QStringLiteral("anomaly")} });
    source.addLabel(101, 1001, 20, box(4, 3, 10, 8));
    source.addLabel(102, 1002, 10, box(8, 4, 12, 9));
}

} // namespace

class ModelDatasetOrganizerTest : public QObject
{
    Q_OBJECT

private slots:
    void rejectsMissingSourceAndSelection()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        ModelDatasetExportRequest request;
        request.dataset_dir = QDir(temp.path()).filePath(QStringLiteral("datasets"));
        request.task_type = ModelTaskType::Train;
        QString error;
        QVERIFY(ModelDatasetOrganizer::organize(request, &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("数据源")));

        InMemoryExportSource source;
        request.source = &source;
        error.clear();
        QVERIFY(ModelDatasetOrganizer::organize(request, &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("选择")));
    }

    void reportsNoAvailableImagesForSelectedDataset()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        InMemoryExportSource source;
        populateSource(source, temp.path());
        auto request = baseRequest(temp.path(), source, QStringLiteral("custom-framework"),
                                   dltool::core::DeepLearningMethod::Detection, ModelTaskType::Train);
        request.selections.train.dataset_ids.clear();
        request.selections.train.dataset_ids.insert(999);

        QString error;
        QVERIFY(ModelDatasetOrganizer::organize(request, &error).isEmpty());
        QVERIFY2(error.contains(QStringLiteral("没有可用图像")), qPrintable(error));
    }

    void skipsMissingSourceImagesWithoutWritingInvalidPaths()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        InMemoryExportSource source;
        populateSource(source, temp.path());
        QVERIFY(QFile::remove(QDir(temp.path()).filePath(QStringLiteral("source-101.png"))));

        auto request = baseRequest(temp.path(), source, QStringLiteral("custom-framework"),
                                   dltool::core::DeepLearningMethod::Detection, ModelTaskType::Train);
        QString error;
        const QVariantMap result = ModelDatasetOrganizer::organize(request, &error);
        QVERIFY2(!result.isEmpty(), qPrintable(error));
        const QVariantMap train = result.value(QStringLiteral("train")).toMap();
        QCOMPARE(train.value(QStringLiteral("image_count")).toInt(), 1);

        QFile file(train.value(QStringLiteral("file_list")).toString());
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString text = QString::fromUtf8(file.readAll());
        QVERIFY(!text.contains(QStringLiteral("101,")));
        QVERIFY(text.contains(QStringLiteral("102,")));
    }

    void genericLayoutWritesStableJsonAndFileList()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        InMemoryExportSource source;
        populateSource(source, temp.path());
        auto request = baseRequest(temp.path(), source, QStringLiteral("custom-framework"),
                                   dltool::core::DeepLearningMethod::Detection, ModelTaskType::Train);
        QString error;
        const QVariantMap result = ModelDatasetOrganizer::organize(request, &error);
        QVERIFY2(!result.isEmpty(), qPrintable(error));
        const QString file_list = result.value(QStringLiteral("train")).toMap().value(QStringLiteral("file_list")).toString();
        QVERIFY(QFileInfo::exists(file_list));
        QFile file(file_list);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString text = QString::fromUtf8(file.readAll());
        QVERIFY(text.contains(QStringLiteral("101,")));
        QVERIFY(text.contains(QStringLiteral("102,")));
        const QString labels_path = QDir(request.dataset_dir).filePath(QStringLiteral("train_labels.json"));
        QVERIFY(QFileInfo::exists(labels_path));
        QFile labels(labels_path);
        QVERIFY(labels.open(QIODevice::ReadOnly));
        const QByteArray json = labels.readAll();
        QVERIFY(json.contains("\"id\":101"));
        QVERIFY(json.contains("\"label_class_id\":20"));
    }

    void anomalyAndDinomalyLayoutsWriteMasksAndValues()
    {
        for (const QString &framework : {QStringLiteral("anomalib"), QStringLiteral("dinomaly2")})
        {
            QTemporaryDir temp;
            QVERIFY(temp.isValid());
            InMemoryExportSource source;
            populateSource(source, temp.path());
            auto request = baseRequest(temp.path(), source, framework,
                                       dltool::core::DeepLearningMethod::Segmentation, ModelTaskType::Train);
            QString error;
            const QVariantMap result = ModelDatasetOrganizer::organize(request, &error);
            QVERIFY2(!result.isEmpty(), qPrintable(error));
            const QString mask_path = QDir(request.dataset_dir).filePath(QStringLiteral("101.png"));
            QVERIFY(QFileInfo::exists(mask_path));
            QImage mask(mask_path);
            QVERIFY(!mask.isNull());
            QVERIFY(mask.pixelColor(6, 5).value() > 0);
            if (framework == QStringLiteral("dinomaly2"))
            {
                QCOMPARE(mask.pixelColor(6, 5).value(), 20);
                QCOMPARE(result.value(QStringLiteral("train")).toMap().value(QStringLiteral("anomaly_values")).toString(),
                         QStringLiteral("20"));
            }
        }
    }

    void ultralyticsMapsNonContiguousClassesAndPrefixesLabels()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        InMemoryExportSource source;
        populateSource(source, temp.path());
        auto request = baseRequest(temp.path(), source, QStringLiteral("ultralytics"),
                                   dltool::core::DeepLearningMethod::Detection, ModelTaskType::Train);
        QString error;
        const QVariantMap result = ModelDatasetOrganizer::organize(request, &error);
        QVERIFY2(!result.isEmpty(), qPrintable(error));
        QVERIFY(QFileInfo::exists(QDir(request.dataset_dir).filePath(QStringLiteral("101.png"))));
        QFile labels(QDir(request.dataset_dir).filePath(QStringLiteral("101.txt")));
        QVERIFY(labels.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString label_text = QString::fromUtf8(labels.readAll());
        QVERIFY(label_text.startsWith(QStringLiteral("101 1 ")));
        QFile yaml(QDir(request.dataset_dir).filePath(QStringLiteral("dataset.yaml")));
        QVERIFY(yaml.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString yaml_text = QString::fromUtf8(yaml.readAll());
        QVERIFY(yaml_text.contains(QStringLiteral("0: 'Cat'")));
        QVERIFY(yaml_text.contains(QStringLiteral("1: 'Scratch'")));
        QVERIFY(yaml_text.contains(QStringLiteral("0: 10")));
        QVERIFY(yaml_text.contains(QStringLiteral("1: 20")));
    }

    void fsSam2TestExportsTrainAndTestPrompts()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        InMemoryExportSource source;
        populateSource(source, temp.path());
        auto request = baseRequest(temp.path(), source, QStringLiteral("fs-sam2"),
                                   dltool::core::DeepLearningMethod::Segmentation, ModelTaskType::Test);
        request.few_shot = true;
        QString error;
        const QVariantMap result = ModelDatasetOrganizer::organize(request, &error);
        QVERIFY2(!result.isEmpty(), qPrintable(error));
        QVERIFY(result.contains(QStringLiteral("train")));
        QVERIFY(result.contains(QStringLiteral("test")));
        QVERIFY(QFileInfo::exists(QDir(request.dataset_dir).filePath(QStringLiteral("image_101_label_1001.png"))));
        QVERIFY(QFileInfo::exists(request.test_file_list_path));
        QFile file(request.test_file_list_path);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        QVERIFY(QString::fromUtf8(file.readAll()).contains(QStringLiteral("101,")));
    }

    void repeatedUltralyticsExportIsIdempotent()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        InMemoryExportSource source;
        populateSource(source, temp.path());
        auto request = baseRequest(temp.path(), source, QStringLiteral("ultralytics"),
                                   dltool::core::DeepLearningMethod::Detection, ModelTaskType::Train);

        QString error;
        const QVariantMap first = ModelDatasetOrganizer::organize(request, &error);
        QVERIFY2(!first.isEmpty(), qPrintable(error));
        const QString label_path = QDir(request.dataset_dir).filePath(QStringLiteral("101.txt"));
        const QString file_list_path
            = first.value(QStringLiteral("train")).toMap().value(QStringLiteral("file_list")).toString();
        QFile labels(label_path);
        QFile file_list(file_list_path);
        QVERIFY(labels.open(QIODevice::ReadOnly | QIODevice::Text));
        QVERIFY(file_list.open(QIODevice::ReadOnly | QIODevice::Text));
        const QByteArray first_labels    = labels.readAll();
        const QByteArray first_file_list = file_list.readAll();
        labels.close();
        file_list.close();

        error.clear();
        const QVariantMap second = ModelDatasetOrganizer::organize(request, &error);
        QVERIFY2(!second.isEmpty(), qPrintable(error));
        QCOMPARE(second, first);

        QVERIFY(labels.open(QIODevice::ReadOnly | QIODevice::Text));
        QVERIFY(file_list.open(QIODevice::ReadOnly | QIODevice::Text));
        QCOMPARE(labels.readAll(), first_labels);
        QCOMPARE(file_list.readAll(), first_file_list);
    }
};

REGISTER_TEST(ModelDatasetOrganizerTest)

#include "test_ModelDatasetOrganizer.moc"
