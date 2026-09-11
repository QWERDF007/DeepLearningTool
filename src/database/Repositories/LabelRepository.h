#pragma once

/**
 * @file LabelRepository.h
 * @brief data 模块私有：labels/label_classes 表访问仓储。
 *
 * 方法只做单表读写，事务由 ProjectDataBase 的共享上下文裁决。
 */

#include "DatabaseContext.h"

#include <QString>

#include <cstdint>
#include <utility>
#include <vector>

namespace dltool::database {

class LabelRepository
{
public:
    static bool getAllLabelClasses(DatabaseContext &context, std::vector<int64_t> &label_class_ids, std::vector<QString> &names,
                                         std::vector<QString> &colors, std::vector<QString> &shortcuts,
                                         std::vector<int64_t> &ordinal_indices,
                                         std::vector<std::vector<uint8_t>> &extra_data, QString &err_msg);
    static bool labelClassIdsForDataset(DatabaseContext &context, const int64_t dataset_id, std::vector<int64_t> &label_class_ids,
                                              QString &err_msg);
    static bool addLabelClass(DatabaseContext &context, const QString &name, const QString &color, const QString &shortcut,
                                    const int64_t ordinal_index, const std::vector<uint8_t> &extra_data,
                                    int64_t &label_class_id, QString &err_msg);
    static bool updateLabelClass(DatabaseContext &context, const int64_t label_class_id, const QString &name, const QString &color,
                                       const QString &shortcut, const int64_t ordinal_index,
                                       const std::vector<uint8_t> &extra_data, QString &err_msg);
    static bool updateLabelClass(DatabaseContext &context, const std::vector<int64_t> &label_class_ids,
                                       const std::vector<int64_t> &ordinal_indexes, QString &err_msg);
    static bool deleteLabelClass(DatabaseContext &context, const int64_t label_class_id, QString &err_msg);
    static bool deleteLabelClasses(DatabaseContext &context, const std::vector<int64_t> &label_class_ids, QString &err_msg);
    static bool getAllLabels(DatabaseContext &context, std::vector<int64_t> &label_ids, std::vector<int64_t> &image_ids,
                                   std::vector<int64_t> &label_class_ids, std::vector<int64_t> &label_types,
                                   std::vector<std::vector<uint8_t>> &labels_data, QString &err_msg);
    static bool getLabelsByImageIds(DatabaseContext &context, const std::vector<int64_t> &requested_image_ids,
                                         std::vector<int64_t>       &label_ids,
                                         std::vector<int64_t>       &image_ids,
                                         std::vector<int64_t>       &label_class_ids,
                                         std::vector<int64_t>       &label_types,
                                         std::vector<std::vector<uint8_t>> &labels_data,
                                         QString                    &err_msg);
    static bool addLabels(DatabaseContext &context, const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_class_ids,
                                const std::vector<int64_t>              &label_types,
                                const std::vector<std::vector<uint8_t>> &labels_data, std::vector<int64_t> &label_ids,
                                QString &err_msg);
    static bool updateLabelsData(DatabaseContext &context, const std::vector<int64_t>              &label_ids,
                                       const std::vector<std::vector<uint8_t>> &labels_data, QString &err_msg);
    static bool updateLabelsClass(DatabaseContext &context, const std::vector<int64_t> &label_ids,
                                        const std::vector<int64_t> &label_class_ids, QString &err_msg);
    static bool deleteLabels(DatabaseContext &context, const std::vector<int64_t> &label_ids, QString &err_msg);
};

} // namespace dltool::database
