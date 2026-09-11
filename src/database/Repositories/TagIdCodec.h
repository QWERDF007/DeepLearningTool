#pragma once

/**
 * @file TagIdCodec.h
 * @brief data 模块私有：标签关系 tag-id 集合的列编码/解码工具。
 *
 * 数据库 schema 独立于 Qt 容器：tag 关系每行保存一个图像或标注的
 * 完整 tag-class ID 集合（定长小端编码）。供 TagRepository 与
 * ProjectDataBase 的跨实体原子工作流共用。
 */

#include "database/DataBase.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace dltool::database::detail {

using TagIds = std::vector<int64_t>;

constexpr int kImageTagType = static_cast<int>(TagType::Image);
constexpr int kLabelTagType = static_cast<int>(TagType::Label);

inline std::vector<uint8_t> encodeTagIds(const TagIds &tag_ids)
{
    std::vector<uint8_t> encoded;
    encoded.reserve(tag_ids.size() * sizeof(int64_t));
    for (const int64_t tag_id : tag_ids)
    {
        const uint64_t value = static_cast<uint64_t>(tag_id);
        for (size_t byte_index = 0; byte_index < sizeof(value); ++byte_index)
            encoded.push_back(static_cast<uint8_t>((value >> (byte_index * 8)) & 0xff));
    }
    return encoded;
}

inline TagIds decodeTagIds(const std::vector<uint8_t> &encoded)
{
    if (encoded.empty() || encoded.size() % sizeof(int64_t) != 0)
        return {};

    TagIds tag_ids;
    tag_ids.reserve(encoded.size() / sizeof(int64_t));
    for (size_t offset = 0; offset < encoded.size(); offset += sizeof(int64_t))
    {
        uint64_t value = 0;
        for (size_t byte_index = 0; byte_index < sizeof(value); ++byte_index)
            value |= static_cast<uint64_t>(encoded[offset + byte_index]) << (byte_index * 8);
        tag_ids.push_back(static_cast<int64_t>(value));
    }
    return tag_ids;
}

inline void appendTagId(TagIds &tag_ids, const int64_t tag_id)
{
    if (std::find(tag_ids.begin(), tag_ids.end(), tag_id) == tag_ids.end())
        tag_ids.push_back(tag_id);
}

inline bool removeTagId(TagIds &tag_ids, const int64_t tag_id)
{
    const auto found = std::remove(tag_ids.begin(), tag_ids.end(), tag_id);
    if (found == tag_ids.end())
        return false;
    tag_ids.erase(found, tag_ids.end());
    return true;
}

} // namespace dltool::database::detail
