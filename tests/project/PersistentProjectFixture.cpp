#include "PersistentProjectFixture.h"

#include "data/DataFormat.h"
#include "data/DataManager.h"
#include "database/DataBase.h"
#include "database/ModelDataBase.h"
#include "model/IModel.h"
#include "model/IModelConfig.h"
#include "model/IParams.h"
#include "model/ModelStorageService.h"
#include "model/ModelTestTaskRepository.h"
#include "project/Projects.h"
#include "settings/GlobalSettings.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QThread>
#include <QSet>

#include <algorithm>

namespace dltool::model::integration {

namespace {

QString cleanAbsolutePath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString sourceRoot()
{
    const QString configured = qEnvironmentVariable("DLT_RUNTIME_ROOT").trimmed();
    if (!configured.isEmpty())
        return cleanAbsolutePath(configured);
#ifdef DLT_SOURCE_ROOT
    return cleanAbsolutePath(QStringLiteral(DLT_SOURCE_ROOT));
#else
    return cleanAbsolutePath(QDir::currentPath());
#endif
}

QString errorOrDefault(const QString &message, const QString &fallback)
{
    return message.isEmpty() ? fallback : message;
}

QString modelUuidByName(const model::ModelManager *manager, const QString &name)
{
    if (manager == nullptr)
        return {};
    for (int row = 0; row < manager->rowCount(); ++row)
    {
        const QVariantMap record = manager->modelAt(row);
        if (record.value(QStringLiteral("name")).toString() == name)
            return record.value(QStringLiteral("uuid")).toString();
    }
    return {};
}

bool hasFiles(const QString &root, const QStringList &filters, const int minimum)
{
    const QDir directory(root);
    if (!directory.exists())
        return false;
    return directory.entryList(filters, QDir::Files | QDir::Readable, QDir::Name).size() == minimum;
}

bool waitForExportOutput(const int format, const QString &output_dir, const int minimum_images, const int timeout_ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeout_ms)
    {
        const QString dataset_root = QDir(output_dir).filePath(PersistentProjectFixture::datasetName());
        const QString images_dir   = QDir(dataset_root).filePath(QStringLiteral("images"));
        bool          ready      = false;
        switch (format)
        {
        case data::DataFormat::Mask:
            ready = hasFiles(images_dir, {QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
                                          QStringLiteral("*.png"), QStringLiteral("*.bmp"), QStringLiteral("*.webp")},
                             minimum_images)
                 && hasFiles(QDir(dataset_root).filePath(QStringLiteral("masks")), {QStringLiteral("*.png")},
                             minimum_images);
            break;
        case data::DataFormat::LabelMe:
            ready = hasFiles(images_dir, {QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
                                          QStringLiteral("*.png"), QStringLiteral("*.bmp"), QStringLiteral("*.webp")},
                             minimum_images)
                 && hasFiles(QDir(dataset_root).filePath(QStringLiteral("annotations")), {QStringLiteral("*.json")},
                             minimum_images);
            break;
        case data::DataFormat::COCO:
        {
            const QString annotation_path
                = QDir(dataset_root).filePath(QStringLiteral("annotations/instances.json"));
            ready = hasFiles(images_dir, {QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
                                          QStringLiteral("*.png"), QStringLiteral("*.bmp"), QStringLiteral("*.webp")},
                             minimum_images)
                 && QFileInfo(annotation_path).isFile() && QFileInfo(annotation_path).size() > 0;
            break;
        }
        default:
            break;
        }
        if (ready)
            return true;

        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(25);
    }
    return false;
}

} // namespace

PythonEnvironmentScope::PythonEnvironmentScope(QString path)
    : settings_(settings::GlobalSettings::getInstance())
    , path_(path.trimmed().isEmpty() ? PersistentProjectFixture::pythonEnvironmentPath()
                                     : cleanAbsolutePath(path))
{
    if (settings_ == nullptr)
    {
        error_ = QStringLiteral("全局设置对象为空");
        return;
    }

    previous_auto_save_ = settings_->autoSaveEnabled();
    previous_path_      = settings_->valueForField(settings::generated::field::Software::PythonEnvPath);
    settings_->setAutoSaveEnabled(false);
    if (!settings_->setFieldValue(settings::generated::field::Software::PythonEnvPath, path_))
    {
        error_ = QStringLiteral("写入 Python 环境设置失败");
        return;
    }

    const QFileInfo environment(path_);
    if (!environment.isDir())
        error_ = QStringLiteral("Python 环境目录不存在: %1").arg(path_);
}

PythonEnvironmentScope::~PythonEnvironmentScope()
{
    if (settings_ == nullptr)
        return;
    settings_->setFieldValue(settings::generated::field::Software::PythonEnvPath, previous_path_);
    settings_->setAutoSaveEnabled(previous_auto_save_);
}

bool PythonEnvironmentScope::isValid() const
{
    return error_.isEmpty() && settings_ != nullptr;
}

QString PythonEnvironmentScope::error() const
{
    return error_;
}

QString PythonEnvironmentScope::path() const
{
    return path_;
}

QString PersistentProjectFixture::projectRoot()
{
    return cleanAbsolutePath(qEnvironmentVariable("DLT_TEST_PROJECT_ROOT", QStringLiteral("F:/tmp/pro")));
}

QString PersistentProjectFixture::projectName()
{
    return qEnvironmentVariable("DLT_TEST_PROJECT_NAME", QStringLiteral("测试项目"));
}

QString PersistentProjectFixture::projectDatabasePath()
{
    return QDir(projectRoot()).filePath(projectName() + QStringLiteral(".dlpro"));
}

QString PersistentProjectFixture::assetRoot()
{
    const QString configured = qEnvironmentVariable("DLT_TEST_ASSET_ROOT").trimmed();
    if (!configured.isEmpty())
        return cleanAbsolutePath(configured);
    return QDir(sourceRoot()).filePath(QStringLiteral("tests/assets/model"));
}

QString PersistentProjectFixture::imageRoot()
{
    return QDir(assetRoot()).filePath(QStringLiteral("images"));
}

QString PersistentProjectFixture::maskRoot()
{
    return QDir(assetRoot()).filePath(QStringLiteral("masks"));
}

QString PersistentProjectFixture::pythonEnvironmentPath()
{
    return cleanAbsolutePath(
        qEnvironmentVariable("DLT_TEST_PYTHON_ENV", QStringLiteral("D:/Software/anaconda3/envs/py312")));
}

QString PersistentProjectFixture::datasetName()
{
    return qEnvironmentVariable("DLT_TEST_DATASET_NAME", QStringLiteral("测试数据集"));
}

QString PersistentProjectFixture::labelMeRoundtripDatasetName()
{
    return QStringLiteral("model-labelme-roundtrip");
}

QString PersistentProjectFixture::cocoRoundtripDatasetName()
{
    return QStringLiteral("model-coco-roundtrip");
}

QString PersistentProjectFixture::maskRoundtripDatasetName()
{
    return QStringLiteral("model-mask-roundtrip");
}

QString PersistentProjectFixture::patchcoreModelName()
{
    return QStringLiteral("patchcore-model");
}

QString PersistentProjectFixture::patchcoreModelCopyName()
{
    return patchcoreModelName() + QStringLiteral(" Copy");
}

QString PersistentProjectFixture::patchcoreModelRenameName()
{
    return patchcoreModelName() + QStringLiteral("-renamed");
}

QString PersistentProjectFixture::patchcoreTestName()
{
    return QStringLiteral("patchcore-test");
}

QString PersistentProjectFixture::dataExportRoot()
{
    return QDir(projectRoot()).filePath(QStringLiteral("data_exports"));
}

QString PersistentProjectFixture::maskExportRoot()
{
    return QDir(dataExportRoot()).filePath(QStringLiteral("mask"));
}

QString PersistentProjectFixture::labelMeExportRoot()
{
    return QDir(dataExportRoot()).filePath(QStringLiteral("labelme"));
}

QString PersistentProjectFixture::cocoExportRoot()
{
    return QDir(dataExportRoot()).filePath(QStringLiteral("coco"));
}

PersistentProjectFixture::PersistentProjectFixture(const bool create_if_missing)
    : python_scope_()
{
    if (!python_scope_.isValid())
    {
        error_ = python_scope_.error();
        return;
    }

    if (!QDir().mkpath(projectRoot()))
    {
        error_ = QStringLiteral("无法创建项目目录: %1").arg(projectRoot());
        return;
    }

    const QString database_path = projectDatabasePath();
    if (!QFileInfo::exists(database_path))
    {
        if (!create_if_missing)
        {
            error_ = QStringLiteral("项目不存在，请先运行项目创建测试: %1").arg(database_path);
            return;
        }

        project_manager_ = project::ProjectManager::getInstance();
        if (project_manager_ == nullptr)
        {
            error_ = QStringLiteral("项目管理器初始化失败");
            return;
        }
        project_ = project_manager_->createProject(projectName(), kMethod, database_path,
                                                   QStringLiteral("模型流程集成测试项目"), projectRoot());
    }
    else
    {
        project_manager_ = project::ProjectManager::getInstance();
        if (project_manager_ == nullptr)
        {
            error_ = QStringLiteral("项目管理器初始化失败");
            return;
        }
        project_ = project_manager_->openProject(database_path);
    }

    if (project_ == nullptr || project_->dataManager() == nullptr || project_->modelManager() == nullptr
        || project_->modelTaskController() == nullptr)
    {
        error_ = QStringLiteral("项目对象初始化不完整: %1").arg(database_path);
        return;
    }

    QVariantMap project_info;
    QString     database_error;
    if (!database::ProjectDataBase::getProjectInfo(database_path, project_info, database_error))
        error_ = QStringLiteral("读取项目数据库失败: %1").arg(database_error);
    else if (project_info.value(QStringLiteral("method")).toInt() != kMethod)
        error_ = QStringLiteral("项目类型不是异常检测: %1").arg(project_info.value(QStringLiteral("method")).toInt());

    if (!error_.isEmpty())
        return;

    int expected_label_count = 0;
    {
        database::ProjectDataBase database(database_path);
        std::vector<int64_t>      label_ids;
        std::vector<int64_t>      image_ids;
        std::vector<int64_t>      label_class_ids;
        std::vector<int64_t>      label_types;
        std::vector<std::vector<uint8_t>> label_data;
        if (!database.getAllLabels(label_ids, image_ids, label_class_ids, label_types, label_data, database_error))
        {
            error_ = QStringLiteral("读取项目标注数量失败: %1").arg(database_error);
            return;
        }
        expected_label_count = static_cast<int>(label_ids.size());
    }

    QElapsedTimer labels_timer;
    labels_timer.start();
    while (project_->dataManager()->labelSource()->rowCount() < expected_label_count
           && labels_timer.elapsed() < 120000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(25);
    }
    if (project_->dataManager()->labelSource()->rowCount() < expected_label_count)
    {
        error_ = QStringLiteral("等待项目标注加载超时: 期望 %1, 实际 %2")
                     .arg(expected_label_count)
                     .arg(project_->dataManager()->labelSource()->rowCount());
    }
}

PersistentProjectFixture::~PersistentProjectFixture()
{
    if (project_manager_ != nullptr && project_manager_->currentProject() == project_)
        project_manager_->closeProject();
}

bool PersistentProjectFixture::isValid() const
{
    return error_.isEmpty() && project_ != nullptr;
}

QString PersistentProjectFixture::error() const
{
    return error_;
}

project::Project *PersistentProjectFixture::project() const
{
    return project_;
}

data::DataManager *PersistentProjectFixture::dataManager() const
{
    return project_ != nullptr ? project_->dataManager() : nullptr;
}

qint64 PersistentProjectFixture::ensureDataset(const QString &name, QString *error) const
{
    if (!isValid() || dataManager() == nullptr || dataManager()->datasets() == nullptr)
    {
        if (error != nullptr)
            *error = errorOrDefault(error_, QStringLiteral("项目未初始化"));
        return -1;
    }

    const int existing = dataManager()->getDatasetId(name);
    if (existing >= 0)
        return existing;

    dataManager()->addDataset(name);
    QElapsedTimer timer;
    timer.start();
    int created = dataManager()->getDatasetId(name);
    while (created < 0 && timer.elapsed() < 120000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        created = dataManager()->getDatasetId(name);
        if (created >= 0)
            break;
        QThread::msleep(25);
    }
    if (created < 0 && error != nullptr)
        *error = QStringLiteral("创建数据集超时或失败: %1").arg(name);
    return created;
}

bool PersistentProjectFixture::datasetCounts(const qint64 dataset_id, int *image_count, int *label_count,
                                             QString *error) const
{
    if (!isValid() || dataset_id < 0)
    {
        if (error != nullptr)
            *error = errorOrDefault(error_, QStringLiteral("项目或数据集无效"));
        return false;
    }

    database::ProjectDataBase database(projectDatabasePath());
    std::vector<int64_t>      image_ids;
    std::vector<QString>      image_paths;
    QString                   database_error;
    if (!database.getImages(dataset_id, image_ids, image_paths, database_error))
    {
        if (error != nullptr)
            *error = QStringLiteral("读取数据集图像失败: %1").arg(database_error);
        return false;
    }

    std::vector<int64_t>             label_ids;
    std::vector<int64_t>             label_image_ids;
    std::vector<int64_t>             label_class_ids;
    std::vector<int64_t>             label_types;
    std::vector<std::vector<uint8_t>> label_data;
    if (!database.getAllLabels(label_ids, label_image_ids, label_class_ids, label_types, label_data, database_error))
    {
        if (error != nullptr)
            *error = QStringLiteral("读取项目标注失败: %1").arg(database_error);
        return false;
    }

    QSet<qint64>               image_set;
    for (const qint64 image_id : image_ids)
        image_set.insert(image_id);

    int labels = 0;
    for (const qint64 label_image_id : label_image_ids)
        if (image_set.contains(label_image_id))
            ++labels;

    if (image_count != nullptr)
        *image_count = image_set.size();
    if (label_count != nullptr)
        *label_count = labels;
    return true;
}

bool PersistentProjectFixture::findPatchcoreModel(QString *model_uuid, QString *error) const
{
    if (!isValid() || project()->modelManager() == nullptr)
    {
        if (error != nullptr)
            *error = errorOrDefault(error_, QStringLiteral("模型管理器未初始化"));
        return false;
    }

    const QString uuid = modelUuidByName(project()->modelManager(), patchcoreModelName());
    if (uuid.isEmpty())
    {
        if (error != nullptr)
            *error = QStringLiteral("PatchCore 模型不存在，请先运行模型创建测试");
        return false;
    }
    if (model_uuid != nullptr)
        *model_uuid = uuid;
    return true;
}

bool PersistentProjectFixture::ensurePatchcoreModel(QString *model_uuid, QString *error) const
{
    if (!isValid() || project()->modelManager() == nullptr)
    {
        if (error != nullptr)
            *error = errorOrDefault(error_, QStringLiteral("模型管理器未初始化"));
        return false;
    }

    QString uuid = modelUuidByName(project()->modelManager(), patchcoreModelName());
    if (uuid.isEmpty())
    {
        QString error_message;
        const ModelManager::ModelRecordView record
            = project()->modelManager()->addModelRecord(patchcoreModelName(), QStringLiteral("anomalib"),
                                                        QStringLiteral("patchcore"), &error_message);
        if (!record.isValid())
        {
            if (error != nullptr)
                *error = errorOrDefault(error_message, QStringLiteral("创建 PatchCore 模型失败"));
            return false;
        }
        uuid = record.uuid;
    }

    if (model_uuid != nullptr)
        *model_uuid = uuid;
    return true;
}

bool PersistentProjectFixture::setPatchcoreParameter(IParams *params, const QString &group_name,
                                                     const QString &name, const QVariant &value, QString *error) const
{
    if (params == nullptr)
    {
        if (error != nullptr)
            *error = QStringLiteral("PatchCore 参数对象为空");
        return false;
    }

    for (ParamGroupModel *group : params->groups())
    {
        if (group == nullptr || group->nameEn() != group_name)
            continue;
        if (group->fieldMapForName(name).isEmpty())
        {
            if (error != nullptr)
                *error = QStringLiteral("PatchCore 参数不存在: %1.%2").arg(group_name, name);
            return false;
        }
        if (group->valueForName(name) != value && !group->setValueForName(name, value))
        {
            if (error != nullptr)
                *error = QStringLiteral("设置 PatchCore 参数失败: %1.%2").arg(group_name, name);
            return false;
        }
        return true;
    }

    if (error != nullptr)
        *error = QStringLiteral("PatchCore 参数组不存在: %1").arg(group_name);
    return false;
}

bool PersistentProjectFixture::savePatchcoreConfiguration(const QString &model_uuid, const qint64 dataset_id,
                                                          QString *task_uuid, QString *error) const
{
    if (!isValid() || project()->modelManager() == nullptr || dataset_id < 0)
    {
        if (error != nullptr)
            *error = errorOrDefault(error_, QStringLiteral("PatchCore 配置输入无效"));
        return false;
    }

    IModel *model = project()->modelManager()->modelForUuid(model_uuid);
    if (model == nullptr || model->config() == nullptr || model->config()->trainParams() == nullptr
        || model->config()->testParams() == nullptr)
    {
        if (error != nullptr)
            *error = QStringLiteral("无法创建 PatchCore 模型实例");
        return false;
    }

    QString parameter_error;
    const std::vector<std::tuple<QString, QString, QVariant>> train_parameters = {
        {QStringLiteral("network"), QStringLiteral("image_size"), 64},
        {QStringLiteral("network"), QStringLiteral("backbone"), QStringLiteral("resnet18")},
        {QStringLiteral("network"), QStringLiteral("pre_trained"), false},
        {QStringLiteral("network"), QStringLiteral("center_crop_size"), 0},
        {QStringLiteral("training"), QStringLiteral("max_epochs"), 1},
        {QStringLiteral("training"), QStringLiteral("batch_size"), 2},
        {QStringLiteral("training"), QStringLiteral("device"), QStringLiteral("cpu")},
        {QStringLiteral("training"), QStringLiteral("num_workers"), 0},
        {QStringLiteral("training"), QStringLiteral("coreset_sampling_ratio"), 1.0},
        {QStringLiteral("training"), QStringLiteral("num_neighbors"), 1},
    };
    for (const auto &[group, name, value] : train_parameters)
        if (!setPatchcoreParameter(model->config()->trainParams(), group, name, value, &parameter_error))
        {
            if (error != nullptr)
                *error = parameter_error;
            return false;
        }

    const std::vector<std::tuple<QString, QString, QVariant>> test_parameters = {
        {QStringLiteral("inference"), QStringLiteral("batch_size"), 2},
        {QStringLiteral("inference"), QStringLiteral("num_workers"), 0},
        {QStringLiteral("inference"), QStringLiteral("device"), QStringLiteral("cpu")},
        {QStringLiteral("evaluation"), QStringLiteral("classification_threshold"), 0.5},
    };
    for (const auto &[group, name, value] : test_parameters)
        if (!setPatchcoreParameter(model->config()->testParams(), group, name, value, &parameter_error))
        {
            if (error != nullptr)
                *error = parameter_error;
            return false;
        }

    std::vector<int64_t> class_ids;
    database::ProjectDataBase project_database(projectDatabasePath());
    QString                database_error;
    if (!project_database.labelClassIdsForDataset(dataset_id, class_ids, database_error))
    {
        if (error != nullptr)
            *error = QStringLiteral("读取数据集类别失败: %1").arg(database_error);
        return false;
    }

    QList<database::DatasetSelectionRecord> selections;
    const QList<qint64> class_list = [&class_ids]()
    {
        QList<qint64> result;
        for (const qint64 class_id : class_ids)
            result.push_back(class_id);
        return result;
    }();
    for (const QString &type : {QStringLiteral("train"), QStringLiteral("validation"), QStringLiteral("test")})
        selections.push_back({type, dataset_id, class_list});

    const ModelStorageService storage(projectRoot());
    database::ModelDataBase   model_database(storage.modelDatabasePath(patchcoreModelName()));
    if (!model_database.replaceTrainParams(model->config()->trainParams()->valuesMap(), &database_error)
        || !model_database.replaceDatasets(selections, &database_error))
    {
        if (error != nullptr)
            *error = QStringLiteral("写入 PatchCore 模型配置失败: %1").arg(database_error);
        return false;
    }

    ModelTestTaskRepository repository(projectRoot());
    repository.setProjectDatabasePath(projectDatabasePath());
    QList<ModelTestTaskDefinition> tasks = repository.listTasks(patchcoreModelName(), &database_error);
    if (!database_error.isEmpty())
    {
        if (error != nullptr)
            *error = QStringLiteral("读取 PatchCore 测试任务失败: %1").arg(database_error);
        return false;
    }

    const QVariantMap test_params = model->config()->testParams()->valuesMap();
    ModelDatasetSelection test_selection;
    test_selection.dataset_ids.insert(dataset_id);
    auto existing = std::find_if(tasks.cbegin(), tasks.cend(), [](const ModelTestTaskDefinition &task)
                                  { return task.name == PersistentProjectFixture::patchcoreTestName(); });
    ModelTestTaskDefinition task;
    if (existing == tasks.cend())
    {
        if (!repository.createTask(patchcoreModelName(), model_uuid, patchcoreTestName(), test_params, test_selection,
                                   task, &database_error))
        {
            if (error != nullptr)
                *error = QStringLiteral("创建 PatchCore 测试任务失败: %1").arg(database_error);
            return false;
        }
    }
    else
    {
        if (!repository.loadTask(patchcoreModelName(), existing->uuid, task, &database_error))
        {
            if (error != nullptr)
                *error = QStringLiteral("读取 PatchCore 测试任务详情失败: %1").arg(database_error);
            return false;
        }
        task.model_uuid        = model_uuid;
        task.test_params       = test_params;
        task.dataset_selection = test_selection;
        task.modified_at       = QDateTime::currentSecsSinceEpoch();
        if (!repository.saveTask(patchcoreModelName(), task, &database_error))
        {
            if (error != nullptr)
                *error = QStringLiteral("更新 PatchCore 测试任务失败: %1").arg(database_error);
            return false;
        }
    }

    if (task_uuid != nullptr)
        *task_uuid = task.uuid;
    return true;
}

bool PersistentProjectFixture::configurePatchcore(QString *model_uuid, const qint64 dataset_id, QString *task_uuid,
                                                  QString *error) const
{
    QString uuid;
    if (!ensurePatchcoreModel(&uuid, error))
        return false;
    if (!savePatchcoreConfiguration(uuid, dataset_id, task_uuid, error))
        return false;
    if (model_uuid != nullptr)
        *model_uuid = uuid;
    return true;
}

bool PersistentProjectFixture::findPatchcoreTask(const QString &model_uuid, QString *task_uuid, QString *error) const
{
    if (!isValid())
    {
        if (error != nullptr)
            *error = errorOrDefault(error_, QStringLiteral("项目未初始化"));
        return false;
    }

    const auto record = project()->modelManager()->modelRecordViewForUuid(model_uuid);
    if (!record.isValid())
    {
        if (error != nullptr)
            *error = QStringLiteral("PatchCore 模型 UUID 无效: %1").arg(model_uuid);
        return false;
    }
    ModelTestTaskRepository repository(projectRoot());
    repository.setProjectDatabasePath(projectDatabasePath());
    QString                          repository_error;
    const QList<ModelTestTaskDefinition> tasks = repository.listTasks(record.name, &repository_error);
    if (!repository_error.isEmpty())
    {
        if (error != nullptr)
            *error = QStringLiteral("读取 PatchCore 测试任务失败: %1").arg(repository_error);
        return false;
    }
    const auto found = std::find_if(tasks.cbegin(), tasks.cend(), [](const ModelTestTaskDefinition &task)
                                    { return task.name == PersistentProjectFixture::patchcoreTestName(); });
    if (found == tasks.cend())
    {
        if (error != nullptr)
            *error = QStringLiteral("PatchCore 测试任务不存在，请先运行模型创建测试");
        return false;
    }
    if (task_uuid != nullptr)
        *task_uuid = found->uuid;
    return true;
}

bool PersistentProjectFixture::importData(const qint64 dataset_id, const int format, const QString &image_dir,
                                          const QString &data_dir, const QVariantMap &label_class_groups,
                                          QString *error, const int timeout_ms) const
{
    if (!isValid() || dataManager() == nullptr)
    {
        if (error != nullptr)
            *error = errorOrDefault(error_, QStringLiteral("数据管理器未初始化"));
        return false;
    }

    bool    completed = false;
    bool    success   = false;
    QString message;
    QEventLoop loop;
    QTimer     timeout;
    timeout.setSingleShot(true);
    QObject::connect(dataManager(), &data::DataManager::dataImportFinished, &loop,
                     [&](const bool ok, const QString &text)
                     {
                         completed = true;
                         success   = ok;
                         message   = text;
                         loop.quit();
                     });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(timeout_ms);
    dataManager()->importDataWithLabelClassGroups(dataset_id, format, image_dir, data_dir, label_class_groups);
    loop.exec();

    if (!completed || !success)
    {
        if (error != nullptr)
            *error = !completed ? QStringLiteral("等待数据导入超时: %1").arg(image_dir)
                                : errorOrDefault(message, QStringLiteral("数据导入失败"));
        return false;
    }
    return true;
}

bool PersistentProjectFixture::exportData(const qint64 dataset_id, const int format, const QString &output_dir,
                                          const int minimum_images, QString *error, const int timeout_ms) const
{
    if (!isValid() || dataManager() == nullptr || dataset_id < 0)
    {
        if (error != nullptr)
            *error = errorOrDefault(error_, QStringLiteral("数据导出输入无效"));
        return false;
    }

    QDir output(output_dir);
    if (output.exists() && !output.removeRecursively())
    {
        if (error != nullptr)
            *error = QStringLiteral("清理旧导出目录失败: %1").arg(output_dir);
        return false;
    }
    if (!QDir().mkpath(output_dir))
    {
        if (error != nullptr)
            *error = QStringLiteral("创建导出目录失败: %1").arg(output_dir);
        return false;
    }

    dataManager()->exportDatasets({dataset_id}, format, output_dir);
    if (!waitForExportOutput(format, output_dir, minimum_images, timeout_ms))
    {
        if (error != nullptr)
            *error = QStringLiteral("等待数据导出完成超时或输出不完整: %1").arg(output_dir);
        return false;
    }

    // The exporter writes its files before the completion callback is delivered
    // back to DataManager.  Drain that queued callback before starting another
    // format in the same test process.
    QElapsedTimer settle_timer;
    settle_timer.start();
    while (settle_timer.elapsed() < 500)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(25);
    }
    return true;
}

bool PersistentProjectFixture::waitForTask(TaskManager *task_manager, const int task_id, QString *error,
                                           const int timeout_ms)
{
    if (task_manager == nullptr || task_id < 0)
    {
        if (error != nullptr)
            *error = QStringLiteral("任务管理器或任务 ID 无效");
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeout_ms)
    {
        const TaskManager::Task *task = task_manager->findTask(task_id);
        if (task != nullptr && TaskManager::isTerminal(task->status))
        {
            if (task->status == TaskManager::Finished)
                return true;
            if (error != nullptr)
                *error = QStringLiteral("任务未成功完成: id=%1, status=%2, log=%3")
                             .arg(task_id)
                             .arg(static_cast<int>(task->status))
                             .arg(task->log_path);
            return false;
        }

        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(50);
    }

    if (error != nullptr)
        *error = QStringLiteral("等待任务超时: %1").arg(task_id);
    return false;
}

} // namespace dltool::model::integration
