#include "ImageRepository.h"

#include "database/ddl/ImagesTable.h"
#include "database/ddl/LabelsTable.h"
#include "database/ddl/TagsTable.h"

#include <sqlpp11/sqlpp11.h>

namespace dltool::database {

namespace {
const auto ImagesTable = Images{};
const auto LabelsTable = Labels{};
const auto TagsTable   = Tags{};
}

bool ImageRepository::addImages(DatabaseContext &context, const int64_t dataset_id, const std::vector<QString> &paths,
                                std::vector<int64_t> &image_ids, QString &err_msg)
{
    try
    {
        auto &db = context.db();
        // db(sqlpp::insert_into(DatasetsTable).set(DatasetsTable.name = name.toUtf8().constData()));
            for (const auto &path : paths)
            {
                db(sqlpp::insert_into(ImagesTable)
                       .set(ImagesTable.datasetId = dataset_id, ImagesTable.path = path.toUtf8().constData()));
                image_ids.emplace_back(static_cast<int64_t>(db.last_insert_id()));
            }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ImageRepository::addImages(DatabaseContext &context, const std::vector<int64_t> &dataset_ids, const std::vector<QString> &paths,
                                std::vector<int64_t> &image_ids, QString &err_msg)
{
    image_ids.clear();
    try
    {
        if (dataset_ids.size() != paths.size())
        {
            err_msg = QString("添加图像失败: 数据集 ID 和路径数量不一致");
            return false;
        }
        if (paths.empty())
        {
            return true;
        }

        auto &db = context.db();
            image_ids.reserve(paths.size());
            for (size_t i = 0; i < paths.size(); ++i)
            {
                db(sqlpp::insert_into(ImagesTable)
                       .set(ImagesTable.datasetId = dataset_ids[i], ImagesTable.path = paths[i].toUtf8().constData()));
                image_ids.emplace_back(static_cast<int64_t>(db.last_insert_id()));
            }

    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}


bool ImageRepository::updateImagesDataset(DatabaseContext &context, const std::vector<int64_t> &image_ids,
                                          const std::vector<int64_t> &dataset_ids, QString &err_msg)
{
    try
    {
        if (image_ids.size() != dataset_ids.size())
        {
            err_msg = QString("移动图像失败: 图像 ID 和数据集 ID 数量不一致");
            return false;
        }
        if (image_ids.empty())
        {
            return true;
        }

        std::map<int64_t, std::vector<int64_t>> image_ids_by_dataset;
        for (size_t i = 0; i < image_ids.size(); ++i)
        {
            image_ids_by_dataset[dataset_ids[i]].push_back(image_ids[i]);
        }

        auto &db = context.db();
            for (const auto &[dataset_id, ids] : image_ids_by_dataset)
            {
                db(sqlpp::update(ImagesTable)
                       .set(ImagesTable.datasetId = dataset_id)
                       .where(ImagesTable.id.in(sqlpp::value_list(ids))));
            }

    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ImageRepository::getImage(DatabaseContext &context, const int64_t image_id, std::pair<int64_t, QString> &image, QString &err_msg)
{
    try
    {
        auto &db = context.db();
        auto data = db(
            sqlpp::select(ImagesTable.datasetId, ImagesTable.path).from(ImagesTable).where(ImagesTable.id == image_id));
        if (!data.empty())
        {
            const auto &row = data.front();
            image.first     = row.datasetId;
            image.second    = QString::fromStdString(row.path);
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ImageRepository::deleteImages(DatabaseContext &context, const std::vector<int64_t> &image_ids, QString &err_msg)
{
    if (image_ids.empty())
    {
        return true;
    }

    try
    {
        auto &db = context.db();
            const auto labels_for_images
                = sqlpp::select(LabelsTable.id)
                      .from(LabelsTable)
                      .where(LabelsTable.imageId.in(sqlpp::value_list(image_ids)));

            db(sqlpp::remove_from(TagsTable).where(TagsTable.labelId.in(labels_for_images)));
            db(sqlpp::remove_from(TagsTable).where(TagsTable.imageId.in(sqlpp::value_list(image_ids))));
            db(sqlpp::remove_from(LabelsTable).where(LabelsTable.imageId.in(sqlpp::value_list(image_ids))));
            db(sqlpp::remove_from(ImagesTable).where(ImagesTable.id.in(sqlpp::value_list(image_ids))));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ImageRepository::getImages(DatabaseContext &context, const int64_t dataset_id, std::vector<int64_t> &image_ids, std::vector<QString> &paths,
                                QString &err_msg)
{
    try
    {
        auto &db = context.db();
        auto data = db(sqlpp::select(ImagesTable.id, ImagesTable.path)
                           .from(ImagesTable)
                           .where(ImagesTable.datasetId == dataset_id));

        for (const auto &row : data)
        {
            image_ids.emplace_back(row.id);
            paths.emplace_back(QString::fromStdString(row.path));
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ImageRepository::getAllImages(DatabaseContext &context, std::vector<int64_t> &dataset_ids, std::vector<int64_t> &image_ids,
                                   std::vector<QString> &paths, std::vector<std::vector<uint8_t>> &extra_data,
                                   QString &err_msg)
{
    try
    {
        auto &db = context.db();
        auto data = db(sqlpp::select(ImagesTable.id, ImagesTable.datasetId, ImagesTable.path, ImagesTable.extraData)
                           .from(ImagesTable)
                           .unconditionally());
        for (const auto &row : data)
        {
            dataset_ids.emplace_back(row.datasetId);
            image_ids.emplace_back(row.id);
            paths.emplace_back(QString::fromStdString(row.path));
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

bool ImageRepository::updateImagesExtraData(DatabaseContext &context, const std::vector<int64_t>              &image_ids,
                                            const std::vector<std::vector<uint8_t>> &extra_data,
                                            QString                                &err_msg)
{
    if (image_ids.size() != extra_data.size())
    {
        err_msg = QString("图像ID数量与扩展数据数量不一致");
        return false;
    }

    auto &db = context.db();
        auto prepared_update = db.prepare(sqlpp::update(ImagesTable)
                                              .set(ImagesTable.extraData = sqlpp::parameter(ImagesTable.extraData))
                                              .where(ImagesTable.id == sqlpp::parameter(ImagesTable.id)));
        for (size_t i = 0; i < image_ids.size(); ++i)
        {
            prepared_update.params.extraData = extra_data[i];
            prepared_update.params.id        = image_ids[i];
            db(prepared_update);
        }
        return true;

}

bool ImageRepository::getImagesByIds(DatabaseContext &context, const std::vector<int64_t> &requested_image_ids,
                                    std::vector<int64_t>       &dataset_ids,
                                    std::vector<int64_t>       &image_ids,
                                    std::vector<QString>       &paths,
                                    std::vector<std::vector<uint8_t>> &extra_data,
                                    QString                    &err_msg)
{
    dataset_ids.clear();
    image_ids.clear();
    paths.clear();
    extra_data.clear();
    if (requested_image_ids.empty())
        return true;

    for (const int64_t image_id : requested_image_ids)
    {
        if (image_id < 0)
        {
            err_msg = QString("图像 ID 无效: %1").arg(image_id);
            return false;
        }
    }

    try
    {
        auto &db = context.db();
        constexpr std::size_t kBatchSize = 500;
        for (std::size_t offset = 0; offset < requested_image_ids.size(); offset += kBatchSize)
        {
            const auto begin = requested_image_ids.begin() + static_cast<std::ptrdiff_t>(offset);
            const auto end   = requested_image_ids.begin()
                             + static_cast<std::ptrdiff_t>(std::min(offset + kBatchSize, requested_image_ids.size()));
            const std::vector<int64_t> batch(begin, end);
            auto data
                = db(sqlpp::select(ImagesTable.id, ImagesTable.datasetId, ImagesTable.path, ImagesTable.extraData)
                         .from(ImagesTable)
                         .where(ImagesTable.id.in(sqlpp::value_list(batch))));
            for (const auto &row : data)
            {
                dataset_ids.emplace_back(row.datasetId);
                image_ids.emplace_back(row.id);
                paths.emplace_back(QString::fromStdString(row.path));
                extra_data.emplace_back(row.extraData.is_null() ? std::vector<uint8_t>{} : row.extraData.value());
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

int64_t ImageRepository::getImagesCount(DatabaseContext &context, const int64_t dataset_id)
{
    try
    {
        auto &db = context.db();
        auto data = db(
            sqlpp::select(sqlpp::count(ImagesTable.id)).from(ImagesTable).where(ImagesTable.datasetId == dataset_id));
        return static_cast<int64_t>(data.front().count);
    }
    catch (const std::exception &)
    {
        return 0;
    }
}

} // namespace dltool::database
