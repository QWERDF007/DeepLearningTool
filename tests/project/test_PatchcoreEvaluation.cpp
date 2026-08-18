#include "PersistentProjectFixture.h"

#include "model/ModelEvaluationViewModel.h"
#include "model/ModelTestTaskManager.h"
#include "project/Projects.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTest>
#include <QThread>

using namespace dltool::model::integration;

class PatchcoreEvaluationIntegrationTest final : public QObject
{
    Q_OBJECT

private slots:
    void evaluatesPatchcorePrediction()
    {
        PersistentProjectFixture fixture;
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        QString model_uuid;
        QString task_uuid;
        QString error;
        QVERIFY2(fixture.findPatchcoreModel(&model_uuid, &error), qPrintable(error));
        QVERIFY2(fixture.findPatchcoreTask(model_uuid, &task_uuid, &error), qPrintable(error));

        auto *task_manager = fixture.project()->modelTestTaskManager();
        QVERIFY(task_manager != nullptr);
        task_manager->setModelUuid(model_uuid);
        QVERIFY2(task_manager->switchTask(task_uuid), qPrintable(QStringLiteral("无法切换到 PatchCore 测试任务")));

        auto *evaluation = task_manager->currentEvaluation();
        QVERIFY(evaluation != nullptr);
        evaluation->evaluate();

        QElapsedTimer timer;
        timer.start();
        while (evaluation->loading() && timer.elapsed() < 300000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(50);
        }

        QVERIFY2(!evaluation->loading(), qPrintable(QStringLiteral("评估等待超时")));
        QVERIFY2(evaluation->available(), qPrintable(evaluation->error()));
        QVERIFY(evaluation->hasImageMetrics());
    }
};

QTEST_GUILESS_MAIN(PatchcoreEvaluationIntegrationTest)

#include "test_PatchcoreEvaluation.moc"
