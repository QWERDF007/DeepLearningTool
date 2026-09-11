#include "DatasetRepository.h"

#include "database/ddl/DatasetsTable.h"
#include "database/ddl/ImagesTable.h"
#include "database/ddl/LabelsTable.h"
#include "database/ddl/TagClassesTable.h"
#include "database/ddl/TagsTable.h"

#include <sqlpp11/sqlpp11.h>

#include <algorithm>

namespace dltool::database {

namespace {
const auto DatasetsTable = Datasets{};
const auto ImagesTable   = Images{};
const auto LabelsTable   = Labels{};
const auto TagsTable     = Tags{};
} // namespace

bool DatasetRepository::getAllDatasets(DatabaseContext &context, std::vector<int64_t> &dataset_ids,
                                       std::vector<QString> &names, QString &err_msg)
{
    try
    {
        auto &db   = context.db();
        auto  data = db(sqlpp::select(DatasetsTable.id, DatasetsTable.name).from(DatasetsTable).unconditionally());
        for (const auto &row : data)
        {
            dataset_ids.emplace_back(row.id);
            names.emplace_back(QString::fromStdString(row.name));
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool DatasetRepository::addDataset(DatabaseContext &context, const QString &name, int64_t &dataset_id,
                                   QString &err_msg)
{
    try
    {
        auto &db = context.db();
        db(sqlpp::insert_into(DatasetsTable).set(DatasetsTable.name = name.toUtf8().constData()));
        dataset_id = static_cast<int64_t>(db.last_insert_id());
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool DatasetRepository::addDatasets(DatabaseContext &context, const std::vector<QString> &names,
                                    std::vector<int64_t> &dataset_ids, QString &err_msg)
{
    dataset_ids.clear();
    if (names.empty())
        return true;
    try
    {
        auto &db = context.db();
        dataset_ids.reserve(names.size());
        for (const QString &name : names)
        {
            db(sqlpp::insert_into(DatasetsTable).set(DatasetsTable.name = name.toUtf8().constData()));
            dataset_ids.emplace_back(static_cast<int64_t>(db.last_insert_id()));
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        dataset_ids.clear();
        return false;
    }
}

bool DatasetRepository::updateDataset(DatabaseContext &context, const int64_t dataset_id, const QString &name,
                                      QString &err_msg)
{
    try
    {
        auto &db = context.db();
        db(sqlpp::update(DatasetsTable)
               .set(DatasetsTable.name = name.toUtf8().constData())
               .where(DatasetsTable.id == dataset_id));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool DatasetRepository::deleteDataset(DatabaseContext &context, const int64_t dataset_id, QString &err_msg)
{
    try
    {
        auto &db = context.db();
        db(sqlpp::remove_from(DatasetsTable).where(DatasetsTable.id == dataset_id));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool DatasetRepository::deleteDatasetsWithContents(DatabaseContext &context, const std::vector<int64_t> &dataset_ids,
                                                   QString &err_msg)
{
    if (dataset_ids.empty())
        return true;
    try
    {
        std::vector<int64_t> unique_dataset_ids = dataset_ids;
        std::sort(unique_dataset_ids.begin(), unique_dataset_ids.end());
        unique_dataset_ids.erase(std::unique(unique_dataset_ids.begin(), unique_dataset_ids.end()),
                                 unique_dataset_ids.end());

        auto &db = context.db();
        // Use a subquery instead of expanding every image ID in the caller.  Apart from
        // being much smaller, this keeps all dependent-row cleanup in one SQLite transaction.
        const auto images_in_datasets
            = sqlpp::select(ImagesTable.id).from(ImagesTable).where(ImagesTable.datasetId.in(
                sqlpp::value_list(unique_dataset_ids)));

        const auto labels_in_datasets
            = sqlpp::select(LabelsTable.id).from(LabelsTable).where(LabelsTable.imageId.in(images_in_datasets));

        db(sqlpp::remove_from(TagsTable).where(TagsTable.labelId.in(labels_in_datasets)));
        db(sqlpp::remove_from(TagsTable).where(TagsTable.imageId.in(images_in_datasets)));
        db(sqlpp::remove_from(LabelsTable).where(LabelsTable.imageId.in(images_in_datasets)));
        db(sqlpp::remove_from(ImagesTable).where(ImagesTable.datasetId.in(sqlpp::value_list(unique_dataset_ids))));
        db(sqlpp::remove_from(DatasetsTable).where(DatasetsTable.id.in(sqlpp::value_list(unique_dataset_ids))));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

} // namespace dltool::database
