#include "../test_runner.h"

#include "model/EvaluationAnomalyConfusion.h"
#include "model/ModelEvaluationProtocol.h"

#include <QTest>

using namespace dltool::model;

class EvaluationAnomalyConfusionTest : public QObject
{
    Q_OBJECT

private slots:
    void buildsBinaryRowsAndErrorMargins()
    {
        const QList<AnomalyConfusionSample> samples{
            {10, QStringLiteral("Good"), false, false},
            {20, QStringLiteral("Scratch"), true, true},
            {10, QStringLiteral("Good"), false, true},
            {20, QStringLiteral("Scratch"), true, false},
        };
        const auto cells = buildAnomalyConfusionCells(samples, {{10, QStringLiteral("Good")},
                                                                {20, QStringLiteral("Scratch")},
                                                                {30, QStringLiteral("Unused")}});
        QCOMPARE(cells.size(), 20);

        auto findCell = [&cells](const QString &row, const QString &column) -> const EvaluationConfusionCell *
        {
            for (const auto &cell : cells)
                if (cell.row_key == row && cell.column_key == column)
                    return &cell;
            return nullptr;
        };
        const auto *tn = findCell(QStringLiteral("0"), QStringLiteral("10"));
        QVERIFY(tn != nullptr);
        QCOMPARE(tn->count, qint64(1));
        QCOMPARE(tn->cell_kind, evaluation::CellKind::Match);
        const auto *tp = findCell(QStringLiteral("1"), QStringLiteral("20"));
        QVERIFY(tp != nullptr);
        QCOMPARE(tp->count, qint64(1));
        QCOMPARE(tp->cell_kind, evaluation::CellKind::Match);
        const auto *fp = findCell(QStringLiteral("1"), QStringLiteral("FP"));
        QVERIFY(fp != nullptr);
        QCOMPARE(fp->count, qint64(1));
        const auto *fn = findCell(QStringLiteral("FN"), QStringLiteral("20"));
        QVERIFY(fn != nullptr);
        QCOMPARE(fn->count, qint64(1));
        const auto *all = findCell(QStringLiteral("TOTAL"), QStringLiteral("TOTAL"));
        QVERIFY(all != nullptr);
        QCOMPARE(all->count, qint64(4));
        QVERIFY(all->selectable);
    }
};

REGISTER_TEST(EvaluationAnomalyConfusionTest)

#include "test_EvaluationAnomalyConfusion.moc"
