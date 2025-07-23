#include "data/LabelData.h"

#include "LabelData.h"
#include "data/CoreDef.h"

#include <json.hpp>

using json = nlohmann::json;

namespace dltool::data {

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

std::vector<uint8_t> SegLabelData_t::toBlob() const
{
    return LabelData_t::toBlob();
}

void SegLabelData_t::fromBlob(const std::vector<uint8_t> &blob)
{
    LabelData_t::fromBlob(blob);
}

void SegLabelData_t::fromQVariantMap(const QVariantMap &data, const QRectF &image_rect)
{
    LabelData_t::fromQVariantMap(data, image_rect);
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

std::unique_ptr<LabelDataHelper_t> createLabelDataHelper(const int type)
{
    switch (type)
    {
    case DeepLearningMethod::Detection:
        return std::make_unique<DetLabelDataHelper>(type);
    default:
        return nullptr;
    }
}

} // namespace dltool::data
