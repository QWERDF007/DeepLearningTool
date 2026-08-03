#pragma once

#include "dltool/model/Export.h"
#include "model/ModelEvaluationProtocol.h"

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
    evaluation::Method method{evaluation::Method::Unknown};
    QString dataset_manifest_path;
    QString prediction_manifest_path;
    QString prediction_images_path;
    // Keep the complete normalized evaluation group as the in-memory cache
    // key.  The scalar fields below are the values consumed by the evaluator.
    QVariantMap evaluation_config;
    double confidence_threshold{evaluation::kDefaultConfidenceThreshold};
    double iou_threshold{evaluation::kDefaultIouThreshold};
    evaluation::MatchingStrategy matching_strategy{evaluation::MatchingStrategy::GreedyIoU};
    // Optional cooperative cancellation token owned by the task controller.
    // The evaluator never dereferences a QObject and can therefore poll this
    // value safely from its worker thread.
    std::shared_ptr<std::atomic_bool> cancel_token;
};

struct MODEL_API ModelEvaluationResult
{
    // The evaluator owns the complete in-memory snapshot consumed directly by
    // ModelEvaluationViewModel; no evaluation output file is produced.
    QVariantMap evaluation_data;
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
 * @brief Reads the normalized dataset/PRED protocol and computes one
 * complete evaluation snapshot in memory.
 */
class MODEL_API ModelEvaluationService
{
public:
    static EvaluationCapabilities capabilitiesForMethod(evaluation::Method method);
    static bool evaluate(const ModelEvaluationOptions &options, ModelEvaluationResult *result = nullptr,
                         QString *err_msg = nullptr);

};

} // namespace dltool::model
