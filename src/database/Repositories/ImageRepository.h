#pragma once

/**
 * @file ImageRepository.h
 * @brief data 模块私有：images 表访问仓储。
 *
 * 方法只做单表读写，事务由 ProjectDataBase 的共享上下文裁决。
 */

#include "DatabaseContext.h"

#include "database/DataBase.h"

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

    /// 单批把图像归属改到目标数据集；调用方负责分批与批间取消检查。SQL 异常向上传播。
    static void updateImagesDatasetId(DatabaseContext &context, const std::vector<int64_t> &image_ids,
                                      const int64_t target_dataset_id);

    /// 跨表插入图像快照：images 行 + 可选 tags 行 + 逐条 labels 行及其 tags 行。
    /// 返回新图像 id 与全部新标注 id。SQL 异常向上传播，由调用方事务边界裁决。
    static void insertImageSnapshot(DatabaseContext &context, const int64_t dataset_id,
                                    const ProjectDataBase::ImageSnapshot &image, int64_t &new_image_id,
                                    std::vector<int64_t> &new_label_ids);
};

} // namespace dltool::database
