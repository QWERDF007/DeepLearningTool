#include "ModelRepository.h"

#include "database/ddl/ModelsTable.h"

#include <sqlpp11/sqlpp11.h>

namespace dltool::database {

namespace {
const auto ModelsTable = Models{};
}

bool ModelRepository::getAllModels(DatabaseContext &context, std::vector<int64_t> &model_ids,
                                   std::vector<QString> &uuids, std::vector<QString> &names,
                                   std::vector<QString> &framework_names, std::vector<QString> &model_architectures,
                                   std::vector<qint64> &ctimes, std::vector<qint64> &mtimes,
                                   std::vector<std::vector<uint8_t>> &extra_data, QString &err_msg)
{
    try
    {
        model_ids.clear();
        uuids.clear();
        names.clear();
        framework_names.clear();
        model_architectures.clear();
        ctimes.clear();
        mtimes.clear();
        extra_data.clear();

        auto &db = context.db();
        auto data
            = db(sqlpp::select(ModelsTable.id, ModelsTable.uuid, ModelsTable.name, ModelsTable.frameworkName,
                               ModelsTable.modelArchitecture, ModelsTable.ctime, ModelsTable.mtime,
                               ModelsTable.extraData)
                     .from(ModelsTable)
                     .unconditionally()
                     .order_by(ModelsTable.id.asc()));
        for (const auto &row : data)
        {
            model_ids.emplace_back(row.id);
            uuids.emplace_back(QString::fromStdString(row.uuid));
            names.emplace_back(QString::fromStdString(row.name));
            framework_names.emplace_back(QString::fromStdString(row.frameworkName));
            model_architectures.emplace_back(QString::fromStdString(row.modelArchitecture));
            ctimes.emplace_back(row.ctime);
            mtimes.emplace_back(row.mtime);
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

bool ModelRepository::addModel(DatabaseContext &context, const QString &uuid, const QString &name,
                               const QString &framework_name, const QString &model_architecture, const qint64 ctime,
                               const qint64 mtime, int64_t &model_id, QString &err_msg)
{
    try
    {
        auto &db = context.db();

        const QByteArray uuid_bytes               = uuid.toUtf8();
        const QByteArray name_bytes               = name.toUtf8();
        const QByteArray framework_name_bytes     = framework_name.toUtf8();
        const QByteArray model_architecture_bytes = model_architecture.toUtf8();
        db(sqlpp::insert_into(ModelsTable)
               .set(ModelsTable.uuid = uuid_bytes.constData(), ModelsTable.name = name_bytes.constData(),
                    ModelsTable.frameworkName     = framework_name_bytes.constData(),
                    ModelsTable.modelArchitecture = model_architecture_bytes.constData(),
                    ModelsTable.ctime = ctime, ModelsTable.mtime = mtime));
        model_id = static_cast<int64_t>(db.last_insert_id());
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ModelRepository::updateModelName(DatabaseContext &context, const int64_t model_id, const QString &name,
                                      const qint64 mtime, QString &err_msg)
{
    try
    {
        auto &db           = context.db();
        const QByteArray name_bytes = name.toUtf8();
        db(sqlpp::update(ModelsTable)
               .set(ModelsTable.name = name_bytes.constData(), ModelsTable.mtime = mtime)
               .where(ModelsTable.id == model_id));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ModelRepository::updateModelExtraData(DatabaseContext &context, const int64_t model_id,
                                           const std::vector<uint8_t> &extra_data, QString &err_msg)
{
    try
    {
        auto &db = context.db();
        db(sqlpp::update(ModelsTable).set(ModelsTable.extraData = extra_data).where(ModelsTable.id == model_id));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ModelRepository::updateModelMtime(DatabaseContext &context, const int64_t model_id, const qint64 mtime,
                                       QString &err_msg)
{
    try
    {
        auto &db = context.db();
        db(sqlpp::update(ModelsTable).set(ModelsTable.mtime = mtime).where(ModelsTable.id == model_id));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ModelRepository::deleteModel(DatabaseContext &context, const int64_t model_id, QString &err_msg)
{
    try
    {
        auto &db = context.db();
        db(sqlpp::remove_from(ModelsTable).where(ModelsTable.id == model_id));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

} // namespace dltool::database
