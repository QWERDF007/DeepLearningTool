#include "../test_runner.h"

#include "model/ModelTaskPreparation.h"
#include "model/ModelTaskTypes.h"
#include "model/ModelStorageService.h"
#include "database/ModelTaskDataBase.h"
#include "data/DatasetExportSource.h"
#include "settings/GlobalSettings.h"
#include "settings/SettingsKeys.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTest>
#include <QTemporaryDir>

#include <map>

using namespace dltool::model;

namespace {

class InMemoryExportSource final : public dltool::data::DatasetExportSource
{
public:
    explicit InMemoryExportSource(QString image_path)
        : image_path_(std::move(image_path))
    {
    }

    std::vector<int64_t> allImageIds() const override
    {
        return {1};
    }

    qint64 imageDatasetId(qint64 image_id) const override
    {
        return image_id == 1 ? 7 : -1;
    }

    QString imagePath(qint64 image_id) const override
    {
        return image_id == 1 ? image_path_ : QString();
    }

    QVariantMap imageLevelLabelData(qint64) const override
    {
        return {};
    }

    std::vector<int64_t> imageLabelIds(qint64 image_id) const override
    {
        return image_id == 1 ? std::vector<int64_t>{101} : std::vector<int64_t>{};
    }

    qint64 labelClassId(qint64 label_id) const override
    {
        return label_id == 101 ? 10 : -1;
    }

    QVariantMap labelData(qint64 label_id) const override
    {
        if (label_id != 101)
            return {};
        return {{QStringLiteral("x"), 4.0},
                {QStringLiteral("y"), 5.0},
                {QStringLiteral("width"), 12.0},
                {QStringLiteral("height"), 10.0}};
    }

    QString labelClassName(qint64 label_class_id) const override
    {
        return label_class_id == 10 ? QStringLiteral("Object") : QString();
    }

    QString labelClassColor(qint64 label_class_id) const override
    {
        return label_class_id == 10 ? QStringLiteral("#3366cc") : QString();
    }

    QString labelClassGroup(qint64 label_class_id) const override
    {
        return label_class_id == 10 ? QStringLiteral("normal") : QString();
    }

    QString datasetName(qint64 dataset_id) const override
    {
        return dataset_id == 7 ? QStringLiteral("Export dataset") : QString();
    }

private:
    QString image_path_;
};

bool hasArgumentPair(const QStringList &arguments, const QString &key, const QString &value)
{
    for (int index = 0; index + 1 < arguments.size(); ++index)
        if (arguments.at(index) == key && arguments.at(index + 1) == value)
            return true;
    return false;
}

QString writeScript(const QString &directory, const QString &name)
{
    const QString path = QDir(directory).filePath(name);
    QFile       file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return {};
    file.write("# test helper\n");
    file.close();
    return path;
}

FrameworkDefinition frameworkFor(const QString &name, const QString &root, const QString &train_script,
                                 const QString &test_script, const QString &box_script, bool few_shot = false)
{
    FrameworkDefinition framework;
    framework.name           = name;
    framework.root           = root;
    framework.train_script   = train_script;
    framework.predict_script = test_script;
    framework.task_capabilities = {{ModelTaskType::BoxToMask, box_script}};
    framework.python_paths      = {root};
    framework.few_shot           = few_shot;
    framework.default_test_task_directory = few_shot ? QStringLiteral("fs_sam2") : QString();
    return framework;
}

ModelTaskRequest baseRequest(const FrameworkDefinition &framework, const QString &model_name)
{
    ModelTaskRequest request;
    request.task_id             = 17;
    request.task_type           = ModelTaskType::Train;
    request.scope_uuid          = QStringLiteral("scope-uuid");
    request.scope_name          = QStringLiteral("Scope");
    request.evaluation_method   = evaluation::Method::Detection;
    request.framework           = framework;
    request.task_server_host    = QStringLiteral("127.0.0.1");
    request.task_server_port    = 48123;
    request.project_database_path = QStringLiteral("F:/project/project.db");
    request.model_config.model_uuid       = QStringLiteral("model-uuid");
    request.model_config.model_name       = model_name;
    request.model_config.framework_name   = framework.name;
    request.model_config.model_architecture = QStringLiteral("YOLOv8");
    request.model_config.train_params     = {{QStringLiteral("train"), QVariantMap{{QStringLiteral("epochs"), 2}}}};
    request.model_config.test_params      = {{QStringLiteral("inference"),
                                               QVariantMap{{QStringLiteral("threshold"), 0.5}}}};
    request.selections.train.dataset_ids.insert(7);
    request.selections.test.dataset_ids.insert(7);
    return request;
}

} // namespace

class ModelTaskPreparationTest : public QObject
{
    Q_OBJECT

private slots:
    void taskDescriptorsExposeCapabilities()
    {
        const ModelTaskDescriptor train = describeModelTask(ModelTaskType::Train);
        QVERIFY(train.requires_dataset_export);
        QCOMPARE(train.key, QStringLiteral("train"));
        QVERIFY(isKnownModelTask(ModelTaskType::BoxToMask));
        QVERIFY(isTrainModelTask(ModelTaskType::Train));
        QVERIFY(isTestModelTask(ModelTaskType::Test));
        QVERIFY(!isKnownModelTask(ModelTaskType::Unknown));
        QCOMPARE(modelTaskLogStem(ModelTaskType::BoxToMask), QStringLiteral("box_to_mask"));
    }

    void rejectsInvalidPreparationRequestsBeforeFilesystemWork()
    {
        ModelTaskRequest request;
        ExternalProcessSpec spec;
        QString error;
        QVERIFY(!prepareModelTask(1, QStringLiteral("F:/tmp/model-task"), request, nullptr, spec, &error));
        QVERIFY(error.contains(QStringLiteral("任务")));

        request.task_id = 1;
        error.clear();
        QVERIFY(!prepareModelTask(1, QStringLiteral("F:/tmp/model-task"), request, nullptr, spec, &error));
        QVERIFY(error.contains(QStringLiteral("类型")));

        request.task_type = ModelTaskType::Test;
        error.clear();
        QVERIFY(!prepareModelTask(1, QStringLiteral("F:/tmp/model-task"), request, nullptr, spec, &error));
        QVERIFY(error.contains(QStringLiteral("端点")));

        request.task_server_host = QStringLiteral("127.0.0.1");
        request.task_server_port = 1;
        error.clear();
        QVERIFY(!prepareModelTask(1, QStringLiteral("F:/tmp/model-task"), request, nullptr, spec, &error));
        QVERIFY(error.contains(QStringLiteral("uuid")));
    }

    void preparesRegularAndFewShotTasksWithDatasetAndProcessContracts()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString image_path = QDir(temp.path()).filePath(QStringLiteral("source.png"));
        QImage image(32, 24, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        QVERIFY(image.save(image_path, "PNG"));
        InMemoryExportSource source(image_path);

        const QString train_script = writeScript(temp.path(), QStringLiteral("train.py"));
        const QString test_script  = writeScript(temp.path(), QStringLiteral("test.py"));
        const QString box_script   = writeScript(temp.path(), QStringLiteral("box_to_mask.py"));
        QVERIFY(!train_script.isEmpty());
        QVERIFY(!test_script.isEmpty());
        QVERIFY(!box_script.isEmpty());
        const QString python_env = QDir(temp.path()).filePath(QStringLiteral("python-env"));
        QVERIFY(QDir().mkpath(python_env));
        QFile python(QDir(python_env).filePath(QStringLiteral("python.exe")));
        QVERIFY(python.open(QIODevice::WriteOnly));
        python.close();

        auto *settings = dltool::settings::GlobalSettings::getInstance();
        QVERIFY(settings != nullptr);
        const bool old_auto_save = settings->autoSaveEnabled();
        const QVariant old_python
            = settings->valueForField(dltool::settings::generated::field::Software::PythonEnvPath);
        settings->setAutoSaveEnabled(false);
        QVERIFY(settings->setFieldValue(dltool::settings::generated::field::Software::PythonEnvPath, python_env));

        const FrameworkDefinition regular
            = frameworkFor(QStringLiteral("ultralytics"), temp.path(), train_script, test_script, box_script);
        ModelTaskRequest request = baseRequest(regular, QStringLiteral("regular"));
        request.task_type = ModelTaskType::Train;
        ExternalProcessSpec spec;
        QString              error;
        QVERIFY2(prepareModelTask(static_cast<int>(evaluation::Method::Detection), temp.path(), request, &source, spec,
                                   &error),
                 qPrintable(error));
        QCOMPARE(spec.task_id, 17);
        QCOMPARE(spec.program, QDir(python_env).filePath(QStringLiteral("python.exe")));
        QCOMPARE(spec.working_directory, temp.path());
        QVERIFY(spec.arguments.contains(train_script));
        QVERIFY(hasArgumentPair(spec.arguments, QStringLiteral("--model_db"),
                                ModelStorageService(temp.path()).modelDatabasePath(QStringLiteral("regular"))));
        QVERIFY(!spec.log_path.isEmpty());
        QVERIFY(QDir(QFileInfo(spec.log_path).absolutePath()).exists());

        request.task_type                    = ModelTaskType::Test;
        request.scope_uuid                   = QStringLiteral("test-scope");
        request.model_config.task_directory  = QStringLiteral("test-1");
        request.selections.train             = {};
        request.selections.test.dataset_ids.insert(7);
        ModelStorageService storage(temp.path());
        QVERIFY(storage.ensureTestTaskStorage(QStringLiteral("regular"), QStringLiteral("test-1"), &error));
        QFile stale(QDir(storage.testTaskPredictionPath(QStringLiteral("regular"), QStringLiteral("test-1")))
                          .filePath(QStringLiteral("stale.json")));
        QVERIFY(stale.open(QIODevice::WriteOnly));
        stale.write("stale");
        stale.close();
        spec = {};
        QVERIFY2(prepareModelTask(static_cast<int>(evaluation::Method::Detection), temp.path(), request, &source, spec,
                                   &error),
                 qPrintable(error));
        QVERIFY(spec.arguments.contains(test_script));
        QVERIFY(QFileInfo::exists(storage.testTaskFileListPath(QStringLiteral("regular"), QStringLiteral("test-1"))));
        QVERIFY(!QFileInfo::exists(stale.fileName()));
        QCOMPARE(QDir(storage.testTaskPredictionPath(QStringLiteral("regular"), QStringLiteral("test-1")))
                     .entryList(QDir::Files | QDir::NoDotAndDotDot),
                 QStringList{});
        QVERIFY(hasArgumentPair(spec.arguments, QStringLiteral("--task_db"),
                                storage.testTaskDatabasePath(QStringLiteral("regular"), QStringLiteral("test-1"))));

        request.task_type                   = ModelTaskType::BoxToMask;
        request.scope_uuid                  = QStringLiteral("box-scope");
        request.model_config.task_directory = QStringLiteral("box-1");
        request.selections                  = {};
        request.selections.train.dataset_ids.insert(7);
        spec = {};
        QVERIFY2(prepareModelTask(static_cast<int>(evaluation::Method::Detection), temp.path(), request, &source, spec,
                                   &error),
                 qPrintable(error));
        QVERIFY(spec.arguments.contains(box_script));
        QVERIFY(hasArgumentPair(spec.arguments, QStringLiteral("--dltool_task_id"), QStringLiteral("17")));

        const FrameworkDefinition few_shot
            = frameworkFor(QStringLiteral("fs-sam2"), temp.path(), train_script, test_script, box_script, true);
        ModelTaskRequest few_request = baseRequest(few_shot, QStringLiteral("few-shot"));
        few_request.task_type                    = ModelTaskType::Test;
        few_request.scope_uuid                   = QStringLiteral("few-scope");
        few_request.model_config.task_directory  = {};
        few_request.selections.train.dataset_ids.insert(7);
        few_request.selections.test.dataset_ids.insert(7);
        spec = {};
        QVERIFY2(prepareModelTask(static_cast<int>(evaluation::Method::Detection), temp.path(), few_request, &source,
                                   spec, &error),
                 qPrintable(error));
        QCOMPARE(spec.arguments.contains(test_script), true);
        QVERIFY(spec.arguments.contains(QStringLiteral("--test_file_list")));
        QVERIFY(QFileInfo::exists(storage.testTaskFileListPath(QStringLiteral("few-shot"), QStringLiteral("fs_sam2"))));

        settings->setFieldValue(dltool::settings::generated::field::Software::PythonEnvPath, old_python);
        settings->setAutoSaveEnabled(old_auto_save);
    }
};

REGISTER_TEST(ModelTaskPreparationTest)

#include "test_ModelTaskPreparation.moc"
