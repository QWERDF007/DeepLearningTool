#pragma once

#include "dltool/model/Export.h"
#include "model/ModelEvaluationModels.h"
#include "model/ModelEvaluationProtocol.h"

#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace dltool::model {

/**
 * @brief 评估方法的能力声明（指标/图表能力位化）。
 */
struct MODEL_API EvaluationCapabilities
{
    bool        has_instance_metrics{false}; ///< 是否产出实例级指标。
    bool        has_image_metrics{false};    ///< 是否产出图像级指标。
    bool        has_confusion_matrix{false}; ///< 是否产出混淆矩阵。
    bool        has_instance_events{false};  ///< 是否产出实例事件。
    QStringList chart_kinds;                 ///< 图表类型列表。
};

/**
 * @brief 由评估方法推导能力声明。
 *
 * 检测方法具备实例指标与混淆矩阵；异常检测为图像级二元分类（无类别错误
 * 语义），仅具备图像指标。Service 结果组装与图表构造共用同一推导。
 * @param method 评估方法。
 * @return 能力声明。
 */
inline EvaluationCapabilities evaluationCapabilitiesForMethod(const evaluation::Method method)
{
    EvaluationCapabilities capabilities;
    capabilities.has_instance_metrics = evaluation::hasInstanceMetrics(method);
    capabilities.has_image_metrics    = evaluation::hasImageMetrics(method);
    capabilities.has_confusion_matrix = evaluation::hasConfusionMatrix(method);
    capabilities.has_instance_events  = evaluation::hasInstanceEvents(method);
    if (evaluation::isAnomaly(method))
        capabilities.chart_kinds = {evaluation::chartKindKey(evaluation::ChartKind::Line)};
    else if (capabilities.has_instance_metrics)
        capabilities.chart_kinds = {evaluation::chartKindKey(evaluation::ChartKind::Bar),
                                    evaluation::chartKindKey(evaluation::ChartKind::Line)};
    return capabilities;
}

/**
 * @brief 按类别 ID 取评估界面调色板颜色。
 *
 * Service 与 ViewModel 共用的类别配色：同一类别在两个模块中呈现一致，
 * 避免各维护一份完全相同的调色板。
 * @param class_id 类别 ID；负数使用第一个颜色。
 * @return 调色板中的颜色字符串。
 */
inline QString classColor(const int class_id)
{
    static const QStringList palette
        = {QStringLiteral("#ef5350"), QStringLiteral("#42a5f5"), QStringLiteral("#66bb6a"), QStringLiteral("#ffa726"),
           QStringLiteral("#ab47bc"), QStringLiteral("#26c6da"), QStringLiteral("#8d6e63"), QStringLiteral("#78909c")};
    const int index = class_id >= 0 ? class_id % palette.size() : 0;
    return palette.at(index);
}

/**
 * @brief 从几何/边界记录中读取文本字段。
 * @param map 记录映射。
 * @param field 评估协议字段。
 * @param fallback 缺省时的回退值。
 * @return 字段值。
 */
inline QString recordText(const QVariantMap &map, const evaluation::Field field, const QString &fallback = {})
{
    const QString value = map.value(evaluation::fieldName(field)).toString();
    return value.isEmpty() ? fallback : value;
}

/**
 * @brief 从几何/边界记录中读取整型字段。
 * @param map 记录映射。
 * @param field 评估协议字段。
 * @param fallback 缺省时的回退值。
 * @return 字段值。
 */
inline int recordInt(const QVariantMap &map, const evaluation::Field field, const int fallback = -1)
{
    bool      ok    = false;
    const int value = map.value(evaluation::fieldName(field)).toInt(&ok);
    return ok ? value : fallback;
}

/**
 * @brief 从几何/边界记录中读取 64 位整型字段。
 * @param map 记录映射。
 * @param field 评估协议字段。
 * @param fallback 缺省时的回退值。
 * @return 字段值。
 */
inline qint64 recordLong(const QVariantMap &map, const evaluation::Field field, const qint64 fallback = 0)
{
    bool         ok    = false;
    const qint64 value = map.value(evaluation::fieldName(field)).toLongLong(&ok);
    return ok ? value : fallback;
}

/**
 * @brief 从几何/边界记录中读取浮点字段。
 * @param map 记录映射。
 * @param field 评估协议字段。
 * @param fallback 缺省时的回退值。
 * @return 字段值。
 */
inline double recordReal(const QVariantMap &map, const evaluation::Field field, const double fallback = 0.0)
{
    bool         ok    = false;
    const double value = map.value(evaluation::fieldName(field)).toDouble(&ok);
    return ok ? value : fallback;
}

/**
 * @brief 将评估结果实例事件记录从映射转换为值对象。
 *
 * Service 产出的实例事件是协议映射，ViewModel 按值对象消费；该转换被
 * 评估结果加载与实例记录加载共用。
 * @param map 实例事件映射。
 * @return 实例记录值对象。
 */
inline EvaluationInstanceRecord instanceFromMap(const QVariantMap &map)
{
    EvaluationInstanceRecord record;
    record.event_uuid          = recordText(map, evaluation::Field::EventUuid);
    record.image_id            = recordLong(map, evaluation::Field::ImageId, -1);
    record.dataset_id          = recordLong(map, evaluation::Field::DatasetId, -1);
    record.image_name          = recordText(map, evaluation::Field::ImageName);
    record.image_path          = recordText(map, evaluation::Field::ImagePath);
    record.image_width         = recordInt(map, evaluation::Field::ImageWidth, 0);
    record.image_height        = recordInt(map, evaluation::Field::ImageHeight, 0);
    record.status              = evaluation::statusFromKey(recordText(map, evaluation::Field::Status));
    record.score               = recordReal(map, evaluation::Field::Score);
    record.iou                 = recordReal(map, evaluation::Field::Iou);
    record.gt_label_id         = recordLong(map, evaluation::Field::GtLabelId, -1);
    record.gt_instance_id      = record.gt_label_id >= 0 ? QString::number(record.gt_label_id) : QString();
    record.pred_instance_id    = recordText(map, evaluation::Field::PredInstanceId);
    record.gt_class_id         = recordInt(map, evaluation::Field::GtClassId);
    record.pred_class_id       = recordInt(map, evaluation::Field::PredClassId);
    record.gt_class            = recordText(map, evaluation::Field::GtClassName);
    record.pred_class          = recordText(map, evaluation::Field::PredClassName);
    record.gt_geometry         = map.value(evaluation::fieldName(evaluation::Field::GtGeometry)).toMap();
    record.pred_geometry       = map.value(evaluation::fieldName(evaluation::Field::PredGeometry)).toMap();
    record.gt_bounds           = record.gt_geometry.value(evaluation::fieldName(evaluation::Field::Bounds)).toMap();
    record.pred_bounds         = record.pred_geometry.value(evaluation::fieldName(evaluation::Field::Bounds)).toMap();
    record.crop_bounds         = map.value(evaluation::fieldName(evaluation::Field::CropBounds)).toMap();
    record.gt_overlay_bounds   = map.value(evaluation::fieldName(evaluation::Field::GtOverlayBounds)).toMap();
    record.pred_overlay_bounds = map.value(evaluation::fieldName(evaluation::Field::PredOverlayBounds)).toMap();
    record.gt_overlay_points   = map.value(evaluation::fieldName(evaluation::Field::GtOverlayPoints)).toList();
    record.pred_overlay_points = map.value(evaluation::fieldName(evaluation::Field::PredOverlayPoints)).toList();
    record.gt_mask_url         = recordText(map, evaluation::Field::GtMaskUrl);
    record.pred_mask_url       = recordText(map, evaluation::Field::PredMaskUrl);
    if (record.image_name.isEmpty())
        record.image_name = QFileInfo(record.image_path).fileName();
    return record;
}

/**
 * @brief 将实例记录序列化为 QML 使用的映射。
 *
 * 与 instanceFromMap 互为逆转换；QML 通过该映射展示实例详情。
 * @param record 实例记录。
 * @return QML 映射。
 */
inline QVariantMap instanceToMap(const EvaluationInstanceRecord &record)
{
    return {
        {        QStringLiteral("eventUuid"),                            record.event_uuid},
        {          QStringLiteral("imageId"),                              record.image_id},
        {        QStringLiteral("datasetId"),                            record.dataset_id},
        {        QStringLiteral("imageName"),                            record.image_name},
        {        QStringLiteral("imagePath"),                            record.image_path},
        {       QStringLiteral("imageWidth"),                           record.image_width},
        {      QStringLiteral("imageHeight"),                          record.image_height},
        {           QStringLiteral("status"),         evaluation::statusKey(record.status)},
        {       QStringLiteral("statusKind"),              static_cast<int>(record.status)},
        {       QStringLiteral("statusText"), evaluation::statusDisplayName(record.status)},
        {          QStringLiteral("gtClass"),                              record.gt_class},
        {      QStringLiteral("gtClassName"),                              record.gt_class},
        {        QStringLiteral("predClass"),                            record.pred_class},
        {    QStringLiteral("predClassName"),                            record.pred_class},
        {        QStringLiteral("gtClassId"),                           record.gt_class_id},
        {      QStringLiteral("predClassId"),                         record.pred_class_id},
        {        QStringLiteral("gtLabelId"),                           record.gt_label_id},
        {     QStringLiteral("gtInstanceId"),                        record.gt_instance_id},
        {   QStringLiteral("predInstanceId"),                      record.pred_instance_id},
        {     QStringLiteral("gtClassColor"),                        record.gt_class_color},
        {   QStringLiteral("predClassColor"),                      record.pred_class_color},
        {     QStringLiteral("thumbnailUrl"),                         record.thumbnail_url},
        {            QStringLiteral("score"),                                 record.score},
        {        QStringLiteral("predScore"),                                 record.score},
        {              QStringLiteral("iou"),                                   record.iou},
        {         QStringLiteral("selected"),                              record.selected},
        {       QStringLiteral("gtGeometry"),                           record.gt_geometry},
        {     QStringLiteral("predGeometry"),                         record.pred_geometry},
        {         QStringLiteral("gtBounds"),                             record.gt_bounds},
        {       QStringLiteral("predBounds"),                           record.pred_bounds},
        {       QStringLiteral("cropBounds"),                           record.crop_bounds},
        {  QStringLiteral("gtOverlayBounds"),                     record.gt_overlay_bounds},
        {QStringLiteral("predOverlayBounds"),                   record.pred_overlay_bounds},
        {  QStringLiteral("gtOverlayPoints"),                     record.gt_overlay_points},
        {QStringLiteral("predOverlayPoints"),                   record.pred_overlay_points},
        {        QStringLiteral("gtMaskUrl"),                           record.gt_mask_url},
        {      QStringLiteral("predMaskUrl"),                         record.pred_mask_url}
    };
}


/**
 * @brief 将实例事件值对象序列化为协议映射（snake_case 字段名）。
 *
 * EvaluationResult::event_maps 与 InstanceRecords 协议字段使用该格式，
 * 与 instanceFromMap() 保持严格互逆；instanceToMap() 是 QML 展示格式，
 * 两者不可混用。
 */
inline QVariantMap instanceToProtocolMap(const EvaluationInstanceRecord &record)
{
    return {
        {      evaluation::fieldName(evaluation::Field::EventUuid),    record.event_uuid},
        {        evaluation::fieldName(evaluation::Field::ImageId),      record.image_id},
        {      evaluation::fieldName(evaluation::Field::DatasetId),    record.dataset_id},
        {      evaluation::fieldName(evaluation::Field::ImageName),      record.image_name},
        {      evaluation::fieldName(evaluation::Field::ImagePath),      record.image_path},
        {     evaluation::fieldName(evaluation::Field::ImageWidth),     record.image_width},
        {    evaluation::fieldName(evaluation::Field::ImageHeight),    record.image_height},
        {         evaluation::fieldName(evaluation::Field::Status), evaluation::statusKey(record.status)},
        {         evaluation::fieldName(evaluation::Field::Score),           record.score},
        {          evaluation::fieldName(evaluation::Field::Iou),            record.iou},
        {     evaluation::fieldName(evaluation::Field::GtLabelId),     record.gt_label_id},
        {     evaluation::fieldName(evaluation::Field::PredInstanceId), record.pred_instance_id},
        {       evaluation::fieldName(evaluation::Field::GtClassId),     record.gt_class_id},
        {     evaluation::fieldName(evaluation::Field::PredClassId),   record.pred_class_id},
        {     evaluation::fieldName(evaluation::Field::GtClassName),     record.gt_class},
        {    evaluation::fieldName(evaluation::Field::PredClassName),    record.pred_class},
        {       evaluation::fieldName(evaluation::Field::GtGeometry),   record.gt_geometry},
        {     evaluation::fieldName(evaluation::Field::PredGeometry), record.pred_geometry},
        {       evaluation::fieldName(evaluation::Field::CropBounds),   record.crop_bounds},
        { evaluation::fieldName(evaluation::Field::GtOverlayBounds), record.gt_overlay_bounds},
        {evaluation::fieldName(evaluation::Field::PredOverlayBounds), record.pred_overlay_bounds},
        { evaluation::fieldName(evaluation::Field::GtOverlayPoints), record.gt_overlay_points},
        {evaluation::fieldName(evaluation::Field::PredOverlayPoints), record.pred_overlay_points},
        {       evaluation::fieldName(evaluation::Field::GtMaskUrl),     record.gt_mask_url},
        {     evaluation::fieldName(evaluation::Field::PredMaskUrl),   record.pred_mask_url}
    };
}
} // namespace dltool::model
