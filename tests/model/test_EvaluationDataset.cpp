#include "../test_runner.h"

#include "model/EvaluationDataset.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace dltool::model;

class EvaluationDatasetTest : public QObject
{
    Q_OBJECT

private slots:
    void readsCsvFileList()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString path = QDir(temp.path()).filePath(QStringLiteral("test.txt"));
        QFile       file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("image_id,image_path\n1,F:/images/a.png\n2,F:/images/b.png\n");
        file.close();

        QList<QPair<qint64, QString>> rows;
        QString                      error;
        QVERIFY(readEvaluationImageList(path, rows, {}, &error));
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).first, 1);
        QCOMPARE(rows.at(0).second, QStringLiteral("F:/images/a.png"));
        QCOMPARE(rows.at(1).first, 2);
        QCOMPARE(rows.at(1).second, QStringLiteral("F:/images/b.png"));
    }

    void missingFileFails()
    {
        QList<QPair<qint64, QString>> rows;
        QString                      error;
        QVERIFY(!readEvaluationImageList(QStringLiteral("F:/no/such/file.txt"), rows, {}, &error));
        QVERIFY(!error.isEmpty());
    }
};

REGISTER_TEST(EvaluationDatasetTest)

#include "test_EvaluationDataset.moc"
