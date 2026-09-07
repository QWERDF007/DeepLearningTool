#include "feature/FeatureManager.h"
#include "feature/ImageSearchController.h"
#include "feature/RoiClusterController.h"
#include "feature/SmartAnnotationController.h"

#include <QTest>

namespace {

class FeatureLifecycleTest : public QObject
{
    Q_OBJECT

private slots:
    void roiClusterShutdownIsIdempotentAndRejectsNewWork()
    {
        dltool::feature::RoiClusterController controller(nullptr, nullptr);

        controller.shutdown();
        controller.shutdown();

        QVERIFY(!controller.isRunning());
        QVERIFY(!controller.cluster({1}));
        QCOMPARE(controller.lastError(), QStringLiteral("标注聚类控制器正在关闭"));
    }

    void featureManagerShutdownPropagatesToChildren()
    {
        dltool::feature::FeatureManager manager(nullptr, nullptr, nullptr, nullptr);

        manager.shutdown();
        manager.shutdown();

        QVERIFY(manager.imageSearch() != nullptr);
        QVERIFY(!manager.imageSearch()->search({1}, {}));
        QCOMPARE(manager.imageSearch()->lastError(), QStringLiteral("图像搜索控制器正在关闭"));

        QVERIFY(manager.roiCluster() != nullptr);
        QVERIFY(!manager.roiCluster()->cluster({1}));
        QCOMPARE(manager.roiCluster()->lastError(), QStringLiteral("标注聚类控制器正在关闭"));

        QVERIFY(manager.smartAnnotation() != nullptr);
        const QVariantMap result = manager.smartAnnotation()->infer({}, {}, {});
        QVERIFY(!result.value(QStringLiteral("success")).toBool());
        QCOMPARE(result.value(QStringLiteral("error")).toString(), QStringLiteral("智能标注控制器正在关闭"));
    }
};

} // namespace

QTEST_GUILESS_MAIN(FeatureLifecycleTest)

#include "test_FeatureLifecycle.moc"
