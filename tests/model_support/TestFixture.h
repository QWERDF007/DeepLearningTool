#pragma once

#include "model/ModelDatasetSelection.h"

#include <QList>
#include <QPair>
#include <QPointF>
#include <QString>
#include <QTemporaryDir>
#include <QVariantMap>

#include <memory>
#include <vector>

namespace dltool::model::testsupport {

/**
 * @brief 临时项目/任务数据库夹具。
 *
 * The fixture uses the public database APIs instead of copying a developer
 * database.  Each instance owns an isolated directory under DLT_TEST_TMP_ROOT
 * (F:\\tmp by default) and removes it when the test finishes.
 */
class EvaluationFixture
{
public:
    explicit EvaluationFixture(int method);
    ~EvaluationFixture();

    EvaluationFixture(const EvaluationFixture &) = delete;
    EvaluationFixture &operator=(const EvaluationFixture &) = delete;

    bool isValid() const;
    QString error() const;

    QString rootPath() const;
    QString projectDatabasePath() const;
    QString taskDatabasePath() const;
    QString fileListPath() const;
    QString predictionDirectory() const;

    qint64 datasetId() const;
    const QList<qint64> &classIds() const;
    const QList<qint64> &imageIds() const;

    qint64 addClass(const QString &name, const QString &group = QStringLiteral("anomaly"));
    qint64 addImage(const QString &name, const QVariantMap &image_level = {});
    qint64 addDetectionLabel(qint64 image_id, qint64 class_id, double x, double y, double width, double height);
    qint64 addSegmentationLabel(qint64 image_id, qint64 class_id, const std::vector<QPointF> &points);
    qint64 addAnomalyLabel(qint64 image_id, qint64 class_id, const std::vector<QPointF> &points);
    bool writeImageList(const QList<QPair<qint64, QString>> &rows = {});
    bool setTestSelection(const QList<qint64> &class_ids = {});
    bool writePrediction(qint64 image_id, const QVariant &prediction);
    bool removePrediction(qint64 image_id);

private:
    bool setError(const QString &message);
    bool writeImageFile(const QString &path, bool anomaly_variant);

    int method_{-1};
    std::unique_ptr<QTemporaryDir> temporary_dir_;
    QString error_;
    QString project_database_path_;
    QString task_database_path_;
    QString file_list_path_;
    QString prediction_directory_;
    qint64 dataset_id_{-1};
    QList<qint64> class_ids_;
    QList<qint64> image_ids_;
    QStringList image_paths_;
};

QVariantMap detectionPrediction(int class_id, const QString &class_name, double score, double x, double y,
                                 double width, double height);
QVariantMap anomalyPrediction(double image_score);

} // namespace dltool::model::testsupport
