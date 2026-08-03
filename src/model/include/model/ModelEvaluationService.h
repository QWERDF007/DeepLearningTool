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
    QString evaluation_dir;
    QString report_path;
    double confidence_threshold{evaluation::kDefaultConfidenceThreshold};
    double iou_threshold{evaluation::kDefaultIouThreshold};
    evaluation::MatchingStrategy matching_strategy{evaluation::MatchingStrategy::GreedyIoU};
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
    static EvaluationCapabilities capabilitiesForMethod(evaluation::Method method);
    static bool evaluate(const ModelEvaluationOptions &options, ModelEvaluationResult *result = nullptr,
                         QString *err_msg = nullptr);

};

} // namespace dltool::model
