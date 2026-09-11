#pragma once

/**
 * @file ImageRepository.h
 * @brief data 模块私有：images 表访问仓储。
 *
 * 方法只做单表读写，事务由 ProjectDataBase 的共享上下文裁决。
 */

#include "DatabaseContext.h"

#include <QString>

#include <cstdint>
#include <utility>
#include <vector>

namespace dltool::database {

class ImageRepository
{
public:
    static bool addImages(DatabaseContext &context, const int64_t dataset_id, const std::vector<QString> &paths,
                                std::vector<int64_t> &image_ids, QString &err_msg);
    static bool addImages(DatabaseContext &context, const std::vector<int64_t> &dataset_ids, const std::vector<QString> &paths,
                                std::vector<int64_t> &image_ids, QString &err_msg);
    static bool updateImagesDataset(DatabaseContext &context, const std::vector<int64_t> &image_ids,
                                          const std::vector<int64_t> &dataset_ids, QString &err_msg);
    static bool getImage(DatabaseContext &context, const int64_t image_id, std::pair<int64_t, QString> &image, QString &err_msg);
    static bool deleteImages(DatabaseContext &context, const std::vector<int64_t> &image_ids, QString &err_msg);
    static bool getImages(DatabaseContext &context, const int64_t dataset_id, std::vector<int64_t> &image_ids, std::vector<QString> &paths,
                                QString &err_msg);
    static bool getAllImages(DatabaseContext &context, std::vector<int64_t> &dataset_ids, std::vector<int64_t> &image_ids,
                                   std::vector<QString> &paths, std::vector<std::vector<uint8_t>> &extra_data,
                                   QString &err_msg);
    static bool updateImagesExtraData(DatabaseContext &context, const std::vector<int64_t>              &image_ids,
                                            const std::vector<std::vector<uint8_t>> &extra_data,
                                            QString                                &err_msg);
    static bool getImagesByIds(DatabaseContext &context, const std::vector<int64_t> &requested_image_ids,
                                    std::vector<int64_t>       &dataset_ids,
                                    std::vector<int64_t>       &image_ids,
                                    std::vector<QString>       &paths,
                                    std::vector<std::vector<uint8_t>> &extra_data,
                                    QString                    &err_msg);
    static int64_t getImagesCount(DatabaseContext &context, const int64_t dataset_id);
};

} // namespace dltool::database
