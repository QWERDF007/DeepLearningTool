#pragma once

#include "dltool/model/Export.h"
#include "model/ModelEvaluationProtocol.h"

#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QVariantMap>
#include <QStringList>
#include <QPointer>
#include <QtQml>
#include <vector>
#include <QVector>
#include <limits>

namespace dltool::model {

struct MODEL_API EvaluationMetricRecord
{
    QString key;
    QString label;
    QString class_name;
    int class_id{-1};
    double precision{0.0};
    double recall{0.0};
    double f1{0.0};
    bool precision_defined{false};
    bool recall_defined{false};
    bool f1_defined{false};
    qint64 tp{0};
    qint64 fp{0};
    qint64 fn{0};
};

struct MODEL_API EvaluationConfusionCell
{
    QString row_key;
    QString column_key;
    QString row_label;
    QString column_label;
    qint64 count{0};
    int row_class_id{-1};
    int column_class_id{-1};
    evaluation::CellKind cell_kind{evaluation::CellKind::NotApplicable};
    QString tooltip;
    bool selectable{true};
    bool diagonal{false};
    bool error{false};
};

struct MODEL_API EvaluationInstanceRecord
{
    QString event_uuid;
    qint64 image_id{-1};
    qint64 dataset_id{-1};
    QString image_name;
    QString image_path;
    int image_width{0};
    int image_height{0};
    evaluation::Status status{evaluation::Status::Unknown};
    QString gt_class;
    QString pred_class;
    int gt_class_id{-1};
    int pred_class_id{-1};
    double score{0.0};
    double iou{0.0};
    QVariantMap gt_geometry;
    QVariantMap pred_geometry;
    QVariantMap gt_bounds;
    QVariantMap pred_bounds;
    QVariantMap crop_bounds;
    QVariantMap gt_overlay_bounds;
    QVariantMap pred_overlay_bounds;
    QVariantList gt_overlay_points;
    QVariantList pred_overlay_points;
    QString gt_mask_url;
    QString pred_mask_url;
    qint64 gt_label_id{-1};
    QString gt_instance_id;
    QString pred_instance_id;
    QString gt_class_color;
    QString pred_class_color;
    QString thumbnail_url;
    bool selected{false};
};

struct MODEL_API EvaluationGroundTruthRecord
{
    qint64 label_id{-1};
    int class_id{-1};
    QString class_name;
    QVariantMap geometry;
};

struct MODEL_API EvaluationPredictionRecord
{
    QString prediction_id;
    int class_id{-1};
    QString class_name;
    double score{0.0};
    QVariantMap geometry;
};

/**
 * @brief 图像级评估记录。
 *
 * 该记录以 pred/images.txt 为全集，不从实例事件反推，因此没有任何 GT
 * 或预测实例的真负图像也会进入图像指标和图像级图表。
 */
struct MODEL_API EvaluationImageRecord
{
    qint64 image_id{-1};
    qint64 dataset_id{-1};
    QString image_name;
    QString image_path;
    int image_width{0};
    int image_height{0};
    QList<EvaluationGroundTruthRecord> gt_instances;
    QList<EvaluationPredictionRecord> predictions;

    // Derived image-level values are part of the in-memory report store.  The
    // QML model can therefore answer common roles without rebuilding lists or
    // scanning every prediction on each delegate/filter request.
    QList<qint64> gt_label_ids;
    QList<int> gt_class_ids;
    QList<int> pred_class_ids;
    double max_prediction_score{0.0};
    bool has_gt{false};
    bool has_pred{false};
};

class MODEL_API EvaluationMetricModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(EvaluationMetricModel)
    QML_UNCREATABLE("EvaluationMetricModel is owned by ModelEvaluationViewModel")
public:
    enum Role
    {
        KeyRole = Qt::UserRole + 1,
        LabelRole,
        ClassNameRole,
        ClassIdRole,
        PrecisionRole,
        RecallRole,
        F1Role,
        PrecisionTextRole,
        RecallTextRole,
        F1TextRole,
        TpRole,
        FpRole,
        FnRole,
    };
    Q_ENUM(Role)

    explicit EvaluationMetricModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setRecords(std::vector<EvaluationMetricRecord> records);
    const std::vector<EvaluationMetricRecord> &records() const;

private:
    std::vector<EvaluationMetricRecord> records_;
};

class MODEL_API EvaluationMetricSortProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(EvaluationMetricSortProxyModel)
    QML_UNCREATABLE("EvaluationMetricSortProxyModel is owned by ModelEvaluationViewModel")
public:
    explicit EvaluationMetricSortProxyModel(QObject *parent = nullptr);
    Q_INVOKABLE void sortBy(const QString &field);
};

class MODEL_API EvaluationConfusionModel : public QAbstractTableModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(EvaluationConfusionModel)
    QML_UNCREATABLE("EvaluationConfusionModel is owned by ModelEvaluationViewModel")
public:
    enum Role
    {
        RowKeyRole = Qt::UserRole + 1,
        ColumnKeyRole,
        RowLabelRole,
        ColumnLabelRole,
        CountRole,
        RowClassIdRole,
        ColumnClassIdRole,
        CellKindRole,
        CellKindValueRole,
        SelectableRole,
        IsDiagonalRole,
        IsErrorRole,
        TooltipRole,
    };
    Q_ENUM(Role)

    enum CellKindValue
    {
        CellKindMatch = static_cast<int>(evaluation::CellKind::Match),
        CellKindClassMismatch = static_cast<int>(evaluation::CellKind::ClassMismatch),
        CellKindFalsePositive = static_cast<int>(evaluation::CellKind::FalsePositive),
        CellKindFalseNegative = static_cast<int>(evaluation::CellKind::FalseNegative),
        CellKindPredTotal = static_cast<int>(evaluation::CellKind::PredTotal),
        CellKindGtTotal = static_cast<int>(evaluation::CellKind::GtTotal),
        CellKindFalsePositiveTotal = static_cast<int>(evaluation::CellKind::FalsePositiveTotal),
        CellKindFalseNegativeTotal = static_cast<int>(evaluation::CellKind::FalseNegativeTotal),
        CellKindAll = static_cast<int>(evaluation::CellKind::All),
        CellKindNotApplicable = static_cast<int>(evaluation::CellKind::NotApplicable),
    };
    Q_ENUM(CellKindValue)

    explicit EvaluationConfusionModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setRecords(std::vector<EvaluationConfusionCell> records);
    const std::vector<EvaluationConfusionCell> &records() const;

private:
    std::vector<EvaluationConfusionCell> records_;
    int dimension_{0};
};

class MODEL_API EvaluationInstanceModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(EvaluationInstanceModel)
    QML_UNCREATABLE("EvaluationInstanceModel is owned by ModelEvaluationViewModel")
public:
    enum Role
    {
        EventUuidRole = Qt::UserRole + 1,
        ImageIdRole,
        DatasetIdRole,
        ImageNameRole,
        ImagePathRole,
        ImageWidthRole,
        ImageHeightRole,
        StatusRole,
        StatusKindRole,
        StatusTextRole,
        GtClassRole,
        GtClassNameRole,
        PredClassRole,
        PredClassNameRole,
        GtClassIdRole,
        PredClassIdRole,
        ScoreRole,
        PredScoreRole,
        IouRole,
        GtGeometryRole,
        PredGeometryRole,
        GtBoundsRole,
        PredBoundsRole,
        CropBoundsRole,
        GtOverlayBoundsRole,
        PredOverlayBoundsRole,
        GtOverlayPointsRole,
        PredOverlayPointsRole,
        GtMaskUrlRole,
        PredMaskUrlRole,
        GtLabelIdRole,
        GtInstanceIdRole,
        PredInstanceIdRole,
        GtClassColorRole,
        PredClassColorRole,
        ThumbnailUrlRole,
        SelectedRole,
    };
    Q_ENUM(Role)

    enum StatusValue
    {
        StatusUnknown = static_cast<int>(evaluation::Status::Unknown),
        StatusTruePositive = static_cast<int>(evaluation::Status::TruePositive),
        StatusTrueNegative = static_cast<int>(evaluation::Status::TrueNegative),
        StatusClassMismatch = static_cast<int>(evaluation::Status::ClassMismatch),
        StatusFalsePositive = static_cast<int>(evaluation::Status::FalsePositive),
        StatusFalseNegative = static_cast<int>(evaluation::Status::FalseNegative),
        StatusIgnored = static_cast<int>(evaluation::Status::Ignored),
    };
    Q_ENUM(StatusValue)

    explicit EvaluationInstanceModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setRecords(std::vector<EvaluationInstanceRecord> records);
    void setSelectedEvent(const QString &eventUuid);
    const std::vector<EvaluationInstanceRecord> &records() const;
    const EvaluationInstanceRecord *recordAt(int row) const;

private:
    std::vector<EvaluationInstanceRecord> records_;
};

class MODEL_API EvaluationImageModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(EvaluationImageModel)
    QML_UNCREATABLE("EvaluationImageModel is owned by ModelEvaluationViewModel")
public:
    enum Role
    {
        ImageIdRole = Qt::UserRole + 1,
        DatasetIdRole,
        ImageNameRole,
        ImagePathRole,
        ImageWidthRole,
        ImageHeightRole,
        GtLabelIdsRole,
        GtClassIdsRole,
        PredClassIdsRole,
        ScoreRole,
        HasGtRole,
        HasPredRole,
    };
    Q_ENUM(Role)

    explicit EvaluationImageModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setRecords(std::vector<EvaluationImageRecord> records);
    const std::vector<EvaluationImageRecord> &records() const;
    const EvaluationImageRecord *recordAt(int row) const;

private:
    std::vector<EvaluationImageRecord> records_;
};

class MODEL_API EvaluationImageFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(EvaluationImageFilterProxyModel)
    QML_UNCREATABLE("EvaluationImageFilterProxyModel is owned by ModelEvaluationViewModel")
public:
    explicit EvaluationImageFilterProxyModel(QObject *parent = nullptr);
    void setGlobalFilter(QObject *filter);
    bool acceptsRecord(const EvaluationImageRecord &record) const;

signals:
    void filterChanged();

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override;

private slots:
    void onExternalFilterChanged();

private:
    QPointer<QObject> global_filter_;
};

class MODEL_API EvaluationGlobalFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(EvaluationGlobalFilterProxyModel)
    QML_UNCREATABLE("EvaluationGlobalFilterProxyModel is owned by ModelEvaluationViewModel")
    Q_PROPERTY(QVariantList datasetIds READ datasetIds WRITE setDatasetIds NOTIFY filterChanged FINAL)
    Q_PROPERTY(QVariantList classIds READ classIds WRITE setClassIds NOTIFY filterChanged FINAL)
public:
    explicit EvaluationGlobalFilterProxyModel(QObject *parent = nullptr);
    QVariantList datasetIds() const;
    void setDatasetIds(const QVariantList &ids);
    QVariantList classIds() const;
    void setClassIds(const QVariantList &ids);
    void setGlobalFilter(QObject *filter);
    bool acceptsRecord(const EvaluationInstanceRecord &record) const;

signals:
    void filterChanged();

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override;

private slots:
    void onExternalFilterChanged();

private:
    bool acceptsGlobalLabel(const EvaluationInstanceRecord &record) const;
    QVariantList dataset_ids_;
    QVariantList class_ids_;
    QPointer<QObject> global_filter_;
};

class MODEL_API EvaluationCellFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(EvaluationCellFilterProxyModel)
    QML_UNCREATABLE("EvaluationCellFilterProxyModel is owned by ModelEvaluationViewModel")
    Q_PROPERTY(QString status READ status WRITE setStatus NOTIFY filterChanged FINAL)
    Q_PROPERTY(QString matrixRow READ matrixRow WRITE setMatrixRow NOTIFY filterChanged FINAL)
    Q_PROPERTY(QString matrixColumn READ matrixColumn WRITE setMatrixColumn NOTIFY filterChanged FINAL)
    Q_PROPERTY(QVariantList predClassIds READ predClassIds WRITE setPredClassIds NOTIFY filterChanged FINAL)
    Q_PROPERTY(double minScore READ minScore WRITE setMinScore NOTIFY filterChanged FINAL)
    Q_PROPERTY(double maxScore READ maxScore WRITE setMaxScore NOTIFY filterChanged FINAL)
public:
    explicit EvaluationCellFilterProxyModel(QObject *parent = nullptr);
    QString status() const;
    void setStatus(const QString &status);
    QString matrixRow() const;
    void setMatrixRow(const QString &value);
    QString matrixColumn() const;
    void setMatrixColumn(const QString &value);
    QVariantList predClassIds() const;
    void setPredClassIds(const QVariantList &ids);
    double minScore() const;
    void setMinScore(double value);
    double maxScore() const;
    void setMaxScore(double value);
    bool acceptsRecord(const EvaluationInstanceRecord &record) const;

signals:
    void filterChanged();

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override;

private:
    QString status_;
    QString matrix_row_;
    QString matrix_column_;
    QVariantList pred_class_ids_;
    double min_score_{-std::numeric_limits<double>::infinity()};
    double max_score_{std::numeric_limits<double>::infinity()};
};

class MODEL_API EvaluationChartModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(EvaluationChartModel)
    QML_UNCREATABLE("EvaluationChartModel is owned by ModelEvaluationViewModel")
public:
    enum Role
    {
        KindRole = Qt::UserRole + 1,
        TitleRole,
        DataRole,
        OptionsRole,
    };
    Q_ENUM(Role)
    explicit EvaluationChartModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setRecords(QList<QVariantMap> records);
    const QList<QVariantMap> &records() const;
    Q_INVOKABLE QVariantMap descriptor(int row) const;

private:
    QList<QVariantMap> records_;
};

} // namespace dltool::model
