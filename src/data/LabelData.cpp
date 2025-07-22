#include "data/LabelData.h"

#include "LabelData.h"
#include "data/CoreDef.h"

#include <json.hpp>

using json = nlohmann::json;

namespace dltool::data {

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

    x = std::min<double>(image_rect.width(), std::max<double>(0, ix));
    y = std::min<double>(image_rect.height(), std::max<double>(0, iy));

    double x2 = ix + iw;
    double y2 = iy + ih;
    x2        = std::min<double>(image_rect.width(), std::max<double>(0, x2));
    y2        = std::min<double>(image_rect.height(), std::max<double>(0, y2));

    width  = std::min<double>(image_rect.width(), std::max<double>(0, x2 - x));
    height = std::min<double>(image_rect.height(), std::max<double>(0, y2 - y));
}

const QVariantMap &LabelData_t::dataMap()
{
    if (data_map_.isEmpty())
    {
        data_map_ = QVariantMap{
            {     "x",      x},
            {     "y",      y},
            { "width",  width},
            {"height", height},
        };
    }
    return data_map_;
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
            {    "found",                false},
            {"direction",                   ""},
            {   "cursor", int(Qt::ArrowCursor)}
        };
    }
    DetLabelData_t *data = dynamic_cast<DetLabelData_t *>(label_data_ptr.get());
    if (data == nullptr)
    {
        return QVariantMap{
            {    "found",                false},
            {"direction",                   ""},
            {   "cursor", int(Qt::ArrowCursor)}
        };
    }
    double x = data->x;
    double y = data->y;
    double w = data->width;
    double h = data->height;

    const double handle_size = 10 / scale;

    static std::map<QString, int> cursor_shapes{
        {"tl", Qt::SizeFDiagCursor},
        {"tr", Qt::SizeBDiagCursor},
        {"bl", Qt::SizeBDiagCursor},
        {"br", Qt::SizeFDiagCursor},
        { "l",   Qt::SizeHorCursor},
        { "r",   Qt::SizeHorCursor},
        { "t",   Qt::SizeVerCursor},
        { "b",   Qt::SizeVerCursor},
    };

    std::vector<std::pair<QString, std::vector<double>>> vertices = {
        {"tl",         {x, y}},
        {"tr",     {x + w, y}},
        {"bl",     {x, y + h}},
        {"br", {x + w, y + h}},
    };
    std::vector<std::pair<QString, std::vector<double>>> edges = {
        {"l",         {x - handle_size, x + handle_size, y, y + h}},
        {"r", {x + w - handle_size, x + w + handle_size, y, y + h}},
        {"t",         {x, x + w, y - handle_size, y + handle_size}},
        {"b", {x, x + w, y + h - handle_size, y + h + handle_size}},
    };

    for (const auto &[dir, _data] : vertices)
    {
        if (std::abs(_data[0] - pos.x()) <= handle_size && std::abs(_data[1] - pos.y()) <= handle_size)
        {
            return QVariantMap{
                {    "found",                    true},
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
                {"direction",                     dir},
                {   "cursor", int(cursor_shapes[dir])}
            };
        }
    }

    return QVariantMap{
        {    "found",                false},
        {"direction",                   ""},
        {   "cursor", int(Qt::ArrowCursor)}
    };
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
