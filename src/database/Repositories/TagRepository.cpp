#include "TagRepository.h"

#include "TagIdCodec.h"
#include "database/ddl/TagClassesTable.h"
#include "database/ddl/TagsTable.h"

#include <sqlpp11/sqlpp11.h>

namespace dltool::database {

namespace {
const auto TagClassesTable = TagClasses{};
const auto TagsTable       = Tags{};
} // namespace

bool TagRepository::getAllTagClasses(DatabaseContext &context, std::vector<int64_t> &tag_class_ids,
                                     std::vector<QString> &names, std::vector<std::vector<uint8_t>> &extra_data,
                                     QString &err_msg)
{
    try
    {
        auto &db   = context.db();
        auto  data = db(sqlpp::select(TagClassesTable.id, TagClassesTable.name, TagClassesTable.extraData)
                           .from(TagClassesTable)
                           .unconditionally());
        for (const auto &row : data)
        {
            tag_class_ids.emplace_back(row.id);
            names.emplace_back(QString::fromStdString(row.name));
            extra_data.emplace_back(row.extraData.is_null() ? std::vector<uint8_t>{} : row.extraData.value());
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool TagRepository::addTagClass(DatabaseContext &context, const QString &name, const std::vector<uint8_t> &extra_data,
                                int64_t &tag_class_id, QString &err_msg)
{
    try
    {
        auto &db = context.db();
        db(sqlpp::insert_into(TagClassesTable)
               .set(TagClassesTable.name = name.toUtf8().constData(), TagClassesTable.extraData = extra_data));
        tag_class_id = static_cast<int64_t>(db.last_insert_id());
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool TagRepository::updateTagClass(DatabaseContext &context, const int64_t tag_class_id, const QString &name,
                                   const std::vector<uint8_t> &extra_data, QString &err_msg)
{
    try
    {
        auto &db = context.db();
        db(sqlpp::update(TagClassesTable)
               .set(TagClassesTable.name = name.toUtf8().constData(), TagClassesTable.extraData = extra_data)
               .where(TagClassesTable.id == tag_class_id));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool TagRepository::deleteTagClass(DatabaseContext &context, const int64_t tag_class_id, QString &err_msg)
{
    try
    {
        auto &db = context.db();
        // 调用方在同一事务中提交：这里同步清空 tags 关系行中的该类别 ID。
        auto data = db(sqlpp::select(TagsTable.id, TagsTable.tagIds).from(TagsTable).unconditionally());
        for (const auto &row : data)
        {
            detail::TagIds tag_ids = detail::decodeTagIds(row.tagIds);
            if (!detail::removeTagId(tag_ids, tag_class_id))
                continue;

            if (tag_ids.empty())
            {
                db(sqlpp::remove_from(TagsTable).where(TagsTable.id == row.id));
            }
            else
            {
                db(sqlpp::update(TagsTable)
                       .set(TagsTable.tagIds = detail::encodeTagIds(tag_ids))
                       .where(TagsTable.id == row.id));
            }
        }
        db(sqlpp::remove_from(TagClassesTable).where(TagClassesTable.id == tag_class_id));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool TagRepository::getAllTags(DatabaseContext &context, std::vector<int64_t> &image_ids,
                               std::vector<std::vector<int64_t>> &image_tag_ids, std::vector<int64_t> &label_ids,
                               std::vector<std::vector<int64_t>> &label_tag_ids, QString &err_msg)
{
    try
    {
        image_ids.clear();
        image_tag_ids.clear();
        label_ids.clear();
        label_tag_ids.clear();

        auto &db   = context.db();
        auto  data = db(sqlpp::select(TagsTable.imageId, TagsTable.labelId, TagsTable.tagIds, TagsTable.type)
                           .from(TagsTable)
                           .unconditionally());
        for (const auto &row : data)
        {
            const detail::TagIds tag_ids = detail::decodeTagIds(row.tagIds);
            const int type = static_cast<int>(row.type.value());
            if (type == detail::kImageTagType && !row.imageId.is_null())
            {
                image_ids.emplace_back(row.imageId.value());
                image_tag_ids.emplace_back(tag_ids);
            }
            else if (type == detail::kLabelTagType && !row.labelId.is_null())
            {
                label_ids.emplace_back(row.labelId.value());
                label_tag_ids.emplace_back(tag_ids);
            }
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool TagRepository::addTagsToImages(DatabaseContext &context, const std::vector<int64_t> &image_ids,
                                    const int64_t tag_id, QString &err_msg)
{
    if (image_ids.empty())
        return true;
    try
    {
        auto &db = context.db();
        for (const int64_t image_id : image_ids)
        {
            auto data = db(sqlpp::select(TagsTable.id, TagsTable.tagIds)
                               .from(TagsTable)
                               .where(TagsTable.imageId == image_id && TagsTable.type == detail::kImageTagType));
            if (data.empty())
            {
                db(sqlpp::insert_into(TagsTable).set(TagsTable.imageId = image_id,
                                                     TagsTable.tagIds = detail::encodeTagIds({tag_id}),
                                                     TagsTable.type   = detail::kImageTagType));
                continue;
            }

            const auto &row        = data.front();
            detail::TagIds tag_ids = detail::decodeTagIds(row.tagIds);
            const size_t old_size  = tag_ids.size();
            detail::appendTagId(tag_ids, tag_id);
            if (tag_ids.size() != old_size)
            {
                db(sqlpp::update(TagsTable)
                       .set(TagsTable.tagIds = detail::encodeTagIds(tag_ids))
                       .where(TagsTable.id == row.id));
            }
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool TagRepository::removeTagsFromImages(DatabaseContext &context, const std::vector<int64_t> &image_ids,
                                         const int64_t tag_id, QString &err_msg)
{
    if (image_ids.empty())
        return true;
    try
    {
        auto &db = context.db();
        for (const int64_t image_id : image_ids)
        {
            auto data = db(sqlpp::select(TagsTable.id, TagsTable.tagIds)
                               .from(TagsTable)
                               .where(TagsTable.imageId == image_id && TagsTable.type == detail::kImageTagType));
            if (data.empty())
                continue;

            const auto &row        = data.front();
            detail::TagIds tag_ids = detail::decodeTagIds(row.tagIds);
            if (!detail::removeTagId(tag_ids, tag_id))
                continue;
            if (tag_ids.empty())
                db(sqlpp::remove_from(TagsTable).where(TagsTable.id == row.id));
            else
                db(sqlpp::update(TagsTable)
                       .set(TagsTable.tagIds = detail::encodeTagIds(tag_ids))
                       .where(TagsTable.id == row.id));
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool TagRepository::removeTagsForImages(DatabaseContext &context, const std::vector<int64_t> &image_ids,
                                        QString &err_msg)
{
    if (image_ids.empty())
        return true;
    try
    {
        auto &db = context.db();
        db(sqlpp::remove_from(TagsTable).where(TagsTable.imageId.in(sqlpp::value_list(image_ids))
                                               && TagsTable.type == detail::kImageTagType));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool TagRepository::addTagsToLabels(DatabaseContext &context, const std::vector<int64_t> &label_ids,
                                    const int64_t tag_id, QString &err_msg)
{
    if (label_ids.empty())
        return true;
    try
    {
        auto &db = context.db();
        for (const int64_t label_id : label_ids)
        {
            auto data = db(sqlpp::select(TagsTable.id, TagsTable.tagIds)
                               .from(TagsTable)
                               .where(TagsTable.labelId == label_id && TagsTable.type == detail::kLabelTagType));
            if (data.empty())
            {
                db(sqlpp::insert_into(TagsTable).set(TagsTable.labelId = label_id,
                                                     TagsTable.tagIds = detail::encodeTagIds({tag_id}),
                                                     TagsTable.type   = detail::kLabelTagType));
                continue;
            }

            const auto &row        = data.front();
            detail::TagIds tag_ids = detail::decodeTagIds(row.tagIds);
            const size_t old_size  = tag_ids.size();
            detail::appendTagId(tag_ids, tag_id);
            if (tag_ids.size() != old_size)
            {
                db(sqlpp::update(TagsTable)
                       .set(TagsTable.tagIds = detail::encodeTagIds(tag_ids))
                       .where(TagsTable.id == row.id));
            }
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool TagRepository::removeTagsFromLabels(DatabaseContext &context, const std::vector<int64_t> &label_ids,
                                         const int64_t tag_id, QString &err_msg)
{
    if (label_ids.empty())
        return true;
    try
    {
        auto &db = context.db();
        for (const int64_t label_id : label_ids)
        {
            auto data = db(sqlpp::select(TagsTable.id, TagsTable.tagIds)
                               .from(TagsTable)
                               .where(TagsTable.labelId == label_id && TagsTable.type == detail::kLabelTagType));
            if (data.empty())
                continue;

            const auto &row        = data.front();
            detail::TagIds tag_ids = detail::decodeTagIds(row.tagIds);
            if (!detail::removeTagId(tag_ids, tag_id))
                continue;
            if (tag_ids.empty())
                db(sqlpp::remove_from(TagsTable).where(TagsTable.id == row.id));
            else
                db(sqlpp::update(TagsTable)
                       .set(TagsTable.tagIds = detail::encodeTagIds(tag_ids))
                       .where(TagsTable.id == row.id));
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool TagRepository::removeTagsForLabels(DatabaseContext &context, const std::vector<int64_t> &label_ids,
                                        QString &err_msg)
{
    if (label_ids.empty())
        return true;
    try
    {
        auto &db = context.db();
        db(sqlpp::remove_from(TagsTable).where(TagsTable.labelId.in(sqlpp::value_list(label_ids))
                                               && TagsTable.type == detail::kLabelTagType));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

} // namespace dltool::database
