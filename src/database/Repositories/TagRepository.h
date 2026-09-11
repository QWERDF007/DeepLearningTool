#pragma once

/**
 * @file TagRepository.h
 * @brief data 模块私有：tag_classes 与 tags 表访问仓储。
 *
 * 方法只做单表读写与关系集合编码，事务由 ProjectDataBase 的共享
 * 上下文裁决；跨实体的 tag 关系写入由原子工作流组合本仓储完成。
 */

#include "DatabaseContext.h"

#include <QString>

#include <cstdint>
#include <vector>

namespace dltool::database {

class TagRepository
{
public:
    static bool getAllTagClasses(DatabaseContext &context, std::vector<int64_t> &tag_class_ids,
                                 std::vector<QString> &names, std::vector<std::vector<uint8_t>> &extra_data,
                                 QString &err_msg);

    static bool addTagClass(DatabaseContext &context, const QString &name, const std::vector<uint8_t> &extra_data,
                            int64_t &tag_class_id, QString &err_msg);

    static bool updateTagClass(DatabaseContext &context, const int64_t tag_class_id, const QString &name,
                               const std::vector<uint8_t> &extra_data, QString &err_msg);

    /// 删除 tag 类别并同步移除 tags 关系行中的对应 ID（调用方负责事务）。
    static bool deleteTagClass(DatabaseContext &context, const int64_t tag_class_id, QString &err_msg);

    static bool getAllTags(DatabaseContext &context, std::vector<int64_t> &image_ids,
                           std::vector<std::vector<int64_t>> &image_tag_ids, std::vector<int64_t> &label_ids,
                           std::vector<std::vector<int64_t>> &label_tag_ids, QString &err_msg);

    static bool addTagsToImages(DatabaseContext &context, const std::vector<int64_t> &image_ids,
                                const int64_t tag_id, QString &err_msg);

    static bool removeTagsFromImages(DatabaseContext &context, const std::vector<int64_t> &image_ids,
                                     const int64_t tag_id, QString &err_msg);

    static bool removeTagsForImages(DatabaseContext &context, const std::vector<int64_t> &image_ids,
                                    QString &err_msg);

    static bool addTagsToLabels(DatabaseContext &context, const std::vector<int64_t> &label_ids,
                                const int64_t tag_id, QString &err_msg);

    static bool removeTagsFromLabels(DatabaseContext &context, const std::vector<int64_t> &label_ids,
                                     const int64_t tag_id, QString &err_msg);

    static bool removeTagsForLabels(DatabaseContext &context, const std::vector<int64_t> &label_ids,
                                    QString &err_msg);
};

} // namespace dltool::database
