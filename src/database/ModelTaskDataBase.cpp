#include "database/ModelTaskDataBase.h"

#include "DatabaseSqliteUtils.h"
#include "DatabaseValueUtils.h"
#include "database/ddl/PredictionTable.h"
#include "database/ddl/TaskInfoTable.h"

#include <sqlpp11/insert.h>
#include <sqlpp11/remove.h>
#include <sqlpp11/select.h>
#include <sqlpp11/order_by.h>
#include <sqlpp11/limit.h>
#include <sqlpp11/sqlpp11.h>
#include <sqlpp11/sqlite3/insert_or.h>
#include <sqlpp11/transaction.h>

#include <exception>
#include <tuple>

namespace dltool::database {

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

} // namespace

ModelTaskDataBase::ModelTaskDataBase(const QString &path, QObject *parent)
    : DataBase(path, parent)
{
    ensureSchema(nullptr);
}

ModelTaskDataBase::~ModelTaskDataBase() = default;

bool ModelTaskDataBase::ensureSchema(QString *err_msg) const
{
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    auto db = connectionPool()->get();
    return detail::ensureTaskSchema(db, err_msg);
}

bool ModelTaskDataBase::readTaskInfo(TaskInfoRecord &info, QString *err_msg) const
{
    info = {};
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    try
    {
        auto       db    = connectionPool()->get();
        const auto table = TaskInfo{};
        if (!detail::ensureTaskSchema(db, err_msg))
            return false;
        auto         rows = db(sqlpp::select(table.taskId, table.ctime, table.mtime)
                                 .from(table)
                                 .unconditionally()
                                 .limit(1U));
        if (rows.empty())
            return setError(err_msg, QString("task_info 为空"));
        const auto &row = rows.front();
        info.task_id    = QString::fromStdString(row.taskId).trimmed();
        info.ctime      = static_cast<qint64>(row.ctime);
        info.mtime      = static_cast<qint64>(row.mtime);
        if (info.task_id.isEmpty() || info.ctime < 0 || info.mtime < 0)
            return setError(err_msg, QString("task_info 字段无效"));
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QString("读取 task_info 失败"));
    }
}

bool ModelTaskDataBase::upsertTaskInfo(const TaskInfoRecord &info, QString *err_msg) const
{
    if (info.task_id.trimmed().isEmpty() || info.ctime < 0 || info.mtime < 0)
        return setError(err_msg, QString("task_info 字段无效"));
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    try
    {
        auto       db    = connectionPool()->get();
        const auto table = TaskInfo{};
        if (!detail::ensureTaskSchema(db, err_msg))
            return false;
        auto tx = sqlpp::start_transaction(db);
        db(sqlpp::remove_from(table).unconditionally());
        db(sqlpp::insert_into(table).set(table.taskId = info.task_id.trimmed().toStdString(),
                                         table.ctime  = static_cast<int64_t>(info.ctime),
                                         table.mtime  = static_cast<int64_t>(info.mtime)));
        tx.commit();
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QString("写入 task_info 失败"));
    }
}

bool ModelTaskDataBase::readTestParams(QVariantMap &params, QString *err_msg) const
{
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    auto db = connectionPool()->get();
    if (!detail::ensureTaskSchema(db, err_msg))
        return false;
    return detail::readParams(db, QStringLiteral("test_params"), params, err_msg);
}

bool ModelTaskDataBase::replaceTestParams(const QVariantMap &params, QString *err_msg) const
{
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    auto db = connectionPool()->get();
    if (!detail::ensureTaskSchema(db, err_msg))
        return false;
    return detail::replaceParams(db, QStringLiteral("test_params"), params, err_msg);
}

bool ModelTaskDataBase::readDatasets(QList<DatasetSelectionRecord> &selections, QString *err_msg) const
{
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    auto db = connectionPool()->get();
    if (!detail::ensureTaskSchema(db, err_msg))
        return false;
    return detail::readDatasets(db, selections, err_msg);
}

bool ModelTaskDataBase::replaceDatasets(const QList<DatasetSelectionRecord> &selections, QString *err_msg) const
{
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    auto db = connectionPool()->get();
    if (!detail::ensureTaskSchema(db, err_msg))
        return false;
    return detail::replaceDatasets(db, selections, err_msg);
}

bool ModelTaskDataBase::clearPredictions(QString *err_msg) const
{
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    try
    {
        auto       db    = connectionPool()->get();
        const auto table = Prediction{};
        if (!detail::ensureTaskSchema(db, err_msg))
            return false;
        db(sqlpp::remove_from(table).unconditionally());
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QString("清理预测结果失败"));
    }
}

bool ModelTaskDataBase::readPredictions(QHash<qint64, QVariant> &predictions, QString *err_msg) const
{
    predictions.clear();
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    try
    {
        auto       db    = connectionPool()->get();
        const auto table = Prediction{};
        if (!detail::ensureTaskSchema(db, err_msg))
            return false;
        auto         rows = db(sqlpp::select(table.imageId, table.data).from(table).unconditionally());
        for (const auto &row : rows)
        {
            const qint64 image_id = static_cast<qint64>(row.imageId);
            const std::string &encoded = row.data;
            QString value_error;
            const QVariant value = detail::jsonToVariant(
                QByteArray(encoded.data(), static_cast<int>(encoded.size())), &value_error);
            if (!value_error.isEmpty())
                return setError(err_msg, QString("读取图像 %1 的预测失败: %2").arg(image_id).arg(value_error));
            if (image_id < 0)
                return setError(err_msg, QString("prediction.image_id 无效"));
            predictions.insert(image_id, value);
        }
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QString("读取预测结果失败"));
    }
}

bool ModelTaskDataBase::upsertPrediction(const PredictionRecord &prediction, QString *err_msg) const
{
    if (prediction.image_id < 0)
        return setError(err_msg, QString("prediction.image_id 无效"));
    QString value_error;
    const QByteArray encoded = detail::variantToJson(prediction.data, &value_error);
    if (!value_error.isEmpty())
        return setError(err_msg, value_error);
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    try
    {
        auto       db    = connectionPool()->get();
        const auto table = Prediction{};
        if (!detail::ensureTaskSchema(db, err_msg))
            return false;
        db(sqlpp::sqlite3::insert_or_replace_into(table)
               .set(table.imageId = static_cast<int64_t>(prediction.image_id),
                    table.data    = encoded.toStdString()));
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QString("写入 prediction 失败"));
    }
}

} // namespace dltool::database

