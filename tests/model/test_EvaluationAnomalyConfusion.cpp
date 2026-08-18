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
            {10,    QStringLiteral("Good"), false, false},
            {20, QStringLiteral("Scratch"),  true,  true},
            {10,    QStringLiteral("Good"), false,  true},
            {20, QStringLiteral("Scratch"),  true, false},
        };
        const auto cells = buildAnomalyConfusionCells(samples, {
                                                                   {10,    QStringLiteral("Good")},
                                                                   {20, QStringLiteral("Scratch")},
                                                                   {30,  QStringLiteral("Unused")}
        });
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

    void strictlyFollowsClassCatalogWithoutSyntheticFallback()
    {
        // 类别目录中只有 1: "good" 和 2: "scratch"，没有 0。
        const QMap<int, QString> catalog{
            {1,    QStringLiteral("good")},
            {2, QStringLiteral("scratch")}
        };
        const QList<AnomalyConfusionSample> samples{
            { 1,    QStringLiteral("good"), false, false},
            { 2, QStringLiteral("scratch"),  true,  true},
            {-1,                 QString(), false, false}
        };
        const auto cells = buildAnomalyConfusionCells(samples, catalog);
        // 列数: catalog.size() (2) + FP (1) + TOTAL (1) = 4 列
        // 行数: 0, 1, FN, TOTAL = 4 行 -> 总计 16 个单元格
        QCOMPARE(cells.size(), 16);

        for (const auto &cell : cells)
        {
            // 绝不能出现 column_key 为 "0" 或 column_label 为 "正常" 的伪列
            QVERIFY(cell.column_key != QStringLiteral("0"));
        }
    }
};

REGISTER_TEST(EvaluationAnomalyConfusionTest)

#include "test_EvaluationAnomalyConfusion.moc"
