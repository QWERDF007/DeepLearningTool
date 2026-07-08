#include "data/LabelData.h"

#include "LabelData.h"
#include "core/CoreDef.h"

#include <json.hpp>

#include <QLineF>
#include <algorithm>
#include <cmath>
#include <limits>

using json = nlohmann::json;

namespace dltool::data {

using dltool::core::DeepLearningMethod;

enum EditMode
{
    NoneMode = -1,
    Move     = 0,
    Resize   = 1,
};

enum EditDirection
{
    NoneDir     = -1,
    TopLeft     = 0,
    TopRight    = 1,
    BottomLeft  = 2,
    BottomRight = 3,
    Left        = 4,
    Right       = 5,
    Top         = 6,
    Bottom      = 7,
    Inside      = 8,
};

namespace {

QVariantList pointsToVariantList(const std::vector<QPointF> &points)
{
    QVariantList result;
    result.reserve(static_cast<int>(points.size()));
    for (const QPointF &point : points)
    {
        result.push_back(QVariantMap{
            {"x", point.x()},
            {"y", point.y()},
        });
    }
    return result;
}

std::vector<QPointF> variantListToPoints(const QVariant &value)
{
    std::vector<QPointF> points;
    const QVariantList   list = value.toList();
    points.reserve(static_cast<size_t>(list.size()));

    for (const QVariant &item : list)
    {
        if (item.canConvert<QVariantMap>())
        {
            const QVariantMap map = item.toMap();
            points.emplace_back(map.value("x").toDouble(), map.value("y").toDouble());
        }
        else if (item.canConvert<QVariantList>())
        {
            const QVariantList pair = item.toList();
            if (pair.size() >= 2)
            {
                points.emplace_back(pair[0].toDouble(), pair[1].toDouble());
            }
        }
    }

    return points;
}

QRectF boundingBoxFromPoints(const std::vector<QPointF> &points)
{
    if (points.empty())
    {
        return QRectF();
    }

    double x_min = points.front().x();
    double y_min = points.front().y();
    double x_max = points.front().x();
    double y_max = points.front().y();
    for (const QPointF &point : points)
    {
        x_min = std::min(x_min, point.x());
        y_min = std::min(y_min, point.y());
        x_max = std::max(x_max, point.x());
        y_max = std::max(y_max, point.y());
    }

    return QRectF(x_min, y_min, x_max - x_min, y_max - y_min);
}

void updateBoundingBoxFromPoints(LabelData_t &data, const std::vector<QPointF> &points)
{
    const QRectF bbox = boundingBoxFromPoints(points);
    data.x            = bbox.x();
    data.y            = bbox.y();
    data.width        = bbox.width();
    data.height       = bbox.height();
}

QPointF clampPointToRect(const QPointF &point, const QRectF &rect)
{
    return QPointF(std::clamp(point.x(), rect.left(), rect.right()), std::clamp(point.y(), rect.top(), rect.bottom()));
}

bool isPointNearLabelBounds(const LabelData_t &data, const QPointF &point, double padding = 0.0)
{
    return point.x() >= data.x - padding && point.x() <= data.x + data.width + padding && point.y() >= data.y - padding
        && point.y() <= data.y + data.height + padding;
}

bool isPointInPolygon(const QPointF &point, const std::vector<QPointF> &polygon)
{
    bool inside = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
    {
        const QPointF &a          = polygon[i];
        const QPointF &b          = polygon[j];
        const bool     intersects = (a.y() > point.y()) != (b.y() > point.y());
        if (intersects)
        {
            const double x_intersection = (b.x() - a.x()) * (point.y() - a.y()) / (b.y() - a.y()) + a.x();
            if (point.x() < x_intersection)
            {
                inside = !inside;
            }
        }
    }
    return inside;
}

std::vector<QPointF> clippedPointsToImage(const std::vector<QPointF> &points, const QRectF &image_rect)
{
    std::vector<QPointF> clipped;
    clipped.reserve(points.size());
    for (const QPointF &point : points)
    {
        clipped.push_back(clampPointToRect(point, image_rect));
    }
    return clipped;
}

double distanceToSegment(const QPointF &point, const QPointF &a, const QPointF &b)
{
    const QLineF segment(a, b);
    if (segment.length() <= 0.0001)
    {
        return QLineF(point, a).length();
    }

    const double  ax = a.x();
    const double  ay = a.y();
    const double  bx = b.x();
    const double  by = b.y();
    const double  dx = bx - ax;
    const double  dy = by - ay;
    const double  t  = std::clamp(((point.x() - ax) * dx + (point.y() - ay) * dy) / (dx * dx + dy * dy), 0.0, 1.0);
    const QPointF projection(ax + t * dx, ay + t * dy);
    return QLineF(point, projection).length();
}

} // namespace

LabelData_t::LabelData_t() {}

LabelData_t::~LabelData_t() {}

std::vector<uint8_t> LabelData_t::toBlob() const
{
    json j = json{
        {     "x",      x},
        {     "y",      y},
        { "width",  width},
        {"height", height},
    };

    return json::to_bson(j);
}

void LabelData_t::fromBlob(const std::vector<uint8_t> &blob)
{
    json j = json::from_bson(blob);
    x      = j.value<double>("x", -1);
    y      = j.value<double>("y", -1);
    width  = j.value<double>("width", -1);
    height = j.value<double>("height", -1);
}

void LabelData_t::fromQVariantMap(const QVariantMap &data, const QRectF &image_rect)
{
    double ix = data.value("x", -1).toDouble();
    double iy = data.value("y", -1).toDouble();
    double iw = data.value("width", -1).toDouble();
    double ih = data.value("height", -1).toDouble();

    QRectF rect = QRectF(ix, iy, iw, ih).intersected(image_rect);

    x      = rect.x();
    y      = rect.y();
    width  = rect.width();
    height = rect.height();
}

QVariantMap LabelData_t::dataMap()
{
    return QVariantMap{
        {     "x",      x},
        {     "y",      y},
        { "width",  width},
        {"height", height},
    };
}

DetLabelData_t::DetLabelData_t() {}

DetLabelData_t::~DetLabelData_t() {}

int DetLabelData_t::type() const
{
    return DeepLearningMethod::Detection;
}

std::vector<uint8_t> DetLabelData_t::toBlob() const
{
    return LabelData_t::toBlob();
}

void DetLabelData_t::fromBlob(const std::vector<uint8_t> &blob)
{
    LabelData_t::fromBlob(blob);
}

void DetLabelData_t::fromQVariantMap(const QVariantMap &data, const QRectF &image_rect)
{
    LabelData_t::fromQVariantMap(data, image_rect);
}

std::pair<std::vector<QString>, std::vector<QString>> DetLabelData_t::columns()
{
    return {
        std::vector<QString>{          "类别", "X", "Y",  "宽度",   "高度"},
        std::vector<QString>{"label_class_id", "x", "y", "width", "height"}
    };
}

int SegLabelData_t::type() const
{
    return DeepLearningMethod::Segmentation;
}

int AnomalyLabelData_t::type() const
{
    return DeepLearningMethod::AnomalyDetection;
}

std::vector<uint8_t> SegLabelData_t::toBlob() const
{
    json point_array = json::array();
    for (const QPointF &point : points)
    {
        point_array.push_back({
            {"x", point.x()},
            {"y", point.y()},
        });
    }

    json j = json{
        {          "x",             x},
        {          "y",             y},
        {      "width",         width},
        {     "height",        height},
        {"point_count", points.size()},
        {     "points",   point_array},
    };

    return json::to_bson(j);
}

void SegLabelData_t::fromBlob(const std::vector<uint8_t> &blob)
{
    json j = json::from_bson(blob);
    x      = j.value<double>("x", -1);
    y      = j.value<double>("y", -1);
    width  = j.value<double>("width", -1);
    height = j.value<double>("height", -1);

    points.clear();
    if (j.contains("points") && j["points"].is_array())
    {
        for (const auto &point_json : j["points"])
        {
            if (point_json.contains("x") && point_json.contains("y") && point_json["x"].is_number()
                && point_json["y"].is_number())
            {
                points.emplace_back(point_json["x"].get<double>(), point_json["y"].get<double>());
            }
        }
    }

    if (!points.empty())
    {
        updateBoundingBoxFromPoints(*this, points);
    }
    else if (width > 0 && height > 0)
    {
        points = {
            QPointF(x, y),
            QPointF(x + width, y),
            QPointF(x + width, y + height),
            QPointF(x, y + height),
        };
    }
}

void SegLabelData_t::fromQVariantMap(const QVariantMap &data, const QRectF &image_rect)
{
    points = clippedPointsToImage(variantListToPoints(data.value("points")), image_rect);

    if (points.size() < 3)
    {
        // 导入或兼容旧数据时，如果只有 bbox，就按矩形生成一个四点多边形。
        LabelData_t::fromQVariantMap(data, image_rect);
        if (width > 0 && height > 0)
        {
            points = {
                QPointF(x, y),
                QPointF(x + width, y),
                QPointF(x + width, y + height),
                QPointF(x, y + height),
            };
        }
        return;
    }

    updateBoundingBoxFromPoints(*this, points);
}

QVariantMap SegLabelData_t::dataMap()
{
    return QVariantMap{
        {          "x",                               x},
        {          "y",                               y},
        {      "width",                           width},
        {     "height",                          height},
        {"point_count", static_cast<int>(points.size())},
        {     "points",     pointsToVariantList(points)},
    };
}

std::pair<std::vector<QString>, std::vector<QString>> SegLabelData_t::columns()
{
    return {
        std::vector<QString>{          "类别",      "顶点数", "X", "Y",  "宽度",   "高度"},
        std::vector<QString>{"label_class_id", "point_count", "x", "y", "width", "height"}
    };
}

ClsLabelData_t::ClsLabelData_t() {}

ClsLabelData_t::~ClsLabelData_t() {}

int ClsLabelData_t::type() const
{
    return DeepLearningMethod::Classification;
}

std::pair<std::vector<QString>, std::vector<QString>> ClsLabelData_t::columns()
{
    return {
        std::vector<QString>{           "类别"},
        std::vector<QString>{"label_class_id"}
    };
}

LabelDataHelper_t::LabelDataHelper_t(const int type)
    : type_(type)
{
}

LabelDataHelper_t::~LabelDataHelper_t() {}

DetLabelDataHelper::DetLabelDataHelper(const int type)
    : LabelDataHelper_t(type)
{
}

DetLabelDataHelper::~DetLabelDataHelper() {}

std::unique_ptr<LabelData_t> DetLabelDataHelper::createLabelData() const
{
    return std::make_unique<dltool::data::DetLabelData_t>();
}

std::pair<std::vector<QString>, std::vector<QString>> DetLabelDataHelper::dataColumns() const
{
    return DetLabelData_t::columns();
}

bool DetLabelDataHelper::isInside(const QPointF &pos, const std::unique_ptr<LabelData_t> &label_data_ptr) const
{
    if (label_data_ptr == nullptr)
        return false;
    DetLabelData_t *data = dynamic_cast<DetLabelData_t *>(label_data_ptr.get());
    if (data == nullptr)
        return false;
    double x = data->x;
    double y = data->y;
    double w = data->width;
    double h = data->height;
    return pos.x() >= x && pos.x() <= x + w && pos.y() >= y && pos.y() <= y + h;
}

QVariantMap DetLabelDataHelper::hitTestHandle(const QPointF &pos, const std::unique_ptr<LabelData_t> &label_data_ptr,
                                              const double scale) const
{
    if (label_data_ptr == nullptr)
    {
        return QVariantMap{
            {    "found",                  false},
            {     "mode",     EditMode::NoneMode},
            {"direction", EditDirection::NoneDir},
            {   "cursor",   int(Qt::ArrowCursor)}
        };
    }
    DetLabelData_t *data = dynamic_cast<DetLabelData_t *>(label_data_ptr.get());
    if (data == nullptr)
    {
        return QVariantMap{
            {    "found",                  false},
            {     "mode",     EditMode::NoneMode},
            {"direction", EditDirection::NoneDir},
            {   "cursor",   int(Qt::ArrowCursor)}
        };
    }
    double x = data->x;
    double y = data->y;
    double w = data->width;
    double h = data->height;

    const double handle_size = 10 / scale;

    static std::map<int, int> cursor_shapes{
        {    EditDirection::TopLeft, Qt::SizeFDiagCursor},
        {   EditDirection::TopRight, Qt::SizeBDiagCursor},
        { EditDirection::BottomLeft, Qt::SizeBDiagCursor},
        {EditDirection::BottomRight, Qt::SizeFDiagCursor},
        {       EditDirection::Left,   Qt::SizeHorCursor},
        {      EditDirection::Right,   Qt::SizeHorCursor},
        {        EditDirection::Top,   Qt::SizeVerCursor},
        {     EditDirection::Bottom,   Qt::SizeVerCursor},
    };

    std::vector<std::pair<int, std::vector<double>>> vertices = {
        {    EditDirection::TopLeft,         {x, y}},
        {   EditDirection::TopRight,     {x + w, y}},
        { EditDirection::BottomLeft,     {x, y + h}},
        {EditDirection::BottomRight, {x + w, y + h}},
    };
    std::vector<std::pair<int, std::vector<double>>> edges = {
        {  EditDirection::Left,         {x - handle_size, x + handle_size, y, y + h}},
        { EditDirection::Right, {x + w - handle_size, x + w + handle_size, y, y + h}},
        {   EditDirection::Top,         {x, x + w, y - handle_size, y + handle_size}},
        {EditDirection::Bottom, {x, x + w, y + h - handle_size, y + h + handle_size}},
    };

    for (const auto &[dir, _data] : vertices)
    {
        if (std::abs(_data[0] - pos.x()) <= handle_size && std::abs(_data[1] - pos.y()) <= handle_size)
        {
            return QVariantMap{
                {    "found",                    true},
                {     "mode",        EditMode::Resize},
                {"direction",                     dir},
                {   "cursor", int(cursor_shapes[dir])}
            };
        }
    }
    for (const auto &[dir, _data] : edges)
    {
        if (pos.x() >= _data[0] && pos.x() <= _data[1] && pos.y() >= _data[2] && pos.y() <= _data[3])
        {
            return QVariantMap{
                {    "found",                    true},
                {     "mode",        EditMode::Resize},
                {"direction",                     dir},
                {   "cursor", int(cursor_shapes[dir])}
            };
        }
    }
    if (pos.x() >= x && pos.x() <= x + w && pos.y() >= y && pos.y() <= y + h)
    {
        return QVariantMap{
            {    "found",                   true},
            {     "mode",         EditMode::Move},
            {"direction",  EditDirection::Inside},
            {   "cursor", int(Qt::SizeAllCursor)}
        };
    }

    return QVariantMap{
        {    "found",                  false},
        {     "mode",     EditMode::NoneMode},
        {"direction", EditDirection::NoneDir},
        {   "cursor",   int(Qt::ArrowCursor)}
    };
}

QVariantMap DetLabelDataHelper::getEditedData(const QVariantMap &data, const QPointF &start, const QPointF &end,
                                              const QRectF &image_rect) const
{
    QVariantMap new_data = data;

    double x = data.value("x", -1).toDouble();
    double y = data.value("y", -1).toDouble();
    double w = data.value("width", -1).toDouble();
    double h = data.value("height", -1).toDouble();

    double dx = end.x() - start.x();
    double dy = end.y() - start.y();

    const QVariantMap &hit = data.value("hit", QVariantMap()).toMap();

    int mode = hit.value("mode", EditMode::NoneMode).toInt();
    int dir  = hit.value("direction", EditDirection::NoneDir).toInt();

    constexpr double min_size = 5;

    QRectF rect;
    if (mode == EditMode::Move)
    {
        double x0 = std::max(0.0, std::min(image_rect.right() - w, x + dx));
        double y0 = std::max(0.0, std::min(image_rect.bottom() - h, y + dy));
        rect      = QRectF(x0, y0, w, h);
    }
    else if (mode == EditMode::Resize)
    {
        switch (dir)
        {
        case EditDirection::TopLeft:
        {
            // 固定右边界和下边界
            double x1 = x + w;
            double y1 = y + h;
            double x0 = end.x() < x ? end.x() : std::min(x1 - min_size, end.x());
            double y0 = end.y() < y ? end.y() : std::min(y1 - min_size, end.y());
            rect      = QRectF(x0, y0, x1 - x0, y1 - y0);
            break;
        }
        case EditDirection::TopRight:
        {
            // 固定左边界和下边界
            double x0 = x;
            double y1 = y + h;
            double x1 = end.x() >= x ? end.x() : std::max(x0 + min_size, end.x());
            double y0 = end.y() < y ? end.y() : std::min(y1 - min_size, end.y());
            rect      = QRectF(x0, y0, x1 - x0, y1 - y0);
            break;
        }
        case EditDirection::BottomLeft:
        {
            // 固定右边界和上边界
            double x1 = x + w;
            double y0 = y;
            double x0 = end.x() < x ? end.x() : std::min(x1 - min_size, end.x());
            double y1 = end.y() >= y ? end.y() : std::max(y0 + min_size, end.y());
            rect      = QRectF(x0, y0, x1 - x0, y1 - y0);
            break;
        }
        case EditDirection::BottomRight:
        {
            // 固定左边界和上边界
            double x0 = x;
            double y0 = y;
            double x1 = end.x() >= x ? end.x() : std::max(x0 + min_size, end.x());
            double y1 = end.y() >= y ? end.y() : std::max(y0 + min_size, end.y());
            rect      = QRectF(x0, y0, x1 - x0, y1 - y0);
            break;
        }
        case EditDirection::Left:
        {
            // 固定右边界
            double x1 = x + w;
            double x0 = end.x() < x ? end.x() : std::min(x1 - min_size, end.x());
            rect      = QRectF(x0, y, x1 - x0, h);
            break;
        }
        case EditDirection::Right:
        {
            // 固定左边界
            double x0 = x;
            double x1 = end.x() >= x ? end.x() : std::max(x0 + min_size, end.x());
            rect      = QRectF(x0, y, x1 - x0, h);
            break;
        }
        case EditDirection::Top:
        {
            // 固定下边界
            double y1 = y + h;
            double y0 = end.y() < y ? end.y() : std::min(y1 - min_size, end.y());
            rect      = QRectF(x, y0, w, y1 - y0);
            break;
        }
        case EditDirection::Bottom:
        {
            // 固定上边界
            double y0 = y;
            double y1 = end.y() >= y ? end.y() : std::max(y0 + min_size, end.y());
            rect      = QRectF(x, y0, w, y1 - y0);
            break;
        }
        default:
            rect = QRectF(x, y, w, h);
            break;
        }
        rect = rect.intersected(image_rect);
    }

    new_data["x"]      = rect.x();
    new_data["y"]      = rect.y();
    new_data["width"]  = rect.width();
    new_data["height"] = rect.height();

    return new_data;
}

SegLabelDataHelper::SegLabelDataHelper(const int type)
    : LabelDataHelper_t(type)
{
}

SegLabelDataHelper::~SegLabelDataHelper() {}

std::unique_ptr<LabelData_t> SegLabelDataHelper::createLabelData() const
{
    return std::make_unique<dltool::data::SegLabelData_t>();
}

std::pair<std::vector<QString>, std::vector<QString>> SegLabelDataHelper::dataColumns() const
{
    return SegLabelData_t::columns();
}

bool SegLabelDataHelper::isInside(const QPointF &pos, const std::unique_ptr<LabelData_t> &label_data_ptr) const
{
    if (label_data_ptr == nullptr)
        return false;
    const SegLabelData_t *data = dynamic_cast<SegLabelData_t *>(label_data_ptr.get());
    if (data == nullptr || data->points.size() < 3)
        return false;
    if (!isPointNearLabelBounds(*data, pos))
        return false;

    return isPointInPolygon(pos, data->points);
}

QVariantMap SegLabelDataHelper::hitTestHandle(const QPointF &pos, const std::unique_ptr<LabelData_t> &label_data_ptr,
                                              const double scale) const
{
    if (label_data_ptr == nullptr)
    {
        return QVariantMap{
            {    "found",                  false},
            {     "mode",     EditMode::NoneMode},
            {"direction", EditDirection::NoneDir},
            {   "cursor",   int(Qt::ArrowCursor)}
        };
    }

    const SegLabelData_t *data = dynamic_cast<SegLabelData_t *>(label_data_ptr.get());
    if (data == nullptr || data->points.empty())
    {
        return QVariantMap{
            {    "found",                  false},
            {     "mode",     EditMode::NoneMode},
            {"direction", EditDirection::NoneDir},
            {   "cursor",   int(Qt::ArrowCursor)}
        };
    }

    const double handle_size      = 10 / scale;
    const double edge_handle_size = 12 / scale;
    if (!isPointNearLabelBounds(*data, pos, edge_handle_size))
    {
        return QVariantMap{
            {    "found",                  false},
            {     "mode",     EditMode::NoneMode},
            {"direction", EditDirection::NoneDir},
            {   "cursor",   int(Qt::ArrowCursor)}
        };
    }

    for (int i = 0; i < static_cast<int>(data->points.size()); ++i)
    {
        if (QLineF(pos, data->points[i]).length() <= handle_size)
        {
            return QVariantMap{
                {    "found",                 true},
                {     "mode",     EditMode::Resize},
                {"direction",                    i},
                {   "cursor", int(Qt::CrossCursor)}
            };
        }
    }

    for (int i = 0; i < static_cast<int>(data->points.size()); ++i)
    {
        const QPointF &a = data->points[i];
        const QPointF &b = data->points[(i + 1) % data->points.size()];
        if (distanceToSegment(pos, a, b) <= edge_handle_size)
        {
            const int next_index = (i + 1) % static_cast<int>(data->points.size());
            return QVariantMap{
                {          "found",                   true},
                {           "mode",         EditMode::Move},
                {      "direction",  EditDirection::Inside},
                {     "edge_index",                      i},
                {"edge_next_index",             next_index},
                {         "cursor", int(Qt::SizeAllCursor)}
            };
        }
    }

    if (isInside(pos, label_data_ptr))
    {
        return QVariantMap{
            {    "found",                   true},
            {     "mode",         EditMode::Move},
            {"direction",  EditDirection::Inside},
            {   "cursor", int(Qt::SizeAllCursor)}
        };
    }

    return QVariantMap{
        {    "found",                  false},
        {     "mode",     EditMode::NoneMode},
        {"direction", EditDirection::NoneDir},
        {   "cursor",   int(Qt::ArrowCursor)}
    };
}

QVariantMap SegLabelDataHelper::getEditedData(const QVariantMap &data, const QPointF &start, const QPointF &end,
                                              const QRectF &image_rect) const
{
    std::vector<QPointF> points = variantListToPoints(data.value("points"));
    if (points.empty())
    {
        return data;
    }

    const double dx = end.x() - start.x();
    const double dy = end.y() - start.y();

    const QVariantMap hit  = data.value("hit", QVariantMap()).toMap();
    const int         mode = hit.value("mode", EditMode::NoneMode).toInt();
    const int         dir  = hit.value("direction", EditDirection::NoneDir).toInt();

    if (mode == EditMode::Move)
    {
        double min_dx = -std::numeric_limits<double>::max();
        double max_dx = std::numeric_limits<double>::max();
        double min_dy = -std::numeric_limits<double>::max();
        double max_dy = std::numeric_limits<double>::max();
        for (const QPointF &point : points)
        {
            min_dx = std::max(min_dx, image_rect.left() - point.x());
            max_dx = std::min(max_dx, image_rect.right() - point.x());
            min_dy = std::max(min_dy, image_rect.top() - point.y());
            max_dy = std::min(max_dy, image_rect.bottom() - point.y());
        }

        const double safe_dx = std::clamp(dx, min_dx, max_dx);
        const double safe_dy = std::clamp(dy, min_dy, max_dy);
        for (QPointF &point : points)
        {
            point += QPointF(safe_dx, safe_dy);
        }
    }
    else if (mode == EditMode::Resize && dir >= 0 && dir < static_cast<int>(points.size()))
    {
        points[dir] = clampPointToRect(points[dir] + QPointF(dx, dy), image_rect);
    }

    const QRectF bbox = boundingBoxFromPoints(points);

    QVariantMap new_data    = data;
    new_data["x"]           = bbox.x();
    new_data["y"]           = bbox.y();
    new_data["width"]       = bbox.width();
    new_data["height"]      = bbox.height();
    new_data["point_count"] = static_cast<int>(points.size());
    new_data["points"]      = pointsToVariantList(points);
    return new_data;
}

AnomalyLabelDataHelper::AnomalyLabelDataHelper(const int type)
    : SegLabelDataHelper(type)
{
}

AnomalyLabelDataHelper::~AnomalyLabelDataHelper() {}

std::unique_ptr<LabelData_t> AnomalyLabelDataHelper::createLabelData() const
{
    return std::make_unique<dltool::data::AnomalyLabelData_t>();
}

ClsLabelDataHelper::ClsLabelDataHelper(const int type)
    : LabelDataHelper_t(type)
{
}

ClsLabelDataHelper::~ClsLabelDataHelper() {}

std::unique_ptr<LabelData_t> ClsLabelDataHelper::createLabelData() const
{
    return std::make_unique<ClsLabelData_t>();
}

std::pair<std::vector<QString>, std::vector<QString>> ClsLabelDataHelper::dataColumns() const
{
    return ClsLabelData_t::columns();
}

bool ClsLabelDataHelper::isInside(const QPointF &pos, const std::unique_ptr<LabelData_t> &label_data_ptr) const
{
    Q_UNUSED(pos)
    return label_data_ptr != nullptr;
}

QVariantMap ClsLabelDataHelper::hitTestHandle(const QPointF &pos, const std::unique_ptr<LabelData_t> &label_data_ptr,
                                              const double scale) const
{
    Q_UNUSED(pos)
    Q_UNUSED(scale)

    if (label_data_ptr == nullptr)
    {
        return QVariantMap{
            {    "found",                  false},
            {     "mode",     EditMode::NoneMode},
            {"direction", EditDirection::NoneDir},
            {   "cursor",   int(Qt::ArrowCursor)}
        };
    }
    return QVariantMap{
        {    "found",                   true},
        {     "mode",         EditMode::Move},
        {"direction",  EditDirection::Inside},
        {   "cursor", int(Qt::SizeAllCursor)}
    };
}

QVariantMap ClsLabelDataHelper::getEditedData(const QVariantMap &data, const QPointF &start, const QPointF &end,
                                              const QRectF &image_rect) const
{
    Q_UNUSED(start)
    Q_UNUSED(end)
    Q_UNUSED(image_rect)
    return data;
}

std::unique_ptr<LabelDataHelper_t> createLabelDataHelper(const int type)
{
    switch (type)
    {
    case DeepLearningMethod::Classification:
        return std::make_unique<ClsLabelDataHelper>(type);
    case DeepLearningMethod::Detection:
        return std::make_unique<DetLabelDataHelper>(type);
    case DeepLearningMethod::Segmentation:
        return std::make_unique<SegLabelDataHelper>(type);
    case DeepLearningMethod::AnomalyDetection:
        return std::make_unique<AnomalyLabelDataHelper>(type);
    default:
        return nullptr;
    }
}

} // namespace dltool::data
