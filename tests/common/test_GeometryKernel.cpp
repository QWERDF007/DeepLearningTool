#include "common/GeometryKernel.h"
#include "common/MaskPolygonUtils.h"

#include <QTest>

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

void comparePoint(const QPointF &actual, const QPointF &expected)
{
    QVERIFY(std::abs(actual.x() - expected.x()) < 1e-9);
    QVERIFY(std::abs(actual.y() - expected.y()) < 1e-9);
}

} // namespace

class GeometryKernelTest final : public QObject
{
    Q_OBJECT

private slots:
    void rectangleUsesCanonicalCorners()
    {
        const std::vector<QPointF> polygon
            = dltool::common::geometry::rectangleToPolygon(QPointF(8.0, 9.0), QPointF(2.0, 3.0));

        QCOMPARE(polygon.size(), size_t(4));
        comparePoint(polygon.at(0), QPointF(2.0, 3.0));
        comparePoint(polygon.at(1), QPointF(8.0, 3.0));
        comparePoint(polygon.at(2), QPointF(8.0, 9.0));
        comparePoint(polygon.at(3), QPointF(2.0, 9.0));
    }

    void mapsAndClipsPolygonToTargetPixels()
    {
        const std::vector<QPointF> polygon{{-10.0, -5.0}, {60.0, 10.0}, {120.0, 10.0},
                                           {120.0, 40.0}, {-10.0, 40.0}};
        const std::vector<QPointF> mapped
            = dltool::common::geometry::mapPolygon(polygon, QSize(100, 50), QSize(200, 100));

        QCOMPARE(mapped.size(), size_t(6));
        comparePoint(mapped.at(0), QPointF(0.0, 0.0));
        comparePoint(mapped.at(1), QPointF(80.0 / 3.0, 0.0));
        comparePoint(mapped.at(2), QPointF(120.0, 20.0));
        comparePoint(mapped.at(3), QPointF(200.0, 20.0));
        comparePoint(mapped.at(4), QPointF(200.0, 80.0));
        comparePoint(mapped.at(5), QPointF(0.0, 80.0));
    }

    void keepsEveryConnectedMaskRegionIncludingSmallRegion()
    {
        const int width  = 8;
        const int height = 8;
        std::vector<uint8_t> mask(static_cast<size_t>(width * height), 0);
        mask[1 * width + 1] = 1;
        mask[5 * width + 4] = 1;
        mask[5 * width + 5] = 1;
        mask[6 * width + 4] = 1;
        mask[6 * width + 5] = 1;

        const std::vector<std::vector<QPointF>> polygons
            = dltool::common::maskToPolygons(mask, width, height, false);

        std::vector<uint8_t> single_mask(static_cast<size_t>(width * height), 0);
        single_mask[1 * width + 1] = 1;
        const auto single_polygon = dltool::common::maskToPolygons(single_mask, width, height, false);
        QCOMPARE(polygons.size(), size_t(2));
        QCOMPARE(single_polygon.size(), size_t(1));
        QVERIFY(dltool::common::polygonArea(polygons.at(0)) > 0.0);
        QVERIFY(dltool::common::polygonArea(polygons.at(1)) > 0.0);
    }
};

QTEST_GUILESS_MAIN(GeometryKernelTest)

#include "test_GeometryKernel.moc"
