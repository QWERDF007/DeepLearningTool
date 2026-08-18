#include "TestFixture.h"

#include "data/LabelData.h"
#include "database/DataBase.h"
#include "database/ModelTaskDataBase.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <utility>

namespace dltool::model::testsupport {

namespace {

QString temporaryRoot()
{
    const QString root = qEnvironmentVariable("DLT_TEST_TMP_ROOT", QStringLiteral("F:/tmp"));
    QDir().mkpath(root);
    return QDir::cleanPath(root);
}

std::vector<uint8_t> jsonBytes(const QJsonObject &object)
{
    const QByteArray encoded = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return {encoded.cbegin(), encoded.cend()};
}

} // namespace

EvaluationFixture::EvaluationFixture(const int method)
    : method_(method)
{
    const QString template_path = QDir(temporaryRoot()).filePath(QStringLiteral("dltool-evaluation-XXXXXX"));
    temporary_dir_              = std::make_unique<QTemporaryDir>(template_path);
    if (!temporary_dir_->isValid())
    {
        setError(QStringLiteral("无法创建临时测试目录: %1").arg(template_path));
        return;
    }

    const QString root = temporary_dir_->path();
    project_database_path_ = QDir(root).filePath(QStringLiteral("project.db"));
    task_database_path_    = QDir(root).filePath(QStringLiteral("task.db"));
    file_list_path_        = QDir(root).filePath(QStringLiteral("test_images.csv"));
    prediction_directory_  = QDir(root).filePath(QStringLiteral("predictions"));
    if (!QDir().mkpath(prediction_directory_))
    {
        setError(QStringLiteral("无法创建预测目录: %1").arg(prediction_directory_));
        return;
    }

    database::ProjectDataBase database(projectDatabasePath());
    QString                  database_error;
    if (!database.initProject(QStringLiteral("evaluation-fixture"), method_, root, {}, root, 1, 1,
                              database_error))
    {
        setError(QStringLiteral("初始化项目数据库失败: %1").arg(database_error));
        return;
    }

    std::vector<int64_t> dataset_ids;
    if (!database.addDatasets({QStringLiteral("test-dataset")}, dataset_ids, database_error)
        || dataset_ids.size() != 1)
    {
        setError(QStringLiteral("初始化测试数据集失败: %1").arg(database_error));
        return;
    }
    dataset_id_ = dataset_ids.front();

    database::ModelTaskDataBase task_database(taskDatabasePath());
    if (!task_database.replaceDatasets({{QStringLiteral("test"), dataset_id_, {}}}, &database_error))
    {
        setError(QStringLiteral("初始化任务数据库失败: %1").arg(database_error));
    }
}

EvaluationFixture::~EvaluationFixture() = default;

bool EvaluationFixture::isValid() const
{
    return temporary_dir_ != nullptr && temporary_dir_->isValid() && error_.isEmpty() && dataset_id_ >= 0;
}

QString EvaluationFixture::error() const
{
    return error_;
}

QString EvaluationFixture::rootPath() const
{
    return temporary_dir_ != nullptr ? temporary_dir_->path() : QString();
}

QString EvaluationFixture::projectDatabasePath() const
{
    return project_database_path_;
}

QString EvaluationFixture::taskDatabasePath() const
{
    return task_database_path_;
}

QString EvaluationFixture::fileListPath() const
{
    return file_list_path_;
}

QString EvaluationFixture::predictionDirectory() const
{
    return prediction_directory_;
}

qint64 EvaluationFixture::datasetId() const
{
    return dataset_id_;
}

const QList<qint64> &EvaluationFixture::classIds() const
{
    return class_ids_;
}

const QList<qint64> &EvaluationFixture::imageIds() const
{
    return image_ids_;
}

qint64 EvaluationFixture::addClass(const QString &name, const QString &group)
{
    if (!isValid() && !error_.isEmpty())
        return -1;

    database::ProjectDataBase database(projectDatabasePath());
    QString                  database_error;
    qint64                   class_id = -1;
    if (!database.addLabelClass(name, QStringLiteral("#3366cc"), {}, class_ids_.size(),
                                jsonBytes(QJsonObject{{QStringLiteral("group"), group}}), class_id,
                                database_error))
    {
        setError(QStringLiteral("写入标签类别失败: %1").arg(database_error));
        return -1;
    }
    class_ids_.push_back(class_id);
    return class_id;
}

qint64 EvaluationFixture::addImage(const QString &name, const QVariantMap &image_level)
{
    if (!isValid() && !error_.isEmpty())
        return -1;

    const QString path = QDir(rootPath()).filePath(QStringLiteral("images/%1.png").arg(name));
    if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !writeImageFile(path, image_level.isEmpty()))
        return -1;

    database::ProjectDataBase database(projectDatabasePath());
    QString                  database_error;
    std::vector<int64_t>     ids;
    if (!database.addImages(dataset_id_ >= 0 ? dataset_id_ : 1, {path}, ids, database_error) || ids.size() != 1)
    {
        setError(QStringLiteral("写入图像失败: %1").arg(database_error));
        return -1;
    }
    const qint64 image_id = ids.front();
    if (!image_level.isEmpty())
    {
        const QJsonObject object = QJsonObject::fromVariantMap(image_level);
        if (!database.updateImagesExtraData({image_id}, {jsonBytes(object)}, database_error))
        {
            setError(QStringLiteral("写入图像级标签失败: %1").arg(database_error));
            return -1;
        }
    }
    image_ids_.push_back(image_id);
    image_paths_.push_back(path);
    return image_id;
}

qint64 EvaluationFixture::addDetectionLabel(const qint64 image_id, const qint64 class_id, const double x,
                                            const double y, const double width, const double height)
{
    data::DetLabelData_t label;
    label.x      = x;
    label.y      = y;
    label.width  = width;
    label.height = height;

    database::ProjectDataBase database(projectDatabasePath());
    QString                  database_error;
    std::vector<int64_t>     label_ids;
    if (!database.addLabels({image_id}, {class_id}, {method_}, {label.toBlob()}, label_ids, database_error)
        || label_ids.size() != 1)
    {
        setError(QStringLiteral("写入标注失败: %1").arg(database_error));
        return -1;
    }
    return label_ids.front();
}

qint64 EvaluationFixture::addSegmentationLabel(const qint64 image_id, const qint64 class_id,
                                               const std::vector<QPointF> &points)
{
    data::SegLabelData_t label;
    label.points = points;
    database::ProjectDataBase database(projectDatabasePath());
    QString                  database_error;
    std::vector<int64_t>     label_ids;
    if (!database.addLabels({image_id}, {class_id}, {method_}, {label.toBlob()}, label_ids, database_error)
        || label_ids.size() != 1)
    {
        setError(QStringLiteral("写入分割标注失败: %1").arg(database_error));
        return -1;
    }
    return label_ids.front();
}

qint64 EvaluationFixture::addAnomalyLabel(const qint64 image_id, const qint64 class_id,
                                          const std::vector<QPointF> &points)
{
    data::AnomalyLabelData_t label;
    label.points = points;
    database::ProjectDataBase database(projectDatabasePath());
    QString                  database_error;
    std::vector<int64_t>     label_ids;
    if (!database.addLabels({image_id}, {class_id}, {method_}, {label.toBlob()}, label_ids, database_error)
        || label_ids.size() != 1)
    {
        setError(QStringLiteral("写入异常标注失败: %1").arg(database_error));
        return -1;
    }
    return label_ids.front();
}

bool EvaluationFixture::writeImageList(const QList<QPair<qint64, QString>> &rows)
{
    const QList<QPair<qint64, QString>> values = rows.isEmpty()
        ? [&]()
        {
              QList<QPair<qint64, QString>> result;
              for (int index = 0; index < image_ids_.size(); ++index)
                  result.push_back({image_ids_.at(index), image_paths_.at(index)});
              return result;
          }()
        : rows;

    QFile file(fileListPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return setError(QStringLiteral("打开文件列表失败: %1").arg(file.errorString()));
    QTextStream stream(&file);
    stream << "image_id,image_path\n";
    for (const auto &row : values)
        stream << row.first << ',' << row.second << '\n';
    return true;
}

bool EvaluationFixture::setTestSelection(const QList<qint64> &class_ids)
{
    const QList<qint64> selected = class_ids.isEmpty() ? class_ids_ : class_ids;
    database::ModelTaskDataBase task_database(taskDatabasePath());
    QString                      database_error;
    if (!task_database.replaceDatasets({{QStringLiteral("test"), dataset_id_, selected}}, &database_error))
        return setError(QStringLiteral("写入测试选择失败: %1").arg(database_error));
    return true;
}

bool EvaluationFixture::writePrediction(const qint64 image_id, const QVariant &prediction)
{
    database::ModelTaskDataBase task_database(taskDatabasePath());
    QString                      database_error;
    if (!task_database.upsertPrediction({image_id, prediction}, &database_error))
        return setError(QStringLiteral("写入预测失败: %1").arg(database_error));
    return true;
}

bool EvaluationFixture::removePrediction(const qint64 image_id)
{
    database::ModelTaskDataBase task_database(taskDatabasePath());
    QHash<qint64, QVariant>      predictions;
    QString                      database_error;
    if (!task_database.readPredictions(predictions, &database_error))
        return setError(QStringLiteral("读取预测失败: %1").arg(database_error));
    predictions.remove(image_id);
    if (!task_database.clearPredictions(&database_error))
        return setError(QStringLiteral("清理预测失败: %1").arg(database_error));
    for (auto it = predictions.cbegin(); it != predictions.cend(); ++it)
        if (!task_database.upsertPrediction({it.key(), it.value()}, &database_error))
            return setError(QStringLiteral("恢复预测失败: %1").arg(database_error));
    return true;
}

bool EvaluationFixture::setError(const QString &message)
{
    error_ = message;
    return false;
}

bool EvaluationFixture::writeImageFile(const QString &path, const bool anomaly_variant)
{
    QImage image(32, 32, QImage::Format_RGBA8888);
    image.fill(anomaly_variant ? QColor(220, 220, 220) : QColor(80, 120, 180));
    if (!image.save(path, "PNG"))
        return setError(QStringLiteral("写入测试图片失败: %1").arg(path));
    return true;
}

QVariantMap detectionPrediction(const int class_id, const QString &class_name, const double score, const double x,
                                 const double y, const double width, const double height)
{
    // Let EvaluationDataset generate image-scoped IDs.  A fixture-level ID
    // based only on class_id would collide when several images share a class.
    return {{QStringLiteral("class_id"), class_id},
            {QStringLiteral("class_name"), class_name},
            {QStringLiteral("score"), score},
            {QStringLiteral("x"), x},
            {QStringLiteral("y"), y},
            {QStringLiteral("width"), width},
            {QStringLiteral("height"), height}};
}

QVariantMap anomalyPrediction(const double image_score)
{
    return {{QStringLiteral("image_score"), image_score}};
}

} // namespace dltool::model::testsupport
