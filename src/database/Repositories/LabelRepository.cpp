#include "LabelRepository.h"

#include "database/ddl/ImagesTable.h"
#include "database/ddl/TagClassesTable.h"
#include "database/ddl/TagsTable.h"
#include "database/ddl/LabelClassesTable.h"
#include "database/ddl/LabelsTable.h"

#include <sqlpp11/sqlpp11.h>

#include <set>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

namespace dltool::database {

namespace {
const auto ImagesTable       = Images{};
const auto LabelClassesTable = LabelClasses{};
const auto LabelsTable       = Labels{};
const auto TagsTable         = Tags{};
}

bool LabelRepository::getAllLabelClasses(DatabaseContext &context, std::vector<int64_t> &label_class_ids, std::vector<QString> &names,
                                         std::vector<QString> &colors, std::vector<QString> &shortcuts,
                                         std::vector<int64_t> &ordinal_indices,
                                         std::vector<std::vector<uint8_t>> &extra_data, QString &err_msg)
{
    try
    {
        auto &db = context.db();
        auto data = db(sqlpp::select(LabelClassesTable.id, LabelClassesTable.name, LabelClassesTable.color,
                                     LabelClassesTable.shortcut, LabelClassesTable.ordinalIndex,
                                     LabelClassesTable.extraData)
                           .from(LabelClassesTable)
                           .unconditionally());
        for (const auto &row : data)
        {
            label_class_ids.emplace_back(row.id);
            names.emplace_back(QString::fromStdString(row.name));
            colors.emplace_back(QString::fromStdString(row.color));
            shortcuts.emplace_back(QString::fromStdString(row.shortcut));
            ordinal_indices.emplace_back(row.ordinalIndex);
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

bool LabelRepository::labelClassIdsForDataset(DatabaseContext &context, const int64_t dataset_id, std::vector<int64_t> &label_class_ids,
                                              QString &err_msg)
{
    try
    {
        label_class_ids.clear();
        auto &db = context.db();

        // 实例标注的类别
        auto label_data = db(sqlpp::select(LabelsTable.labelClassId)
                                 .from(LabelsTable.join(ImagesTable).on(LabelsTable.imageId == ImagesTable.id))
                                 .where(ImagesTable.datasetId == dataset_id));
        std::set<int64_t> class_ids;
        for (const auto &row : label_data)
        {
            if (row.labelClassId >= 0)
                class_ids.insert(row.labelClassId);
        }

        // 图像级类别保存在 images.extra_data 的 image_label_class_id 字段
        auto image_data = db(sqlpp::select(ImagesTable.extraData)
                                 .from(ImagesTable)
                                 .where(ImagesTable.datasetId == dataset_id));
        for (const auto &row : image_data)
        {
            const std::vector<uint8_t> extra
                = row.extraData.is_null() ? std::vector<uint8_t>{} : row.extraData.value();
            if (extra.empty())
                continue;
            const QByteArray bytes(reinterpret_cast<const char *>(extra.data()),
                                   static_cast<qsizetype>(extra.size()));
            QJsonParseError parse_error;
            const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse_error);
            if (parse_error.error != QJsonParseError::NoError || !document.isObject())
                continue;
            const int64_t class_id = document.object()
                                         .value(QStringLiteral("image_label_class_id"))
                                         .toInteger(-1);
            if (class_id >= 0)
                class_ids.insert(class_id);
        }

        label_class_ids.assign(class_ids.begin(), class_ids.end());
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool LabelRepository::addLabelClass(DatabaseContext &context, const QString &name, const QString &color, const QString &shortcut,
                                    const int64_t ordinal_index, const std::vector<uint8_t> &extra_data,
                                    int64_t &label_class_id, QString &err_msg)
{
    try
    {
        auto &db = context.db();
        db(sqlpp::insert_into(LabelClassesTable)
               .set(LabelClassesTable.name         = name.toUtf8().constData(),
                    LabelClassesTable.color        = color.toUtf8().constData(),
                    LabelClassesTable.shortcut     = shortcut.toUtf8().constData(),
                    LabelClassesTable.ordinalIndex = ordinal_index,
                    LabelClassesTable.extraData    = extra_data));
        label_class_id = static_cast<int64_t>(db.last_insert_id());
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool LabelRepository::updateLabelClass(DatabaseContext &context, const int64_t label_class_id, const QString &name, const QString &color,
                                       const QString &shortcut, const int64_t ordinal_index,
                                       const std::vector<uint8_t> &extra_data, QString &err_msg)
{
    try
    {
        auto &db = context.db();
        db(sqlpp::update(LabelClassesTable)
               .set(LabelClassesTable.name         = name.toUtf8().constData(),
                    LabelClassesTable.color        = color.toUtf8().constData(),
                    LabelClassesTable.shortcut     = shortcut.toUtf8().constData(),
                    LabelClassesTable.ordinalIndex = ordinal_index,
                    LabelClassesTable.extraData    = extra_data)
               .where(LabelClassesTable.id == label_class_id));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool LabelRepository::updateLabelClass(DatabaseContext &context, const std::vector<int64_t> &label_class_ids,
                                       const std::vector<int64_t> &ordinal_indexes, QString &err_msg)
{
    try
    {
        if (label_class_ids.size() != ordinal_indexes.size())
        {
            err_msg = QString("标签类别ID数量与序号数量不一致");
            return false;
        }
        auto &db = context.db();
        for (size_t i = 0; i < label_class_ids.size(); ++i)
        {
            db(sqlpp::update(LabelClassesTable)
                   .set(LabelClassesTable.ordinalIndex = ordinal_indexes[i])
                   .where(LabelClassesTable.id == label_class_ids[i]));
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool LabelRepository::deleteLabelClass(DatabaseContext &context, const int64_t label_class_id, QString &err_msg)
{
    try
    {
        auto &db = context.db();
        db(sqlpp::remove_from(LabelClassesTable).where(LabelClassesTable.id == label_class_id));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool LabelRepository::deleteLabelClasses(DatabaseContext &context, const std::vector<int64_t> &label_class_ids, QString &err_msg)
{
    if (label_class_ids.empty())
    {
        return true;
    }

    try
    {

        auto &db = context.db();
            db(sqlpp::remove_from(LabelClassesTable)
                   .where(LabelClassesTable.id.in(sqlpp::value_list(label_class_ids))));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool LabelRepository::getAllLabels(DatabaseContext &context, std::vector<int64_t> &label_ids, std::vector<int64_t> &image_ids,
                                   std::vector<int64_t> &label_class_ids, std::vector<int64_t> &label_types,
                                   std::vector<std::vector<uint8_t>> &labels_data, QString &err_msg)
{
    try
    {
        auto &db = context.db();
        auto data = db(sqlpp::select(LabelsTable.id, LabelsTable.imageId, LabelsTable.labelClassId,
                                     LabelsTable.regionType, LabelsTable.region)
                           .from(LabelsTable)
                           .unconditionally());
        for (const auto &row : data)
        {
            label_ids.emplace_back(row.id);
            image_ids.emplace_back(row.imageId);
            label_class_ids.emplace_back(row.labelClassId);
            label_types.emplace_back(row.regionType);
            labels_data.emplace_back(row.region.value());
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool LabelRepository::getLabelsByImageIds(DatabaseContext &context, const std::vector<int64_t> &requested_image_ids,
                                         std::vector<int64_t>       &label_ids,
                                         std::vector<int64_t>       &image_ids,
                                         std::vector<int64_t>       &label_class_ids,
                                         std::vector<int64_t>       &label_types,
                                         std::vector<std::vector<uint8_t>> &labels_data,
                                         QString                    &err_msg)
{
    label_ids.clear();
    image_ids.clear();
    label_class_ids.clear();
    label_types.clear();
    labels_data.clear();
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
                = db(sqlpp::select(LabelsTable.id, LabelsTable.imageId, LabelsTable.labelClassId,
                                   LabelsTable.regionType, LabelsTable.region)
                         .from(LabelsTable)
                         .where(LabelsTable.imageId.in(sqlpp::value_list(batch))));
            for (const auto &row : data)
            {
                label_ids.emplace_back(row.id);
                image_ids.emplace_back(row.imageId);
                label_class_ids.emplace_back(row.labelClassId);
                label_types.emplace_back(row.regionType);
                labels_data.emplace_back(row.region.value());
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

bool LabelRepository::addLabels(DatabaseContext &context, const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_class_ids,
                                const std::vector<int64_t>              &label_types,
                                const std::vector<std::vector<uint8_t>> &labels_data, std::vector<int64_t> &label_ids,
                                QString &err_msg)
{
    label_ids.clear();

    if (image_ids.size() != label_class_ids.size() || image_ids.size() != label_types.size()
        || image_ids.size() != labels_data.size())
    {
        err_msg = QString("标注写入参数数量不一致: image_ids=%1, label_class_ids=%2, label_types=%3, labels=%4")
                      .arg(image_ids.size())
                      .arg(label_class_ids.size())
                      .arg(label_types.size())
                      .arg(labels_data.size());
        return false;
    }

    auto &db = context.db();
        auto prepared_insert
            = db.prepare(sqlpp::insert_into(LabelsTable)
                             .set(LabelsTable.imageId      = sqlpp::parameter(LabelsTable.imageId),
                                  LabelsTable.labelClassId = sqlpp::parameter(LabelsTable.labelClassId),
                                  LabelsTable.regionType   = sqlpp::parameter(LabelsTable.regionType),
                                  LabelsTable.region       = sqlpp::parameter(LabelsTable.region)));

        for (size_t i = 0; i < image_ids.size(); ++i)
        {
            prepared_insert.params.imageId      = image_ids[i];
            prepared_insert.params.labelClassId = label_class_ids[i];
            prepared_insert.params.regionType   = label_types[i];
            prepared_insert.params.region       = labels_data[i];
            db(prepared_insert);
            label_ids.emplace_back(static_cast<int64_t>(db.last_insert_id()));
        }
        return true;

}

bool LabelRepository::updateLabelsData(DatabaseContext &context, const std::vector<int64_t>              &label_ids,
                                       const std::vector<std::vector<uint8_t>> &labels_data, QString &err_msg)
{
    if (label_ids.size() != labels_data.size())
    {
        err_msg
            = QString("标注更新参数数量不一致: label_ids=%1, labels=%2").arg(label_ids.size()).arg(labels_data.size());
        return false;
    }

    auto &db = context.db();
        auto prepared_update = db.prepare(sqlpp::update(LabelsTable)
                                              .set(LabelsTable.region = sqlpp::parameter(LabelsTable.region))
                                              .where(LabelsTable.id == sqlpp::parameter(LabelsTable.id)));

        for (size_t i = 0; i < label_ids.size(); ++i)
        {
            prepared_update.params.region = labels_data[i];
            prepared_update.params.id     = label_ids[i];
            db(prepared_update);
        }
        return true;

}

bool LabelRepository::updateLabelsClass(DatabaseContext &context, const std::vector<int64_t> &label_ids,
                                        const std::vector<int64_t> &label_class_ids, QString &err_msg)
{
    auto &db = context.db();
        for (size_t i = 0; i < label_ids.size(); ++i)
        {
            db(sqlpp::update(LabelsTable)
                   .set(LabelsTable.labelClassId = label_class_ids[i])
                   .where(LabelsTable.id == label_ids[i]));
        }
        return true;

}

bool LabelRepository::deleteLabels(DatabaseContext &context, const std::vector<int64_t> &label_ids, QString &err_msg)
{
    if (label_ids.empty())
    {
        return true;
    }

    try
    {
        auto &db = context.db();
            db(sqlpp::remove_from(TagsTable).where(TagsTable.labelId.in(sqlpp::value_list(label_ids))));
            db(sqlpp::remove_from(LabelsTable).where(LabelsTable.id.in(sqlpp::value_list(label_ids))));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

} // namespace dltool::database
