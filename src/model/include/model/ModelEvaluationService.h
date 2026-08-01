#pragma once

#include "dltool/model/Export.h"

#include <QVariantMap>
#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>

namespace dltool::model {

/**
 * @brief C++ evaluation input.  The service deliberately accepts paths and
 * plain values only, so it can run off the GUI thread and does not depend on
 * QML models or DataManager objects.
 */
struct MODEL_API ModelEvaluationOptions
{
    QString model_uuid;
    QString test_task_uuid;
    QString model_name;
    QString task_directory;
    QString method;
    QString dataset_manifest_path;
    QString prediction_manifest_path;
    QString prediction_images_path;
    QString evaluation_dir;
    QString evaluation_config_path;
    QString report_path;
    QString instances_path;
    double confidence_threshold{0.5};
    double iou_threshold{0.5};
    QString matching_strategy{QStringLiteral("greedy_iou")};
    QVariantMap evaluation_config;
    // Optional cooperative cancellation token owned by the task controller.
    // The evaluator never dereferences a QObject and can therefore poll this
    // value safely from its worker thread.
    std::shared_ptr<std::atomic_bool> cancel_token;
};

struct MODEL_API ModelEvaluationResult
{
    int image_count{0};
    int prediction_count{0};
    int event_count{0};
    QString inference_digest;
    QString evaluation_digest;
    QVariantMap result;
};

struct MODEL_API EvaluationCapabilities
{
    bool has_instance_metrics{false};
    bool has_image_metrics{false};
    bool has_confusion_matrix{false};
    bool has_instance_events{false};
    QStringList chart_kinds;
};

/**
 * @brief Reads the normalized dataset/PRED protocol, computes diagnostic
 * matching metrics, and atomically commits evaluation YAML files.
 */
class MODEL_API ModelEvaluationService
{
public:
    static EvaluationCapabilities capabilitiesForMethod(const QString &method);
    static bool evaluate(const ModelEvaluationOptions &options, ModelEvaluationResult *result = nullptr,
                         QString *err_msg = nullptr);

    static bool validatePrediction(const QString &images_path, const QString &manifest_path,
                                   int *image_count = nullptr, int *prediction_count = nullptr,
                                   QString *err_msg = nullptr,
                                   const QString &expected_model_uuid = {},
                                   const QString &expected_task_uuid = {},
                                   const QString &expected_method = {});
};

} // namespace dltool::model
