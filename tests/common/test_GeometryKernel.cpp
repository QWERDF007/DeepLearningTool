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

    void rectangleToPolygonRejectsDegenerateOrInvertedEmpty()
    {
        // Zero width
        QVERIFY(dltool::common::geometry::rectangleToPolygon(QPointF(5.0, 5.0), QPointF(5.0, 8.0)).empty());
        // Zero height
        QVERIFY(dltool::common::geometry::rectangleToPolygon(QPointF(5.0, 5.0), QPointF(8.0, 5.0)).empty());
        // Inverted points should be normalized to canonical corners
        const auto normalized
            = dltool::common::geometry::rectangleToPolygon(QPointF(10.0, 20.0), QPointF(4.0, 8.0));
        QCOMPARE(normalized.size(), size_t(4));
        comparePoint(normalized.at(0), QPointF(4.0, 8.0));
        comparePoint(normalized.at(1), QPointF(10.0, 8.0));
        comparePoint(normalized.at(2), QPointF(10.0, 20.0));
        comparePoint(normalized.at(3), QPointF(4.0, 20.0));
    }

    void clipPolygonPreservesValidSmallAreaAndClipsBoundary()
    {
        // Out-of-bounds polygon clipped to boundary
        const std::vector<QPointF> unclipped{{-5.0, -5.0}, {15.0, -5.0}, {15.0, 15.0}, {-5.0, 15.0}};
        const std::vector<QPointF> clipped
            = dltool::common::geometry::clipPolygon(unclipped, QRectF(0.0, 0.0, 10.0, 10.0));
        QCOMPARE(clipped.size(), size_t(4));
        comparePoint(clipped.at(0), QPointF(0.0, 10.0));
        comparePoint(clipped.at(1), QPointF(0.0, 0.0));
        comparePoint(clipped.at(2), QPointF(10.0, 0.0));
        comparePoint(clipped.at(3), QPointF(10.0, 10.0));
        QCOMPARE(dltool::common::polygonArea(clipped), 100.0);

        // Valid small subpixel region is kept (not discarded by area)
        const std::vector<QPointF> small_poly{{2.0, 3.0}, {2.5, 3.0}, {2.5, 3.5}, {2.0, 3.5}};
        const std::vector<QPointF> small_clipped
            = dltool::common::geometry::clipPolygon(small_poly, QRectF(0.0, 0.0, 10.0, 10.0));
        QCOMPARE(small_clipped.size(), size_t(4));
        QVERIFY(dltool::common::polygonArea(small_clipped) > 0.0);
        QVERIFY(std::abs(dltool::common::polygonArea(small_clipped) - 0.25) < 1e-9);

        // Collinear / degenerate points produce empty
        const std::vector<QPointF> line_poly{{1.0, 1.0}, {2.0, 2.0}, {3.0, 3.0}};
        QVERIFY(dltool::common::geometry::clipPolygon(line_poly, QRectF(0.0, 0.0, 10.0, 10.0)).empty());
    }

    void polygonBoundsReturnsExactBoundingBox()
    {
        const std::vector<QPointF> poly{{2.5, 3.5}, {7.0, 1.0}, {9.5, 8.0}, {4.0, 11.5}};
        const QRectF bounds = dltool::common::geometry::polygonBounds(poly);
        QCOMPARE(bounds.left(), 2.5);
        QCOMPARE(bounds.top(), 1.0);
        QCOMPARE(bounds.right(), 9.5);
        QCOMPARE(bounds.bottom(), 11.5);
        QCOMPARE(bounds.width(), 7.0);
        QCOMPARE(bounds.height(), 10.5);
    }

    void maskToPolygonsHandlesSinglePixelAndLineFallback()
    {
        const int width  = 8;
        const int height = 8;

        // Single pixel at (3, 4)
        std::vector<uint8_t> single_pixel_mask(static_cast<size_t>(width * height), 0);
        single_pixel_mask[4 * width + 3] = 1;
        const auto single_polygons = dltool::common::maskToPolygons(single_pixel_mask, width, height, false);
        QCOMPARE(single_polygons.size(), size_t(1));
        const auto &p1 = single_polygons.front();
        QCOMPARE(p1.size(), size_t(4));
        const QRectF b1 = dltool::common::geometry::polygonBounds(p1);
        QCOMPARE(b1.x(), 3.0);
        QCOMPARE(b1.y(), 4.0);
        QCOMPARE(b1.width(), 1.0);
        QCOMPARE(b1.height(), 1.0);
        QCOMPARE(dltool::common::polygonArea(p1), 1.0);

        // Horizontal 2-pixel line at (2, 2) and (3, 2)
        std::vector<uint8_t> line_mask(static_cast<size_t>(width * height), 0);
        line_mask[2 * width + 2] = 1;
        line_mask[2 * width + 3] = 1;
        const auto line_polygons = dltool::common::maskToPolygons(line_mask, width, height, false);
        QCOMPARE(line_polygons.size(), size_t(1));
        const auto &p2 = line_polygons.front();
        QCOMPARE(p2.size(), size_t(4));
        const QRectF b2 = dltool::common::geometry::polygonBounds(p2);
        QCOMPARE(b2.x(), 2.0);
        QCOMPARE(b2.y(), 2.0);
        QCOMPARE(b2.width(), 2.0);
        QCOMPARE(b2.height(), 1.0);
        QCOMPARE(dltool::common::polygonArea(p2), 2.0);
    }

    void maskWithHoleExtractsOuterBoundaryAndFillsHoleWhenRasterized()
    {
        // 8x8 mask with outer 6x6 square [1..6, 1..6] and inner 2x2 hole [3..4, 3..4]
        const int width  = 8;
        const int height = 8;
        std::vector<uint8_t> mask(static_cast<size_t>(width * height), 0);
        for (int y = 1; y <= 6; ++y)
        {
            for (int x = 1; x <= 6; ++x)
            {
                if (x >= 3 && x <= 4 && y >= 3 && y <= 4)
                    continue; // hole
                mask[y * width + x] = 1;
            }
        }

        // maskToPolygons uses RETR_EXTERNAL: only the outer boundary is extracted
        const auto polygons = dltool::common::maskToPolygons(mask, width, height, false);
        QCOMPARE(polygons.size(), size_t(1));
        const QRectF outer_bounds = dltool::common::geometry::polygonBounds(polygons.front());
        QVERIFY(outer_bounds.left() <= 1.0);
        QVERIFY(outer_bounds.top() <= 1.0);
        QVERIFY(outer_bounds.right() >= 6.0);
        QVERIFY(outer_bounds.bottom() >= 6.0);

        // Rasterizing back to mask via polygons2Mask fills the hole (polygon format cannot express holes)
        const std::vector<uint8_t> re_rasterized = dltool::common::polygons2Mask(polygons, width, height, 1);
        QCOMPARE(re_rasterized.size(), static_cast<size_t>(width * height));
        // The hole pixels are now filled (value == 1)
        QCOMPARE(re_rasterized[3 * width + 3], uint8_t{1});
        QCOMPARE(re_rasterized[3 * width + 4], uint8_t{1});
        QCOMPARE(re_rasterized[4 * width + 3], uint8_t{1});
        QCOMPARE(re_rasterized[4 * width + 4], uint8_t{1});
    }

    void polygons2MaskRasterizesPixelsCorrectly()
    {
        const int width  = 8;
        const int height = 8;
        // Rectangle from (1, 2) to (4, 5) -> covers pixels [1, 3] x [2, 4]
        const std::vector<QPointF> rect{{1.0, 2.0}, {4.0, 2.0}, {4.0, 5.0}, {1.0, 5.0}};
        const std::vector<uint8_t> mask = dltool::common::polygons2Mask({rect}, width, height, 255);

        QCOMPARE(mask.size(), static_cast<size_t>(width * height));
        // Check pixel (0, 0) is background
        QCOMPARE(mask[0 * width + 0], uint8_t{0});
        // Check inside pixel (2, 3) is foreground
        QCOMPARE(mask[3 * width + 2], uint8_t{255});
        // Check boundary pixels
        QCOMPARE(mask[2 * width + 1], uint8_t{255});
        QCOMPARE(mask[4 * width + 3], uint8_t{255});
        // Check outside pixel (5, 5) is background
        QCOMPARE(mask[5 * width + 5], uint8_t{0});
    }
};

QTEST_GUILESS_MAIN(GeometryKernelTest)

#include "test_GeometryKernel.moc"
