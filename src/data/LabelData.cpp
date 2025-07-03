#include "data/LabelData.h"

#include "LabelData.h"
#include "data/CoreDef.h"

#include <json.hpp>

using json = nlohmann::json;

namespace dltool::data {

LabelData_t::LabelData_t()
{

}

LabelData_t::~LabelData_t()
{

}

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

void LabelData_t::fromQVariantMap(const QVariantMap &data)
{
    x      = data.value("x", -1).toDouble();
    y      = data.value("y", -1).toDouble();
    width  = data.value("width", -1).toDouble();
    height = data.value("height", -1).toDouble();
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

DetLabelData_t::DetLabelData_t()
{
}

DetLabelData_t::~DetLabelData_t()
{
}

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

void DetLabelData_t::fromQVariantMap(const QVariantMap &data)
{
    LabelData_t::fromQVariantMap(data);
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

void SegLabelData_t::fromQVariantMap(const QVariantMap &data)
{
    LabelData_t::fromQVariantMap(data);
}

// clang-format off
LabelDataFactory createLabelDataFactory(const int type)
{
    static std::unordered_map<int, LabelDataFactory> factory_map = {
        {DeepLearningMethod::Detection, []() { return std::make_unique<dltool::data::DetLabelData_t>(); }},
    };
    auto found = factory_map.find(type);
    if (found != factory_map.end())
        return found->second;
    return nullptr;
}

// clang-format on

std::pair<std::vector<QString>, std::vector<QString>> LabelDataColumns(const int type)
{
    switch (type)
    {
    case DeepLearningMethod::Detection:
        return DetLabelData_t::columns();
    default:
        return {std::vector<QString>{}, std::vector<QString>{}};
    }
}

} // namespace dltool::data
