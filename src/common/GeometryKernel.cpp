#include "common/GeometryKernel.h"

#include <algorithm>
#include <cmath>

namespace dltool::common::geometry {

namespace {

constexpr double kEpsilon = 1e-9;

bool finitePoint(const QPointF &point)
{
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

double signedArea(const std::vector<QPointF> &polygon)
{
    if (polygon.size() < 3)
        return 0.0;

    double area = 0.0;
    for (size_t index = 0; index < polygon.size(); ++index)
    {
        const QPointF &current = polygon[index];
        const QPointF &next    = polygon[(index + 1) % polygon.size()];
        area += current.x() * next.y() - next.x() * current.y();
    }
    return area / 2.0;
}

bool samePoint(const QPointF &left, const QPointF &right)
{
    return std::abs(left.x() - right.x()) <= kEpsilon && std::abs(left.y() - right.y()) <= kEpsilon;
}

std::vector<QPointF> removeDuplicatePoints(std::vector<QPointF> points)
{
    std::vector<QPointF> result;
    result.reserve(points.size());
    for (const QPointF &point : points)
    {
        if (result.empty() || !samePoint(result.back(), point))
            result.push_back(point);
    }
    if (result.size() > 1 && samePoint(result.front(), result.back()))
        result.pop_back();
    return result;
}

template <typename Inside, typename Intersection>
std::vector<QPointF> clipAgainst(const std::vector<QPointF> &polygon, Inside inside, Intersection intersection)
{
    std::vector<QPointF> result;
    if (polygon.empty())
        return result;

    QPointF previous = polygon.back();
    bool    previous_inside = inside(previous);
    for (const QPointF &current : polygon)
    {
        const bool current_inside = inside(current);
        if (current_inside != previous_inside)
            result.push_back(intersection(previous, current));
        if (current_inside)
            result.push_back(current);
        previous        = current;
        previous_inside = current_inside;
    }
    return removeDuplicatePoints(std::move(result));
}

} // namespace

std::vector<QPointF> rectangleToPolygon(const QPointF &first, const QPointF &second)
{
    if (!finitePoint(first) || !finitePoint(second))
        return {};

    const double left   = std::min(first.x(), second.x());
    const double top    = std::min(first.y(), second.y());
    const double right  = std::max(first.x(), second.x());
    const double bottom = std::max(first.y(), second.y());
    if (right <= left || bottom <= top)
        return {};

    return {QPointF(left, top), QPointF(right, top), QPointF(right, bottom), QPointF(left, bottom)};
}

std::vector<QPointF> clipPolygon(const std::vector<QPointF> &polygon, const QRectF &bounds)
{
    if (polygon.size() < 3 || !bounds.isValid() || bounds.width() <= 0.0 || bounds.height() <= 0.0)
        return {};
    for (const QPointF &point : polygon)
    {
        if (!finitePoint(point))
            return {};
    }

    std::vector<QPointF> clipped = polygon;
    const double         left    = bounds.left();
    const double         top     = bounds.top();
    const double         right   = bounds.right();
    const double         bottom  = bounds.bottom();

    clipped = clipAgainst(
        clipped, [left](const QPointF &point) { return point.x() >= left - kEpsilon; },
        [left](const QPointF &from, const QPointF &to)
        {
            const double denominator = to.x() - from.x();
            if (std::abs(denominator) <= kEpsilon)
                return QPointF(left, from.y());
            const double t = (left - from.x()) / denominator;
            return QPointF(left, from.y() + t * (to.y() - from.y()));
        });
    clipped = clipAgainst(
        clipped, [right](const QPointF &point) { return point.x() <= right + kEpsilon; },
        [right](const QPointF &from, const QPointF &to)
        {
            const double denominator = to.x() - from.x();
            if (std::abs(denominator) <= kEpsilon)
                return QPointF(right, from.y());
            const double t = (right - from.x()) / denominator;
            return QPointF(right, from.y() + t * (to.y() - from.y()));
        });
    clipped = clipAgainst(
        clipped, [top](const QPointF &point) { return point.y() >= top - kEpsilon; },
        [top](const QPointF &from, const QPointF &to)
        {
            const double denominator = to.y() - from.y();
            if (std::abs(denominator) <= kEpsilon)
                return QPointF(from.x(), top);
            const double t = (top - from.y()) / denominator;
            return QPointF(from.x() + t * (to.x() - from.x()), top);
        });
    clipped = clipAgainst(
        clipped, [bottom](const QPointF &point) { return point.y() <= bottom + kEpsilon; },
        [bottom](const QPointF &from, const QPointF &to)
        {
            const double denominator = to.y() - from.y();
            if (std::abs(denominator) <= kEpsilon)
                return QPointF(from.x(), bottom);
            const double t = (bottom - from.y()) / denominator;
            return QPointF(from.x() + t * (to.x() - from.x()), bottom);
        });

    clipped = removeDuplicatePoints(std::move(clipped));
    if (clipped.size() < 3 || std::abs(signedArea(clipped)) <= kEpsilon)
        return {};
    return clipped;
}

std::vector<QPointF> mapPolygon(const std::vector<QPointF> &polygon, const QSize &source_size,
                                const QSize &target_size)
{
    if (!source_size.isValid() || source_size.isEmpty() || !target_size.isValid() || target_size.isEmpty())
        return {};

    const double scale_x = static_cast<double>(target_size.width()) / source_size.width();
    const double scale_y = static_cast<double>(target_size.height()) / source_size.height();
    std::vector<QPointF> scaled;
    scaled.reserve(polygon.size());
    for (const QPointF &point : polygon)
    {
        if (!finitePoint(point))
            return {};
        scaled.emplace_back(point.x() * scale_x, point.y() * scale_y);
    }
    return clipPolygon(scaled, QRectF(0.0, 0.0, target_size.width(), target_size.height()));
}

QRectF polygonBounds(const std::vector<QPointF> &polygon)
{
    if (polygon.empty())
        return {};

    double left   = polygon.front().x();
    double top    = polygon.front().y();
    double right  = left;
    double bottom = top;
    for (const QPointF &point : polygon)
    {
        if (!finitePoint(point))
            return {};
        left   = std::min(left, point.x());
        top    = std::min(top, point.y());
        right  = std::max(right, point.x());
        bottom = std::max(bottom, point.y());
    }
    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

} // namespace dltool::common::geometry
