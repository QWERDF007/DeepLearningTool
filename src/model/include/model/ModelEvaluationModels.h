#pragma once

#include "dltool/model/Export.h"
#include "model/EvaluationData.h"
#include "model/ModelEvaluationProtocol.h"

#include <QAbstractListModel>
#include <QAbstractTableModel>
#include <QPointer>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
#include <QtQml>
#include <limits>
#include <vector>

namespace dltool::model {

/**
 * @brief 评估指标记录结构体。
 *
 * 表示实例级、图像级或单类别维度的统计指标数据，包括精确率、召回率、F1 分数、AP 及 TP/FP/FN 计数。
 */
struct MODEL_API EvaluationMetricRecord
{
    QString key;                      ///< 指标唯一键（如类别名称或 "overall"）。
    QString label;                    ///< 指标展示标签。
    QString class_name;               ///< 类别名称（如果是类别指标）。
    int     class_id{-1};             ///< 类别 ID。
    double  precision{0.0};           ///< 精确率（0.0 ~ 1.0）。
    double  recall{0.0};              ///< 召回率（0.0 ~ 1.0）。
    double  f1{0.0};                  ///< F1 分数（0.0 ~ 1.0）。
    double  ap{0.0};                  ///< 平均精确率（Average Precision，0.0 ~ 1.0）。
    bool    precision_defined{false}; ///< 精确率是否有效定义（除数非零）。
    bool    recall_defined{false};    ///< 召回率是否有效定义。
    bool    f1_defined{false};        ///< F1 分数是否有效定义。
    bool    ap_defined{false};        ///< AP 是否有效定义。
    QString class_color;              ///< 类别渲染颜色（HEX 字符串）。
    qint64  tp{0};                    ///< 真正例（True Positive）计数。
    qint64  fp{0};                    ///< 假正例（False Positive）计数。
    qint64  fn{0};                    ///< 假负例（False Negative）计数。
};

/**
 * @brief 混淆矩阵单元格结构体。
 *
 * 表示混淆矩阵中的一个网格项，记录预测行与标注列对应的实例/图像计数及属性。
 */
struct MODEL_API EvaluationConfusionCell
{
    QString              row_key;                                        ///< 行键值（通常为预测类别键）。
    QString              column_key;                                     ///< 列键值（通常为 GT 标注类别键）。
    QString              row_label;                                      ///< 行显示文本。
    QString              column_label;                                   ///< 列显示文本。
    qint64               count{0};                                       ///< 单元格统计数量。
    int                  row_class_id{-1};                               ///< 行对应的类别 ID。
    int                  column_class_id{-1};                            ///< 列对应的类别 ID。
    evaluation::CellKind cell_kind{evaluation::CellKind::NotApplicable}; ///< 单元格类型（匹配、错检、漏检、合计等）。
    QString              tooltip;                                        ///< 鼠标悬浮提示文本。
    bool                 selectable{true};                               ///< 是否支持交互点击过滤。
    bool                 diagonal{false};                                ///< 是否位于对角线（即预测与 GT 一致）。
    bool                 error{false};                                   ///< 是否属于错误类别项（非对角线或错检漏检）。
};

/**
 * @brief 评估实例事件记录结构体。
 *
 * 表示单个匹配或孤立的实例事件（TP/FP/FN/类别错检），用于结果列表与图元展示。
 */
struct MODEL_API EvaluationInstanceRecord
{
    QString            event_uuid;                          ///< 事件全局唯一 UUID。
    qint64             image_id{-1};                        ///< 所属图像 ID。
    qint64             dataset_id{-1};                      ///< 所属数据集 ID。
    QString            image_name;                          ///< 图像文件名。
    QString            image_path;                          ///< 图像完整文件路径。
    int                image_width{0};                      ///< 图像像素宽度。
    int                image_height{0};                     ///< 图像像素高度。
    evaluation::Status status{evaluation::Status::Unknown}; ///< 事件匹配状态（TP, FP, FN, ClassMismatch 等）。
    QString            gt_class;                            ///< GT 标注类别名称。
    QString            pred_class;                          ///< 模型预测类别名称。
    int                gt_class_id{-1};                     ///< GT 标注类别 ID。
    int                pred_class_id{-1};                   ///< 模型预测类别 ID。
    double             score{0.0};                          ///< 预测置信度分数。
    double             iou{0.0};                            ///< 预测与 GT 之间的重叠度 IoU。
    QVariantMap        gt_geometry;                         ///< GT 几何图形数据。
    QVariantMap        pred_geometry;                       ///< 预测几何图形数据。
    QVariantMap        gt_bounds;                           ///< GT 边界框。
    QVariantMap        pred_bounds;                         ///< 预测边界框。
    QVariantMap        crop_bounds;                         ///< 缩略图裁剪区域边界。
    QVariantMap        gt_overlay_bounds;                   ///< GT 图元在缩略图坐标系下的覆盖边界。
    QVariantMap        pred_overlay_bounds;                 ///< 预测图元在缩略图坐标系下的覆盖边界。
    QVariantList       gt_overlay_points;                   ///< GT 掩膜或多边形折线点集。
    QVariantList       pred_overlay_points;                 ///< 预测掩膜或多边形折线点集。
    QString            gt_mask_url;                         ///< GT 掩膜图像资源 URL。
    QString            pred_mask_url;                       ///< 预测掩膜图像资源 URL。
    qint64             gt_label_id{-1};                     ///< 关联的标注标签 ID。
    QString            gt_instance_id;                      ///< GT 实例标识。
    QString            pred_instance_id;                    ///< 预测实例标识。
    QString            gt_class_color;                      ///< GT 类别颜色。
    QString            pred_class_color;                    ///< 预测类别颜色。
    QString            thumbnail_url;                       ///< 缩略图请求 URL。
    QString            anomaly_score_map_path;               ///< 原始异常分数图 TIFF 路径。
    QVariantList       anomaly_model_polygons;               ///< 模型坐标系异常区域多边形集合。
    QVariantList       anomaly_image_polygons;               ///< 原图坐标系异常区域多边形集合。
    bool               selected{false};                     ///< 当前是否被选中。
};

/**
 * @brief Service 与 ViewModel 共用同一套值对象层级别名。
 */
using EvaluationGroundTruthRecord = EvaluationGroundTruthData;
using EvaluationPredictionRecord  = EvaluationPredictionData;
using EvaluationImageRecord       = EvaluationImageData;

/**
 * @brief 根据当前 GT/预测列表重建图像记录的派生字段。
 *
 * 过滤聚合会替换实例列表但不经过 EvaluationImageModel::setRecords()，
 * 因此调用方在提交裁剪后的值记录前必须显式刷新这些缓存字段。
 * @param record 待刷新派生字段的图像记录对象。
 */
MODEL_API void rebuildImageDerivedValues(EvaluationImageRecord &record);

/**
 * @brief 评估指标列表数据模型。
 *
 * 封装 EvaluationMetricRecord 集合，供 QML 界面中各种指标面板与表格进行数据绑定。
 */
class MODEL_API EvaluationMetricModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(EvaluationMetricModel)
    QML_UNCREATABLE("EvaluationMetricModel is owned by ModelEvaluationViewModel")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged FINAL)
public:
    /**
     * @brief 数据角色枚举。
     */
    enum Role
    {
        KeyRole = Qt::UserRole + 1, ///< 指标键。
        LabelRole,                  ///< 显示标签。
        ClassNameRole,              ///< 类别名称。
        ClassIdRole,                ///< 类别 ID。
        PrecisionRole,              ///< 精确率数值。
        RecallRole,                 ///< 召回率数值。
        F1Role,                     ///< F1 分数数值。
        ApRole,                     ///< AP 数值。
        PrecisionTextRole,          ///< 精确率格式化文本。
        RecallTextRole,             ///< 召回率格式化文本。
        F1TextRole,                 ///< F1 格式化文本。
        ApTextRole,                 ///< AP 格式化文本。
        ClassColorRole,             ///< 类别颜色。
        PredictedCountRole,         ///< 预测数（TP + FP）。
        LabeledCountRole,           ///< 标注数（TP + FN）。
        FpPredictedTextRole,        ///< FP / 预测数组合文本。
        FnLabeledTextRole,          ///< FN / 标注数组合文本。
        TpRole,                     ///< TP 数量。
        FpRole,                     ///< FP 数量。
        FnRole,                     ///< FN 数量。
    };
    Q_ENUM(Role)

    explicit EvaluationMetricModel(QObject *parent = nullptr);
    int                    rowCount(const QModelIndex &parent = {}) const override;
    QVariant               data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /**
     * @brief 设置指标记录集合并触发视图刷新。
     * @param records 指标记录向量。
     */
    void setRecords(std::vector<EvaluationMetricRecord> records);

    /**
     * @brief 获取底层指标记录集合。
     * @return 指标记录向量常引用。
     */
    const std::vector<EvaluationMetricRecord> &records() const;

signals:
    /**
     * @brief 记录数量变化信号。
     */
    void countChanged();

private:
    std::vector<EvaluationMetricRecord> records_; ///< 内部存储的指标记录集合。
};

/**
 * @brief 评估混淆矩阵二维表格数据模型。
 *
 * 继承自 QAbstractTableModel，提供混淆矩阵单元格及表头数据的 QML 绑定。
 */
class MODEL_API EvaluationConfusionModel : public QAbstractTableModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(EvaluationConfusionModel)
    QML_UNCREATABLE("EvaluationConfusionModel is owned by ModelEvaluationViewModel")
public:
    /**
     * @brief 混淆矩阵数据角色枚举。
     */
    enum Role
    {
        RowKeyRole = Qt::UserRole + 1, ///< 行标识。
        ColumnKeyRole,                 ///< 列标识。
        RowLabelRole,                  ///< 行标签文本。
        ColumnLabelRole,               ///< 列标签文本。
        CountRole,                     ///< 单元格数量。
        RowClassIdRole,                ///< 行类别 ID。
        ColumnClassIdRole,             ///< 列类别 ID。
        CellKindRole,                  ///< 单元格类型字符串。
        CellKindValueRole,             ///< 单元格类型枚举整型值。
        SelectableRole,                ///< 是否可点击选择。
        IsDiagonalRole,                ///< 是否为对角线单元格。
        IsErrorRole,                   ///< 是否为错误单元格。
        TooltipRole,                   ///< 悬浮提示文本。
    };
    Q_ENUM(Role)

    /**
     * @brief 单元格类型枚举值（导出至 QML）。
     */
    enum CellKindValue
    {
        CellKindMatch              = static_cast<int>(evaluation::CellKind::Match),              ///< 正确匹配。
        CellKindClassMismatch      = static_cast<int>(evaluation::CellKind::ClassMismatch),      ///< 类别分类错误。
        CellKindFalsePositive      = static_cast<int>(evaluation::CellKind::FalsePositive),      ///< 误检。
        CellKindFalseNegative      = static_cast<int>(evaluation::CellKind::FalseNegative),      ///< 漏检。
        CellKindPredTotal          = static_cast<int>(evaluation::CellKind::PredTotal),          ///< 预测总计。
        CellKindGtTotal            = static_cast<int>(evaluation::CellKind::GtTotal),            ///< GT 总计。
        CellKindFalsePositiveTotal = static_cast<int>(evaluation::CellKind::FalsePositiveTotal), ///< 误检总计。
        CellKindFalseNegativeTotal = static_cast<int>(evaluation::CellKind::FalseNegativeTotal), ///< 漏检总计。
        CellKindAll                = static_cast<int>(evaluation::CellKind::All),                ///< 全部实例总数。
        CellKindNotApplicable      = static_cast<int>(evaluation::CellKind::NotApplicable),      ///< 不适用。
    };
    Q_ENUM(CellKindValue)

    explicit EvaluationConfusionModel(QObject *parent = nullptr);
    int      rowCount(const QModelIndex &parent = {}) const override;
    int      columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /**
     * @brief 设置混淆矩阵单元格数据并刷新网格维度。
     * @param records 扁平化的单元格列表。
     */
    void setRecords(std::vector<EvaluationConfusionCell> records);

    /**
     * @brief 获取所有单元格记录集合。
     * @return 单元格向量常引用。
     */
    const std::vector<EvaluationConfusionCell> &records() const;

private:
    std::vector<EvaluationConfusionCell> records_;         ///< 单元格记录列表。
    int                                  row_count_{0};    ///< 行数。
    int                                  column_count_{0}; ///< 列数。
};

/**
 * @brief 评估实例事件列表数据模型。
 *
 * 管理检测/分割/分类输出的所有实例级匹配事件，供九宫格/瀑布流视图与详细信息面板展示。
 */
class MODEL_API EvaluationInstanceModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(EvaluationInstanceModel)
    QML_UNCREATABLE("EvaluationInstanceModel is owned by ModelEvaluationViewModel")
public:
    /**
     * @brief 实例事件数据角色枚举。
     */
    enum Role
    {
        EventUuidRole = Qt::UserRole + 1, ///< 事件 UUID。
        ImageIdRole,                      ///< 图像 ID。
        DatasetIdRole,                    ///< 数据集 ID。
        ImageNameRole,                    ///< 图像名称。
        ImagePathRole,                    ///< 图像路径。
        ImageWidthRole,                   ///< 图像宽度。
        ImageHeightRole,                  ///< 图像高度。
        StatusRole,                       ///< 事件状态（Status 枚举）。
        StatusKindRole,                   ///< 状态分类字符串。
        StatusTextRole,                   ///< 状态本地化文本。
        GtClassRole,                      ///< GT 类别名。
        GtClassNameRole,                  ///< GT 类别全名。
        PredClassRole,                    ///< 预测类别名。
        PredClassNameRole,                ///< 预测类别全名。
        GtClassIdRole,                    ///< GT 类别 ID。
        PredClassIdRole,                  ///< 预测类别 ID。
        ScoreRole,                        ///< 置信度分数。
        PredScoreRole,                    ///< 预测分数（别名）。
        IouRole,                          ///< 重叠度 IoU。
        GtGeometryRole,                   ///< GT 几何对象。
        PredGeometryRole,                 ///< 预测几何对象。
        GtBoundsRole,                     ///< GT 边界框。
        PredBoundsRole,                   ///< 预测边界框。
        CropBoundsRole,                   ///< 缩略图裁剪框。
        GtOverlayBoundsRole,              ///< GT 覆盖边界。
        PredOverlayBoundsRole,            ///< 预测覆盖边界。
        GtOverlayPointsRole,              ///< GT 覆盖多边形点。
        PredOverlayPointsRole,            ///< 预测覆盖多边形点。
        GtMaskUrlRole,                    ///< GT 掩膜 URL。
        PredMaskUrlRole,                  ///< 预测掩膜 URL。
        GtLabelIdRole,                    ///< GT 标签 ID。
        GtInstanceIdRole,                 ///< GT 实例标识。
        PredInstanceIdRole,               ///< 预测实例标识。
        GtClassColorRole,                 ///< GT 类别颜色。
        PredClassColorRole,               ///< 预测类别颜色。
        ThumbnailUrlRole,                 ///< 缩略图 URL。
        AnomalyScoreMapPathRole,          ///< 原始异常分数图 TIFF 路径。
        AnomalyModelPolygonsRole,         ///< 模型坐标系异常区域多边形集合。
        AnomalyImagePolygonsRole,         ///< 原图坐标系异常区域多边形集合。
        SelectedRole,                     ///< 是否选中。
    };
    Q_ENUM(Role)

    /**
     * @brief 匹配状态枚举值（导出至 QML）。
     */
    enum StatusValue
    {
        StatusUnknown       = static_cast<int>(evaluation::Status::Unknown),       ///< 未知状态。
        StatusTruePositive  = static_cast<int>(evaluation::Status::TruePositive),  ///< 真正例（正确检出）。
        StatusTrueNegative  = static_cast<int>(evaluation::Status::TrueNegative),  ///< 真负例（正确排除）。
        StatusClassMismatch = static_cast<int>(evaluation::Status::ClassMismatch), ///< 类别分类错误。
        StatusFalsePositive = static_cast<int>(evaluation::Status::FalsePositive), ///< 假正例（误检）。
        StatusFalseNegative = static_cast<int>(evaluation::Status::FalseNegative), ///< 假负例（漏检）。
        StatusIgnored       = static_cast<int>(evaluation::Status::Ignored),       ///< 忽略项。
    };
    Q_ENUM(StatusValue)

    explicit EvaluationInstanceModel(QObject *parent = nullptr);
    int                    rowCount(const QModelIndex &parent = {}) const override;
    QVariant               data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /**
     * @brief 设置实例记录集合。
     * @param records 实例事件记录向量。
     */
    void setRecords(std::vector<EvaluationInstanceRecord> records);

    /**
     * @brief 设置当前选中的事件 UUID。
     * @param eventUuid 事件唯一标识。
     */
    void setSelectedEvent(const QString &eventUuid);

    /**
     * @brief 获取所有实例事件记录。
     * @return 记录向量常引用。
     */
    const std::vector<EvaluationInstanceRecord> &records() const;

    /**
     * @brief 获取指定行号的实例记录指针。
     * @param row 行索引。
     * @return 实例记录指针，超出范围返回 nullptr。
     */
    const EvaluationInstanceRecord *recordAt(int row) const;

private:
    std::vector<EvaluationInstanceRecord> records_; ///< 底层实例记录集合。
};

/**
 * @brief 评估图像列表数据模型。
 *
 * 管理参与评估的原始图像记录及其派生的 GT/预测汇总信息。
 */
class MODEL_API EvaluationImageModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(EvaluationImageModel)
    QML_UNCREATABLE("EvaluationImageModel is owned by ModelEvaluationViewModel")
public:
    /**
     * @brief 图像数据角色枚举。
     */
    enum Role
    {
        ImageIdRole = Qt::UserRole + 1, ///< 图像 ID。
        DatasetIdRole,                  ///< 所属数据集 ID。
        ImageNameRole,                  ///< 图像名称。
        ImagePathRole,                  ///< 图像完整路径。
        ImageWidthRole,                 ///< 图像宽度。
        ImageHeightRole,                ///< 图像高度。
        GtLabelIdsRole,                 ///< 包含的 GT 标签 ID 列表。
        GtClassIdsRole,                 ///< 包含的 GT 类别 ID 列表。
        PredClassIdsRole,               ///< 包含的预测类别 ID 列表。
        ScoreRole,                      ///< 图像预测最高分。
        HasGtRole,                      ///< 是否包含 GT 标注。
        HasPredRole,                    ///< 是否包含有效预测。
    };
    Q_ENUM(Role)

    explicit EvaluationImageModel(QObject *parent = nullptr);
    int                    rowCount(const QModelIndex &parent = {}) const override;
    QVariant               data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /**
     * @brief 设置图像记录集合。
     * @param records 图像记录向量。
     */
    void setRecords(std::vector<EvaluationImageRecord> records);

    /**
     * @brief 获取所有图像记录。
     * @return 图像记录向量常引用。
     */
    const std::vector<EvaluationImageRecord> &records() const;

    /**
     * @brief 获取指定行号的图像记录指针。
     * @param row 行索引。
     * @return 图像记录指针，超出范围返回 nullptr。
     */
    const EvaluationImageRecord *recordAt(int row) const;

private:
    std::vector<EvaluationImageRecord> records_; ///< 底层图像记录列表。
};

/**
 * @brief 图像过滤代理模型。
 *
 * 基于外部全局过滤器（如数据集筛选、类别筛选）对 EvaluationImageModel 进行过滤。
 */
class MODEL_API EvaluationImageFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(EvaluationImageFilterProxyModel)
    QML_UNCREATABLE("EvaluationImageFilterProxyModel is owned by ModelEvaluationViewModel")
public:
    explicit EvaluationImageFilterProxyModel(QObject *parent = nullptr);

    /**
     * @brief 绑定外部全局过滤器对象。
     * @param filter 过滤器 QObject 实例。
     */
    void setGlobalFilter(QObject *filter);

    /**
     * @brief 判断指定图像记录是否满足当前过滤条件。
     * @param record 待检测图像记录。
     * @return 满足返回 true。
     */
    bool acceptsRecord(const EvaluationImageRecord &record) const;

signals:
    /**
     * @brief 过滤条件变更信号。
     */
    void filterChanged();

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override;

private slots:
    void onExternalFilterChanged();

private:
    QPointer<QObject> global_filter_; ///< 弱引用外部全局过滤器对象。
};

/**
 * @brief 全局过滤代理模型。
 *
 * 对实例事件进行数据集与类别的顶层筛选。
 */
class MODEL_API EvaluationGlobalFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(EvaluationGlobalFilterProxyModel)
    QML_UNCREATABLE("EvaluationGlobalFilterProxyModel is owned by ModelEvaluationViewModel")
    Q_PROPERTY(QVariantList datasetIds READ datasetIds WRITE setDatasetIds NOTIFY filterChanged FINAL)
    Q_PROPERTY(QVariantList classIds READ classIds WRITE setClassIds NOTIFY filterChanged FINAL)
public:
    explicit EvaluationGlobalFilterProxyModel(QObject *parent = nullptr);

    /**
     * @brief 获取当前过滤的数据集 ID 列表。
     */
    QVariantList datasetIds() const;

    /**
     * @brief 设置过滤的数据集 ID 列表。
     * @param ids 数据集 ID 列表（空表示全选）。
     */
    void setDatasetIds(const QVariantList &ids);

    /**
     * @brief 获取当前过滤的类别 ID 列表。
     */
    QVariantList classIds() const;

    /**
     * @brief 设置过滤的类别 ID 列表。
     * @param ids 类别 ID 列表（空表示全选）。
     */
    void setClassIds(const QVariantList &ids);

    /**
     * @brief 绑定外部全局过滤器对象。
     * @param filter 过滤器 QObject 实例。
     */
    void setGlobalFilter(QObject *filter);

    /**
     * @brief 判断实例记录是否满足全局过滤条件。
     * @param record 实例事件记录。
     * @return 满足返回 true。
     */
    bool acceptsRecord(const EvaluationInstanceRecord &record) const;

signals:
    /**
     * @brief 过滤条件变更信号。
     */
    void filterChanged();

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override;

private slots:
    void onExternalFilterChanged();

private:
    bool              acceptsGlobalLabel(const EvaluationInstanceRecord &record) const;
    QVariantList      dataset_ids_;   ///< 选中的数据集 ID。
    QVariantList      class_ids_;     ///< 选中的类别 ID。
    QPointer<QObject> global_filter_; ///< 弱引用外部过滤器。
};

/**
 * @brief 单元格与明细过滤代理模型。
 *
 * 针对混淆矩阵点击、状态过滤（TP/FP/FN）、预测类别、分数区间等复合条件进行第二层精确过滤。
 */
class MODEL_API EvaluationCellFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(EvaluationCellFilterProxyModel)
    QML_UNCREATABLE("EvaluationCellFilterProxyModel is owned by ModelEvaluationViewModel")
public:
    /**
     * @brief 实例图像列表的显示排序方式。
     *
     * SortNone 表示不按评估分数排序，仅按当前评估事件的 image_id 升序显示。
     * Score 模式同时表示异常检测分数或目标检测/语义分割的置信度。
     */
    enum SortMode
    {
        SortNone = 0,
        SortScoreAscending,
        SortScoreDescending,
        SortIouAscending,
        SortIouDescending,
    };
    Q_ENUM(SortMode)

    Q_PROPERTY(SortMode sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged FINAL)
    Q_PROPERTY(QString status READ status WRITE setStatus NOTIFY filterChanged FINAL)
    Q_PROPERTY(QString matrixRow READ matrixRow WRITE setMatrixRow NOTIFY filterChanged FINAL)
    Q_PROPERTY(QString matrixColumn READ matrixColumn WRITE setMatrixColumn NOTIFY filterChanged FINAL)
    Q_PROPERTY(QVariantList predClassIds READ predClassIds WRITE setPredClassIds NOTIFY filterChanged FINAL)
    Q_PROPERTY(double minScore READ minScore WRITE setMinScore NOTIFY filterChanged FINAL)
    Q_PROPERTY(double maxScore READ maxScore WRITE setMaxScore NOTIFY filterChanged FINAL)
public:
    explicit EvaluationCellFilterProxyModel(QObject *parent = nullptr);

    SortMode sortMode() const;
    void     setSortMode(SortMode mode);

    void setSourceModel(QAbstractItemModel *sourceModel) override;

    QString status() const;
    void    setStatus(const QString &status);

    QString matrixRow() const;
    void    setMatrixRow(const QString &value);

    QString matrixColumn() const;
    void    setMatrixColumn(const QString &value);

    QVariantList predClassIds() const;
    void         setPredClassIds(const QVariantList &ids);

    double minScore() const;
    void   setMinScore(double value);

    double maxScore() const;
    void   setMaxScore(double value);

    /**
     * @brief 判断实例记录是否满足单元格过滤规则。
     * @param record 实例事件记录。
     * @return 满足返回 true。
     */
    bool acceptsRecord(const EvaluationInstanceRecord &record) const;

signals:
    /**
     * @brief 过滤条件变更信号。
     */
    void filterChanged();
    /** @brief 排序方式变更信号。 */
    void sortModeChanged();

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override;
    bool lessThan(const QModelIndex &source_left, const QModelIndex &source_right) const override;

private:
    SortMode     sort_mode_{SortNone};                              ///< 当前实例列表排序方式。
    QString      status_;                                              ///< 状态过滤字符串。
    QString      matrix_row_;                                          ///< 选中的混淆矩阵行标识。
    QString      matrix_column_;                                       ///< 选中的混淆矩阵列标识。
    QVariantList pred_class_ids_;                                      ///< 预测类别 ID 集合。
    double       min_score_{-std::numeric_limits<double>::infinity()}; ///< 最低分数限制。
    double       max_score_{std::numeric_limits<double>::infinity()};  ///< 最高分数限制。
};

/**
 * @brief 评估图表描述符列表模型。
 *
 * 管理从评估 Service 产出的所有图表描述符（PR 曲线、分数分布直方图、ROC 曲线等）。
 */
class MODEL_API EvaluationChartModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(EvaluationChartModel)
    QML_UNCREATABLE("EvaluationChartModel is owned by ModelEvaluationViewModel")
public:
    /**
     * @brief 图表数据角色枚举。
     */
    enum Role
    {
        KindRole = Qt::UserRole + 1, ///< 图表种类（pr_curve, score_distribution 等）。
        TitleRole,                   ///< 图表标题。
        DataRole,                    ///< 图表数据结构（系列、坐标点）。
        OptionsRole,                 ///< 图表显示配置选项。
    };
    Q_ENUM(Role)

    explicit EvaluationChartModel(QObject *parent = nullptr);
    int                    rowCount(const QModelIndex &parent = {}) const override;
    QVariant               data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /**
     * @brief 设置图表描述符列表。
     * @param records 图表字典列表。
     */
    void setRecords(QList<QVariantMap> records);

    /**
     * @brief 获取所有图表描述符。
     * @return 图表字典列表常引用。
     */
    const QList<QVariantMap> &records() const;

    /**
     * @brief 获取指定行的图表描述符字典（供 QML 调用）。
     * @param row 行索引。
     * @return 图表描述符 QVariantMap。
     */
    Q_INVOKABLE QVariantMap descriptor(int row) const;

private:
    QList<QVariantMap> records_; ///< 图表描述符列表。
};

} // namespace dltool::model
