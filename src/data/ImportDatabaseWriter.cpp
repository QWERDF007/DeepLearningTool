#include "data/ImportDatabaseWriter.h"

#include "core/CoreDef.h"
#include "data/DataFormat.h"
#include "data/DataNameUtils.h"
#include "data/DatasetIO.h"
#include "data/Images.h"
#include "data/LabelClasses.h"
#include "data/LabelData.h"
#include "database/ddl/ImagesTable.h"
#include "database/ddl/LabelClassesTable.h"
#include "database/ddl/LabelsTable.h"

#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include <sqlpp11/sqlite3/connection.h>
#include <sqlpp11/sqlpp11.h>
#include <sqlpp11/transaction.h>

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QSize>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace dltool::data {

struct ImportDatabaseWriter::Impl
{
    ImportDatabaseWriter *q{nullptr};
    QString database_path;
    int method{-1};
    int data_format{-1};
    int64_t dataset_id{-1};
    std::map<QString, QString> label_class_group_map;
    QPointer<DataIO> importer;

    std::unique_ptr<sqlpp::sqlite3::connection> db;
    std::unique_ptr<sqlpp::transaction_t<sqlpp::sqlite3::connection>> tx;

    struct ClassCacheEntry
    {
        int64_t id{-1};
        QString group;
        ClassCacheEntry() = default;
        ClassCacheEntry(int64_t i, QString g) : id(i), group(std::move(g)) {}
    };
    std::map<QString, ClassCacheEntry> label_class_map;
    int64_t max_ordinal_index{-1};

    std::map<QString, int64_t> normalized_image_path_to_id;
    std::map<QString, int64_t> image_path_to_id;
    std::map<int64_t, QSize> image_dimensions;
    std::map<int64_t, int64_t> folder_class_by_image_id;
    std::map<int64_t, int64_t> first_polygon_class_by_image_id;
    std::map<int64_t, int64_t> first_anomaly_polygon_class_by_image_id;

    std::unique_ptr<LabelDataHelper_t> label_data_helper;

    Stats stats;
    bool failed{false};
    QString first_error_message;
    bool finished_emitted{false};

    bool ensureConnected(QString &err_msg);
    int64_t ensureLabelClass(const QString &label_name, const QString &color, QString &err_msg);
    void handleDataBatchReady(int64_t batch_dataset_id, std::vector<QString> image_paths,
                              std::vector<int64_t> image_widths, std::vector<int64_t> image_heights,
                              std::map<QString, QString> label_class_info,
                              std::vector<dltool::data::ImportedLabel> labels, int64_t processed_images,
                              int64_t total_images);
    void handleImporterFinished(bool success, std::vector<int64_t> image_ids,
                                std::vector<int64_t> label_class_ids);
};

bool ImportDatabaseWriter::Impl::ensureConnected(QString &err_msg)
{
    if (db != nullptr)
    {
        return true;
    }

    try
    {
        auto config              = std::make_shared<sqlpp::sqlite3::connection_config>();
        config->path_to_database = database_path.toUtf8().constData();
        config->flags            = SQLITE_OPEN_READWRITE;
        db                       = std::make_unique<sqlpp::sqlite3::connection>(config);
        sqlite3_busy_timeout(db->native_handle(), 30000);

        const auto LabelClassesTable = dltool::database::LabelClasses{};
        auto classes_rows = (*db)(sqlpp::select(LabelClassesTable.id, LabelClassesTable.name,
                                                LabelClassesTable.ordinalIndex, LabelClassesTable.extraData)
                                      .from(LabelClassesTable)
                                      .unconditionally());
        for (const auto &row : classes_rows)
        {
            const QString name  = QString::fromStdString(row.name);
            const QString group = groupFromExtraData(row.extraData.is_null() ? std::vector<uint8_t>{}
                                                                             : row.extraData.value());
            label_class_map[name] = ClassCacheEntry(row.id, group);
            max_ordinal_index     = std::max(max_ordinal_index, static_cast<int64_t>(row.ordinalIndex));
        }

        const auto ImagesTable = dltool::database::Images{};
        auto images_rows       = (*db)(sqlpp::select(ImagesTable.id, ImagesTable.path, ImagesTable.extraData)
                                      .from(ImagesTable)
                                      .where(ImagesTable.datasetId == dataset_id));
        for (const auto &row : images_rows)
        {
            const QString path      = QString::fromStdString(row.path);
            const QString norm_path = normalizedImagePath(path);
            normalized_image_path_to_id[norm_path] = row.id;
            image_path_to_id[path]                 = row.id;
        }

        label_data_helper = createLabelDataHelper(method);
        tx                = std::make_unique<sqlpp::transaction_t<sqlpp::sqlite3::connection>>(
            sqlpp::start_transaction(*db, sqlpp::quiet_auto_rollback));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = QString("无法打开数据库或启动导入事务: %1").arg(e.what());
        db.reset();
        return false;
    }
}

int64_t ImportDatabaseWriter::Impl::ensureLabelClass(const QString &label_name, const QString &color, QString &err_msg)
{
    if (label_name.isEmpty())
    {
        return -1;
    }

    const bool anomaly_project = (method == core::DeepLearningMethod::AnomalyDetection);

    auto cached_it = label_class_map.find(label_name);
    if (cached_it != label_class_map.end())
    {
        const int64_t label_class_id = cached_it->second.id;
        if (label_class_id < 0)
        {
            return -1;
        }

        if (anomaly_project)
        {
            auto group_it = label_class_group_map.find(label_name);
            if (group_it != label_class_group_map.end())
            {
                const QString resolved_group = normalizeLabelClassGroup(group_it->second);
                if (!resolved_group.isEmpty() && cached_it->second.group != resolved_group)
                {
                    const auto LabelClassesTable = dltool::database::LabelClasses{};
                    const std::vector<uint8_t> extra_data_blob = extraDataForGroup(resolved_group);
                    try
                    {
                        (*db)(sqlpp::update(LabelClassesTable)
                                  .set(LabelClassesTable.extraData = extra_data_blob)
                                  .where(LabelClassesTable.id == label_class_id));
                        cached_it->second.group = resolved_group;
                    }
                    catch (const std::exception &e)
                    {
                        err_msg = QString("更新类别属性失败: %1").arg(e.what());
                        return -1;
                    }
                }
            }
        }
        return label_class_id;
    }

    int64_t ordinal_index = ++max_ordinal_index;
    QString chosen_color  = color;
    if (chosen_color.isEmpty())
    {
        chosen_color = DatasetIO::generateDefaultColor(static_cast<int>(label_class_map.size()));
    }

    QString group;
    if (anomaly_project)
    {
        auto group_it = label_class_group_map.find(label_name);
        if (group_it != label_class_group_map.end())
        {
            group = normalizeLabelClassGroup(group_it->second);
        }
        if (group.isEmpty())
        {
            group = anomalyLabelClassGroup();
        }
    }
    const std::vector<uint8_t> extra_data_blob = extraDataForGroup(group);

    const auto LabelClassesTable = dltool::database::LabelClasses{};
    try
    {
        (*db)(sqlpp::insert_into(LabelClassesTable)
                  .set(LabelClassesTable.name         = label_name.toStdString(),
                       LabelClassesTable.color        = chosen_color.toStdString(),
                       LabelClassesTable.shortcut     = std::string{},
                       LabelClassesTable.ordinalIndex = ordinal_index,
                       LabelClassesTable.extraData    = extra_data_blob));
    }
    catch (const std::exception &e)
    {
        err_msg = QString("插入新类别失败: %1").arg(e.what());
        return -1;
    }

    const int64_t label_class_id = db->last_insert_id();
    label_class_map[label_name]  = ClassCacheEntry(label_class_id, group);
    return label_class_id;
}

void ImportDatabaseWriter::Impl::handleDataBatchReady(
    int64_t batch_dataset_id, std::vector<QString> image_paths, std::vector<int64_t> image_widths,
    std::vector<int64_t> image_heights, std::map<QString, QString> label_class_info,
    std::vector<dltool::data::ImportedLabel> labels, int64_t processed_images, int64_t total_images)
{
    Q_UNUSED(processed_images);
    Q_UNUSED(total_images);

    if (failed)
    {
        return;
    }

    QString err_msg;
    if (!ensureConnected(err_msg))
    {
        failed = true;
        first_error_message = err_msg;
        return;
    }

    try
    {
        const auto ImagesTable = dltool::database::Images{};
        const auto LabelsTable = dltool::database::Labels{};

        const bool anomaly_project = (method == core::DeepLearningMethod::AnomalyDetection);
        const bool classification_project = (method == core::DeepLearningMethod::Classification);
        const bool folder_import = (data_format == DataFormat::Folder);

        // 1. 类别处理
        for (const auto &[name, color] : label_class_info)
        {
            if (ensureLabelClass(name, color, err_msg) < 0)
            {
                failed = true;
                first_error_message = err_msg;
                return;
            }
        }

        // 2. 图像批处理插入
        for (size_t i = 0; i < image_paths.size(); ++i)
        {
            const QString &path      = image_paths[i];
            const QString  norm_path = normalizedImagePath(path);

            int64_t image_id = -1;
            auto    cached_it = normalized_image_path_to_id.find(norm_path);
            if (cached_it != normalized_image_path_to_id.end())
            {
                image_id = cached_it->second;
            }
            else
            {
                (*db)(sqlpp::insert_into(ImagesTable)
                          .set(ImagesTable.datasetId = batch_dataset_id,
                               ImagesTable.path      = path.toStdString(),
                               ImagesTable.extraData = std::vector<uint8_t>{}));
                image_id = db->last_insert_id();
                normalized_image_path_to_id[norm_path] = image_id;
                image_path_to_id[path]                 = image_id;
                stats.imported_images++;
            }

            const int64_t width  = (i < image_widths.size()) ? image_widths[i] : -1;
            const int64_t height = (i < image_heights.size()) ? image_heights[i] : -1;
            if (width > 0 && height > 0)
            {
                image_dimensions[image_id] = QSize(static_cast<int>(width), static_cast<int>(height));
            }
        }

        // 3. 分类/异常检测文件夹级标签预解析
        if (folder_import && (classification_project || anomaly_project))
        {
            for (const auto &path : image_paths)
            {
                int64_t image_id = -1;
                auto id_it = image_path_to_id.find(path);
                if (id_it != image_path_to_id.end())
                {
                    image_id = id_it->second;
                }
                else
                {
                    auto norm_it = normalized_image_path_to_id.find(normalizedImagePath(path));
                    if (norm_it != normalized_image_path_to_id.end())
                        image_id = norm_it->second;
                }

                if (image_id >= 0 && folder_class_by_image_id.find(image_id) == folder_class_by_image_id.end())
                {
                    const QFileInfo file_info(path);
                    const QString   parent_dir_name = file_info.dir().dirName();
                    const int64_t   class_id = ensureLabelClass(parent_dir_name, QString{}, err_msg);
                    if (class_id >= 0)
                    {
                        folder_class_by_image_id[image_id] = class_id;
                    }
                }
            }
        }

        // 4. 标注插入
        if (!folder_import)
        {
            for (const auto &label : labels)
            {
                if (label.data.isEmpty())
                {
                    stats.skipped_labels++;
                    continue;
                }

                int64_t image_id = -1;
                auto image_it = image_path_to_id.find(label.image_path);
                if (image_it != image_path_to_id.end())
                {
                    image_id = image_it->second;
                }
                else
                {
                    auto norm_it = normalized_image_path_to_id.find(normalizedImagePath(label.image_path));
                    if (norm_it != normalized_image_path_to_id.end())
                    {
                        image_id = norm_it->second;
                    }
                }

                if (image_id < 0)
                {
                    stats.skipped_labels++;
                    continue;
                }

                const int64_t label_class_id = ensureLabelClass(label.label_class_name, QString{}, err_msg);
                if (label_class_id < 0)
                {
                    failed = true;
                    first_error_message = err_msg;
                    return;
                }

                if (first_polygon_class_by_image_id.find(image_id) == first_polygon_class_by_image_id.end())
                {
                    first_polygon_class_by_image_id[image_id] = label_class_id;
                }
                auto class_cache_it = label_class_map.find(label.label_class_name);
                if (class_cache_it != label_class_map.end() && class_cache_it->second.group != goodLabelClassGroup())
                {
                    if (first_anomaly_polygon_class_by_image_id.find(image_id) == first_anomaly_polygon_class_by_image_id.end())
                    {
                        first_anomaly_polygon_class_by_image_id[image_id] = label_class_id;
                    }
                }

                if (label_data_helper != nullptr)
                {
                    auto label_data = label_data_helper->createLabelData();
                    if (label_data != nullptr)
                    {
                        auto dim_it = image_dimensions.find(image_id);
                        if (dim_it == image_dimensions.end())
                        {
                            QImageReader reader(label.image_path);
                            const QSize  size = reader.size();
                            dim_it            = image_dimensions.emplace(image_id, size).first;
                        }
                        const QRectF rect(0, 0, dim_it->second.width(), dim_it->second.height());
                        label_data->fromQVariantMap(label.data, rect);
                        const int64_t              region_type = label_data->type();
                        const std::vector<uint8_t> region_blob = label_data->toBlob();

                        (*db)(sqlpp::insert_into(LabelsTable)
                                   .set(LabelsTable.imageId      = image_id,
                                        LabelsTable.labelClassId = label_class_id,
                                        LabelsTable.regionType   = region_type,
                                        LabelsTable.region       = region_blob));

                        stats.imported_labels++;
                    }
                }
            }
        }

        // 5. 分类/异常检测：更新图像的类别关联到 Images.extra_data
        if (folder_import && classification_project)
        {
            for (const auto &[image_id, class_id] : folder_class_by_image_id)
            {
                const std::string json_str = QString("{\"image_label_class_id\":%1,\"class_id\":%1}").arg(class_id).toStdString();
                const std::vector<uint8_t> blob(json_str.begin(), json_str.end());
                (*db)(sqlpp::update(ImagesTable)
                          .set(ImagesTable.extraData = blob)
                          .where(ImagesTable.id == image_id));
            }
        }
        else if (anomaly_project)
        {
            for (const auto &[image_id, class_id] : folder_class_by_image_id)
            {
                int64_t effective_class_id = class_id;
                auto it_poly = first_anomaly_polygon_class_by_image_id.find(image_id);
                if (it_poly != first_anomaly_polygon_class_by_image_id.end())
                {
                    effective_class_id = it_poly->second;
                }
                else
                {
                    auto it_first = first_polygon_class_by_image_id.find(image_id);
                    if (it_first != first_polygon_class_by_image_id.end())
                    {
                        effective_class_id = it_first->second;
                    }
                }
                const std::string json_str = QString("{\"image_label_class_id\":%1,\"class_id\":%1}").arg(effective_class_id).toStdString();
                const std::vector<uint8_t> blob(json_str.begin(), json_str.end());
                (*db)(sqlpp::update(ImagesTable)
                          .set(ImagesTable.extraData = blob)
                          .where(ImagesTable.id == image_id));
            }
        }
    }
    catch (const std::exception &e)
    {
        failed = true;
        first_error_message = QString("写入导入数据失败: %1").arg(e.what());
    }
}

void ImportDatabaseWriter::Impl::handleImporterFinished(bool success, std::vector<int64_t> /*image_ids*/,
                                                        std::vector<int64_t> /*label_class_ids*/)
{
    if (finished_emitted)
    {
        return;
    }
    finished_emitted = true;

    const bool is_cancelled = (importer != nullptr && importer->isCancelRequested());
    if (!success || failed || is_cancelled)
    {
        if (tx != nullptr)
        {
            try
            {
                tx->rollback();
            }
            catch (const std::exception &e)
            {
                spdlog::warn("导入事务回滚异常: {}", e.what());
            }
            tx.reset();
        }

        QString message;
        if (is_cancelled)
        {
            message = "导入已取消，所有更改已回滚";
        }
        else if (!first_error_message.isEmpty())
        {
            message = QString("导入失败: %1，所有更改已回滚").arg(first_error_message);
        }
        else
        {
            message = "导入失败，所有更改已回滚";
        }

        emit q->finished(false, message, stats);
        return;
    }

    if (tx != nullptr)
    {
        try
        {
            tx->commit();
            tx.reset();
        }
        catch (const std::exception &e)
        {
            first_error_message = QString("提交导入事务失败: %1").arg(e.what());
            try
            {
                tx->rollback();
            }
            catch (...)
            {
            }
            tx.reset();
            emit q->finished(false, first_error_message, stats);
            return;
        }
    }

    const QString message = QString("导入完成: 新增图片 %1 张, 新增标注 %2 条, 跳过标注 %3 条")
                                .arg(stats.imported_images)
                                .arg(stats.imported_labels)
                                .arg(stats.skipped_labels);
    emit q->finished(true, message, stats);
}

ImportDatabaseWriter::ImportDatabaseWriter(QString database_path, const int method, const int data_format,
                                           const int64_t dataset_id, std::map<QString, QString> label_class_groups,
                                           QPointer<DataIO> importer, QObject *parent)
    : QObject(parent)
    , impl_(std::make_unique<Impl>())
{
    impl_->q = this;
    impl_->database_path = std::move(database_path);
    impl_->method = method;
    impl_->data_format = data_format;
    impl_->dataset_id = dataset_id;
    impl_->label_class_group_map = std::move(label_class_groups);
    impl_->importer = std::move(importer);
}

ImportDatabaseWriter::~ImportDatabaseWriter()
{
    if (impl_ && impl_->tx != nullptr)
    {
        try
        {
            impl_->tx->rollback();
        }
        catch (...)
        {
        }
        impl_->tx.reset();
    }
}

void ImportDatabaseWriter::onDataBatchReady(int64_t dataset_id, std::vector<QString> image_paths,
                                            std::vector<int64_t> image_widths, std::vector<int64_t> image_heights,
                                            std::map<QString, QString> label_class_info,
                                            std::vector<dltool::data::ImportedLabel> labels,
                                            int64_t processed_images, int64_t total_images)
{
    impl_->handleDataBatchReady(dataset_id, std::move(image_paths), std::move(image_widths),
                                std::move(image_heights), std::move(label_class_info),
                                std::move(labels), processed_images, total_images);
}

void ImportDatabaseWriter::onImporterFinished(bool success, std::vector<int64_t> image_ids,
                                              std::vector<int64_t> label_class_ids)
{
    impl_->handleImporterFinished(success, std::move(image_ids), std::move(label_class_ids));
}

} // namespace dltool::data
