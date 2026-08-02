#pragma once

#include "dltool/model/Export.h"
#include "core/CoreDef.h"

#include <QString>

namespace dltool::model::evaluation {

/**
 * @brief 评估层使用的统一方法类型。
 *
 * 评估直接复用项目级方法枚举，避免在任务请求、评估服务和报告协议之间
 * 维护第二套方法类型。Unknown 仅用于未初始化或不受评估协议支持的方法。
 */
using Method = core::DeepLearningMethod::Method;

enum class Status
{
    Unknown = 0,
    TruePositive,
    TrueNegative,
    ClassMismatch,
    FalsePositive,
    FalseNegative,
    Ignored,
};

enum class MatchingStrategy
{
    GreedyIoU = 0,
    HungarianIoU,
};

enum class MetricSet
{
    Diagnostic = 0,
    Official,
};

enum class CellKind
{
    Match = 0,
    ClassMismatch,
    FalsePositive,
    FalseNegative,
    PredTotal,
    GtTotal,
    FalsePositiveTotal,
    FalseNegativeTotal,
    All,
    NotApplicable,
};

constexpr int kReportSchemaVersion = 3;
constexpr int kResultSchemaVersion = 2;
constexpr double kDefaultConfidenceThreshold = 0.5;
constexpr double kDefaultIouThreshold = 0.5;

MODEL_API Method fromProjectMethod(int method);
MODEL_API Method fromProjectMethod(core::DeepLearningMethod::Method method);
MODEL_API Method methodFromKey(const QString &key);
MODEL_API QString methodKey(Method method);

MODEL_API QString statusKey(Status status);
MODEL_API Status statusFromKey(const QString &key);
MODEL_API QString statusDisplayName(Status status);

MODEL_API QString matchingStrategyKey(MatchingStrategy strategy);
MODEL_API MatchingStrategy matchingStrategyFromKey(const QString &key);

MODEL_API QString metricSetKey(MetricSet metric_set);
MODEL_API MetricSet metricSetFromKey(const QString &key);
MODEL_API QString cellKindKey(CellKind kind);
MODEL_API CellKind cellKindFromKey(const QString &key);

MODEL_API bool isAnomaly(Method method);
MODEL_API bool hasInstanceMetrics(Method method);
MODEL_API bool hasImageMetrics(Method method);
MODEL_API bool hasConfusionMatrix(Method method);
MODEL_API bool hasInstanceEvents(Method method);

} // namespace dltool::model::evaluation
