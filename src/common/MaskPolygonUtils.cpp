#include "common/MaskPolygonUtils.h"

#include <opencv2/imgproc.hpp>

#include <QLineF>
#include <QPointF>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dltool::common {

namespace {

double signedPolygonArea(const std::vector<QPointF> &points)
{
    if (points.size() < 3)
        return 0.0;

    double area = 0.0;
    for (size_t i = 0; i < points.size(); ++i)
    {
        const QPointF &a = points[i];
        const QPointF &b = points[(i + 1) % points.size()];
        area += a.x() * b.y() - b.x() * a.y();
    }
    return area / 2.0;
}

cv::Mat maskToPaddedMat(const std::vector<uint8_t> &mask, int width, int height)
{
    const int64_t total = static_cast<int64_t>(width) * static_cast<int64_t>(height);
    if (width <= 0 || height <= 0 || total <= 0 || mask.size() < static_cast<size_t>(total))
        return {};

    cv::Mat padded(height + 2, width + 2, CV_8UC1, cv::Scalar(0));
    for (int y = 0; y < height; ++y)
    {
        uchar       *dst        = padded.ptr<uchar>(y + 1) + 1;
        const size_t row_offset = static_cast<size_t>(y) * width;
        for (int x = 0; x < width; ++x) dst[x] = mask[row_offset + static_cast<size_t>(x)] != 0 ? uchar{255} : uchar{0};
    }
    return padded;
}

cv::Mat signedDistanceField(const cv::Mat &padded_mask)
{
    cv::Mat inside_distance;
    cv::distanceTransform(padded_mask, inside_distance, cv::DIST_L2, cv::DIST_MASK_PRECISE);

    cv::Mat inverted_mask;
    cv::bitwise_not(padded_mask, inverted_mask);

    cv::Mat outside_distance;
    cv::distanceTransform(inverted_mask, outside_distance, cv::DIST_L2, cv::DIST_MASK_PRECISE);

    cv::Mat signed_distance;
    cv::subtract(inside_distance, outside_distance, signed_distance);
    return signed_distance;
}

QPointF refineContourPoint(const cv::Mat &signed_distance, const cv::Point &point, int width, int height)
{
    const float inside_value = signed_distance.at<float>(point.y, point.x);
    cv::Point   best_outside = point;
    float       best_value   = 0.0F;
    bool        found        = false;

    for (int dy = -1; dy <= 1; ++dy)
    {
        const int y = point.y + dy;
        if (y < 0 || y >= signed_distance.rows)
            continue;
        for (int dx = -1; dx <= 1; ++dx)
        {
            if (dx == 0 && dy == 0)
                continue;

            const int x = point.x + dx;
            if (x < 0 || x >= signed_distance.cols)
                continue;

            const float value = signed_distance.at<float>(y, x);
            if (value <= 0.0F && (!found || value > best_value))
            {
                found        = true;
                best_value   = value;
                best_outside = cv::Point(x, y);
            }
        }
    }

    double x = static_cast<double>(point.x);
    double y = static_cast<double>(point.y);
    if (found && inside_value > best_value)
    {
        const double t = static_cast<double>(inside_value) / static_cast<double>(inside_value - best_value);
        x += t * static_cast<double>(best_outside.x - point.x);
        y += t * static_cast<double>(best_outside.y - point.y);
    }
    else
    {
        const int    left   = std::max(point.x - 1, 0);
        const int    right  = std::min(point.x + 1, signed_distance.cols - 1);
        const int    top    = std::max(point.y - 1, 0);
        const int    bottom = std::min(point.y + 1, signed_distance.rows - 1);
        const double dx
            = 0.5
            * static_cast<double>(signed_distance.at<float>(point.y, right) - signed_distance.at<float>(point.y, left));
        const double dy
            = 0.5
            * static_cast<double>(signed_distance.at<float>(bottom, point.x) - signed_distance.at<float>(top, point.x));
        const double denom = dx * dx + dy * dy;
        if (denom > 0.000001)
        {
            x -= static_cast<double>(inside_value) * dx / denom;
            y -= static_cast<double>(inside_value) * dy / denom;
        }
    }

    return QPointF(std::clamp(x - 0.5, 0.0, static_cast<double>(width)),
                   std::clamp(y - 0.5, 0.0, static_cast<double>(height)));
}

std::vector<QPointF> contourToPolygon(const std::vector<cv::Point> &contour, const cv::Mat &signed_distance, int width,
                                      int height)
{
    std::vector<QPointF> points;
    points.reserve(contour.size());
    for (const cv::Point &point : contour) points.push_back(refineContourPoint(signed_distance, point, width, height));

    points = normalizePolygon(std::move(points));
    if (!points.empty() && signedPolygonArea(points) < 0.0)
        std::reverse(points.begin(), points.end());
    return points;
}

std::vector<cv::Point> approximateContour(const std::vector<cv::Point> &contour, double epsilon_ratio)
{
    if (contour.size() < 3 || !std::isfinite(epsilon_ratio) || epsilon_ratio <= 0.0)
        return contour;

    const double epsilon = epsilon_ratio * cv::arcLength(contour, true);
    if (!std::isfinite(epsilon) || epsilon <= 0.0)
        return contour;

    std::vector<cv::Point> fitted;
    cv::approxPolyDP(contour, fitted, epsilon, true);
    // if (fitted.size() < 3 || std::abs(cv::contourArea(fitted)) <= 0.5)
    //     return contour;
    return fitted;
}

} // namespace

double polygonArea(const std::vector<QPointF> &points)
{
    if (points.size() < 3)
        return 0.0;

    double area = 0.0;
    for (size_t i = 0; i < points.size(); ++i)
    {
        const QPointF &a = points[i];
        const QPointF &b = points[(i + 1) % points.size()];
        area += a.x() * b.y() - b.x() * a.y();
    }
    return std::abs(area) / 2.0;
}

std::vector<QPointF> normalizePolygon(std::vector<QPointF> points)
{
    std::vector<QPointF> normalized;
    normalized.reserve(points.size());
    for (const QPointF &point : points)
    {
        if (!normalized.empty() && QLineF(normalized.back(), point).length() < 0.001)
            continue;
        normalized.push_back(point);
    }

    if (normalized.size() > 1 && QLineF(normalized.front(), normalized.back()).length() < 0.001)
        normalized.pop_back();

    if (normalized.size() < 3 || polygonArea(normalized) <= 0.5)
        return {};
    return normalized;
}

std::vector<uint8_t> polygons2Mask(const std::vector<std::vector<QPointF>> &polygons, int width, int height,
                                   uint8_t foreground_value)
{
    const int64_t total = static_cast<int64_t>(width) * static_cast<int64_t>(height);
    if (width <= 0 || height <= 0 || total <= 0)
        return {};

    cv::Mat mask(height, width, CV_8UC1, cv::Scalar(0));
    if (!polygons.empty() && foreground_value > 0)
    {
        std::vector<std::vector<cv::Point>> cv_polygons;
        cv_polygons.reserve(polygons.size());
        for (const std::vector<QPointF> &polygon : polygons)
        {
            std::vector<QPointF> normalized = normalizePolygon(polygon);
            if (normalized.empty())
                continue;

            std::vector<cv::Point> cv_polygon;
            cv_polygon.reserve(normalized.size());
            for (const QPointF &point : normalized)
            {
                const int x = std::clamp(static_cast<int>(std::lround(point.x())), 0, width - 1);
                const int y = std::clamp(static_cast<int>(std::lround(point.y())), 0, height - 1);
                cv_polygon.emplace_back(x, y);
            }
            if (cv_polygon.size() >= 3)
                cv_polygons.push_back(std::move(cv_polygon));
        }

        if (!cv_polygons.empty())
            cv::fillPoly(mask, cv_polygons, cv::Scalar(foreground_value));
    }

    std::vector<uint8_t> result(static_cast<size_t>(total));
    if (mask.isContinuous())
    {
        std::copy(mask.data, mask.data + total, result.begin());
    }
    else
    {
        for (int y = 0; y < height; ++y)
        {
            const uchar *row = mask.ptr<uchar>(y);
            std::copy(row, row + width, result.begin() + static_cast<size_t>(y) * width);
        }
    }
    return result;
}

std::vector<std::vector<QPointF>> maskToPolygons(const std::vector<uint8_t> &mask, int width, int height, bool keep_max,
                                                 double polygon_approx_epsilon_ratio)
{
    const cv::Mat padded_mask = maskToPaddedMat(mask, width, height);
    if (padded_mask.empty())
        return {};

    const cv::Mat signed_distance = signedDistanceField(padded_mask);
    cv::Mat       foreground;
    cv::compare(signed_distance, 0.0F, foreground, cv::CMP_GT);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(foreground, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (contours.empty())
        return {};

    std::vector<std::vector<QPointF>> polygons;
    polygons.reserve(keep_max ? 1 : contours.size());
    const double epsilon_ratio
        = std::isfinite(polygon_approx_epsilon_ratio) ? std::max(0.0, polygon_approx_epsilon_ratio) : 0.0;

    for (const std::vector<cv::Point> &contour : contours)
    {
        if (std::abs(cv::contourArea(contour)) <= 0.5)
            continue;

        const std::vector<cv::Point> fitted_contour = approximateContour(contour, epsilon_ratio);
        std::vector<QPointF>         points         = contourToPolygon(fitted_contour, signed_distance, width, height);
        if (points.empty())
            continue;

        points = normalizePolygon(std::move(points));
        if (points.empty())
            continue;
        if (signedPolygonArea(points) < 0.0)
            std::reverse(points.begin(), points.end());

        polygons.push_back(std::move(points));
    }

    if (polygons.empty())
        return {};

    std::sort(polygons.begin(), polygons.end(), [](const std::vector<QPointF> &left, const std::vector<QPointF> &right)
              { return polygonArea(left) > polygonArea(right); });

    if (keep_max && polygons.size() > 1)
        polygons.resize(1);

    return polygons;
}

} // namespace dltool::common
