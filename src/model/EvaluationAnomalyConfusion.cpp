#include "model/EvaluationAnomalyConfusion.h"

#include "model/ModelEvaluationProtocol.h"

#include <QMap>
#include <utility>

namespace dltool::model {

namespace {

struct GroundTruthCategory
{
    QString name;
    bool    anomaly{false};
};

} // namespace

std::vector<EvaluationConfusionCell>
buildAnomalyConfusionCells(const QList<AnomalyConfusionSample> &samples)
{
    const QString matrix_fn    = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalseNegative);
    const QString matrix_fp    = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::FalsePositive);
    const QString matrix_total = evaluation::matrixAxisKey(evaluation::MatrixAxisKey::Total);
    const QString separator(QLatin1Char('\x1f'));
    QMap<int, GroundTruthCategory> categories;
    QMap<QString, qint64>           counts;
    QMap<int, qint64>               row_totals;
    QMap<int, qint64>               row_errors;
    QMap<int, qint64>               column_totals;
    QMap<int, qint64>               column_errors;
    qint64                          error_total = 0;

    for (const AnomalyConfusionSample &sample : samples)
    {
        categories[sample.category_id] = GroundTruthCategory{sample.category_name, sample.category_anomaly};
        const int row_id = sample.predicted_anomaly ? 1 : 0;
        ++counts[QString::number(row_id) + separator + QString::number(sample.category_id)];
        ++row_totals[row_id];
        ++column_totals[sample.category_id];

        // FP/FN are the prediction-axis and GT-axis error margins. A class
        // mismatch intentionally contributes to both margins.
        if (sample.predicted_anomaly != sample.category_anomaly)
        {
            ++row_errors[row_id];
            ++column_errors[sample.category_id];
            ++error_total;
        }
    }

    std::vector<EvaluationConfusionCell> cells;
    const auto appendCell = [&cells](const QString &row_key, const QString &row_label, const int row_class_id,
                                     const QString &column_key, const QString &column_label,
                                     const int column_class_id, const qint64 count,
                                     const evaluation::CellKind kind, const bool selectable,
                                     const bool diagonal, const bool error)
    {
        EvaluationConfusionCell cell;
        cell.row_key         = row_key;
        cell.column_key      = column_key;
        cell.row_label       = row_label;
        cell.column_label    = column_label;
        cell.row_class_id    = row_class_id;
        cell.column_class_id = column_class_id;
        cell.count           = count;
        cell.cell_kind       = kind;
        cell.selectable      = selectable;
        cell.diagonal        = diagonal;
        cell.error           = error;
        cells.push_back(std::move(cell));
    };

    const QString total_label = QStringLiteral("合计");
    for (const int row_id : {0, 1})
    {
        const QString row_key   = QString::number(row_id);
        const QString row_label = row_id == 0 ? QStringLiteral("GOOD") : QStringLiteral("Anomaly");
        for (auto category = categories.cbegin(); category != categories.cend(); ++category)
        {
            const qint64 count = counts.value(row_key + separator + QString::number(category.key()));
            const bool correct = (row_id == 1) == category.value().anomaly;
            appendCell(row_key, row_label, row_id, QString::number(category.key()), category.value().name,
                       category.key(), count,
                       correct ? evaluation::CellKind::Match : evaluation::CellKind::ClassMismatch,
                       true, correct, !correct);
        }
        appendCell(row_key, row_label, row_id, matrix_fp, matrix_fp, -1, row_errors.value(row_id),
                   evaluation::CellKind::FalsePositive, true, false, true);
        appendCell(row_key, row_label, row_id, matrix_total, total_label, -1, row_totals.value(row_id),
                   evaluation::CellKind::PredTotal, true, false, false);
    }

    for (auto category = categories.cbegin(); category != categories.cend(); ++category)
        appendCell(matrix_fn, matrix_fn, -1, QString::number(category.key()), category.value().name, category.key(),
                   column_errors.value(category.key()), evaluation::CellKind::FalseNegative, true, false, true);
    appendCell(matrix_fn, matrix_fn, -1, matrix_fp, matrix_fp, -1, error_total,
               evaluation::CellKind::NotApplicable, false, false, true);
    appendCell(matrix_fn, matrix_fn, -1, matrix_total, total_label, -1, error_total,
               evaluation::CellKind::FalseNegativeTotal, true, false, true);

    for (auto category = categories.cbegin(); category != categories.cend(); ++category)
        appendCell(matrix_total, total_label, -1, QString::number(category.key()), category.value().name,
                   category.key(), column_totals.value(category.key()), evaluation::CellKind::GtTotal,
                   true, false, false);
    appendCell(matrix_total, total_label, -1, matrix_fp, matrix_fp, -1, error_total,
               evaluation::CellKind::FalsePositiveTotal, true, false, true);
    appendCell(matrix_total, total_label, -1, matrix_total, total_label, -1, samples.size(),
               evaluation::CellKind::All, true, false, false);
    return cells;
}

} // namespace dltool::model
