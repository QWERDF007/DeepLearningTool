#include "DatabaseSqliteUtils.h"

#include "DatabaseValueUtils.h"
#include "database/SqlDef.h"
#include "database/ddl/ModelDatasetsTable.h"
#include "database/ddl/TestParamsTable.h"
#include "database/ddl/TrainParamsTable.h"

#include <sqlpp11/remove.h>
#include <sqlpp11/select.h>
#include <sqlpp11/order_by.h>
#include <sqlpp11/limit.h>
#include <sqlpp11/sqlpp11.h>
#include <sqlpp11/transaction.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QVariant>

#include <initializer_list>
#include <tuple>

namespace dltool::database::detail {

namespace {

bool setError(QString *err_msg, const QString &message)
{
    if (err_msg != nullptr)
        *err_msg = message;
    return false;
}

bool failFromException(QString *err_msg, const std::exception &error, const QString &prefix)
{
    return setError(err_msg, prefix + QStringLiteral(": ") + QString::fromUtf8(error.what()));
}

QByteArray classIdsJson(const QList<qint64> &class_ids)
{
    QJsonArray array;
    for (const qint64 class_id : class_ids)
        array.append(class_id);
    return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

QList<qint64> parseClassIds(const QByteArray &value)
{
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(value, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isArray())
        return {};

    QList<qint64> result;
    for (const QJsonValue &item : document.array())
    {
        bool ok = false;
        const qint64 class_id = item.toVariant().toLongLong(&ok);
        if (ok && class_id >= 0 && !result.contains(class_id))
            result.push_back(class_id);
    }
    return result;
}

bool runSchemaSqls(sqlpp::pooled_connection<sqlpp::sqlite3::connection_base> &db, const std::initializer_list<int> &types, QString *err_msg)
{
    try
    {
        const auto &sql_map = SqlDef::SqlMap;
        for (const int type : types)
        {
            const auto found = sql_map.find(type);
            if (found == sql_map.cend())
                return setError(err_msg, QString("缺少建表 SQL 定义"));
            db.execute(found->second);
        }
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QString("创建数据库表失败"));
    }
}

template <typename Table>
bool readParamsImpl(sqlpp::pooled_connection<sqlpp::sqlite3::connection_base> &db, QVariantMap &params, QString *err_msg)
{
    try
    {
        const Table table{};
        QList<ParamValue> values;
        for (const auto &row :
             db(sqlpp::select(table.group, table.nameEn, table.value, table.type).from(table).unconditionally()))
        {
            ParamValue entry;
            entry.group   = QString::fromStdString(row.group).trimmed();
            entry.name_en = QString::fromStdString(row.nameEn).trimmed();
            if (entry.group.isEmpty() || entry.name_en.isEmpty())
                return setError(err_msg, QString("数据库参数组或参数名为空"));
            QString value_error;
            const QVariant value
                = paramValueFromText(QString::fromStdString(row.type), QString::fromStdString(row.value), &value_error);
            if (!value_error.isEmpty())
                return setError(err_msg, QString("读取参数 '%1.%2' 失败: %3").arg(entry.group, entry.name_en, value_error));
            entry.value = value;
            values.push_back(entry);
        }
        params = unflattenParamValues(values);
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QString("读取参数失败"));
    }
}

template <typename Table>
bool replaceParamsImpl(sqlpp::pooled_connection<sqlpp::sqlite3::connection_base> &db, const QVariantMap &params, QString *err_msg)
{
    try
    {
        const Table table{};
        auto         tx = sqlpp::start_transaction(db);
        db(sqlpp::remove_from(table).unconditionally());
        for (const ParamValue &entry : flattenParamValues(params))
        {
            db(sqlpp::insert_into(table).set(table.group = entry.group.toStdString(),
                                             table.nameEn = entry.name_en.toStdString(),
                                             table.value  = paramValueText(entry.value).toStdString(),
                                             table.type   = paramValueType(entry.value).toStdString()));
        }
        tx.commit();
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QString("写入参数失败"));
    }
}

} // namespace

bool ensureModelSchema(sqlpp::pooled_connection<sqlpp::sqlite3::connection_base> &db, QString *err_msg)
{
    return runSchemaSqls(db, {SqlDef::CreateTrainParams, SqlDef::CreateModelDatasets, SqlDef::CreateTestTasks},
                         err_msg);
}

bool ensureTaskSchema(sqlpp::pooled_connection<sqlpp::sqlite3::connection_base> &db, QString *err_msg)
{
    return runSchemaSqls(db, {SqlDef::CreateTaskInfo, SqlDef::CreateTestParams, SqlDef::CreateModelDatasets,
                              SqlDef::CreatePrediction},
                         err_msg);
}

bool readParams(sqlpp::pooled_connection<sqlpp::sqlite3::connection_base> &db, const QString &table_name, QVariantMap &params, QString *err_msg)
{
    params.clear();
    if (table_name == QStringLiteral("train_params"))
        return readParamsImpl<TrainParams>(db, params, err_msg);
    if (table_name == QStringLiteral("test_params"))
        return readParamsImpl<TestParams>(db, params, err_msg);
    return setError(err_msg, QString("不支持的参数表"));
}

bool replaceParams(sqlpp::pooled_connection<sqlpp::sqlite3::connection_base> &db, const QString &table_name, const QVariantMap &params,
                   QString *err_msg)
{
    if (table_name == QStringLiteral("train_params"))
        return replaceParamsImpl<TrainParams>(db, params, err_msg);
    if (table_name == QStringLiteral("test_params"))
        return replaceParamsImpl<TestParams>(db, params, err_msg);
    return setError(err_msg, QString("不支持的参数表"));
}

bool readDatasets(sqlpp::pooled_connection<sqlpp::sqlite3::connection_base> &db, QList<DatasetSelectionRecord> &selections, QString *err_msg)
{
    selections.clear();
    try
    {
        const ModelDatasets table{};
        auto                rows = db(sqlpp::select(table.type, table.datasetId, table.classIds)
                                          .from(table)
                                          .unconditionally());
        for (const auto &row : rows)
        {
            DatasetSelectionRecord record;
            record.type       = QString::fromStdString(row.type);
            record.dataset_id = static_cast<qint64>(row.datasetId);
            record.class_ids = parseClassIds(QString::fromStdString(row.classIds).toUtf8());
            if (record.type.trimmed().isEmpty() || record.dataset_id < 0)
                return setError(err_msg, QString("数据库数据集选择字段无效"));
            selections.push_back(record);
        }
        std::sort(selections.begin(), selections.end(),
                  [](const DatasetSelectionRecord &lhs, const DatasetSelectionRecord &rhs)
                  {
                      return std::tie(lhs.type, lhs.dataset_id) < std::tie(rhs.type, rhs.dataset_id);
                  });
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QString("读取数据集选择失败"));
    }
}

bool replaceDatasets(sqlpp::pooled_connection<sqlpp::sqlite3::connection_base> &db, const QList<DatasetSelectionRecord> &selections,
                     QString *err_msg)
{
    try
    {
        const ModelDatasets table{};
        auto                tx = sqlpp::start_transaction(db);
        db(sqlpp::remove_from(table).unconditionally());
        for (const DatasetSelectionRecord &record : selections)
        {
            if (record.type.trimmed().isEmpty() || record.dataset_id < 0)
                return setError(err_msg, QString("数据集选择字段无效"));
            const QByteArray class_ids = classIdsJson(record.class_ids);
            db(sqlpp::insert_into(table).set(table.type      = record.type.toStdString(),
                                             table.datasetId = static_cast<int64_t>(record.dataset_id),
                                             table.classIds  = class_ids.toStdString()));
        }
        tx.commit();
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QString("写入数据集选择失败"));
    }
}

} // namespace dltool::database::detail


