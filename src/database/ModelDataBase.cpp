#include "database/ModelDataBase.h"

#include "DatabaseSqliteUtils.h"
#include "database/ddl/TestTasksTable.h"

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

ModelDataBase::ModelDataBase(const QString &path, QObject *parent)
    : DataBase(path, parent)
{
    ensureSchema(nullptr);
}

ModelDataBase::~ModelDataBase() = default;

bool ModelDataBase::ensureSchema(QString *err_msg) const
{
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    auto db = connectionPool()->get();
    return detail::ensureModelSchema(db, err_msg);
}

bool ModelDataBase::readTrainParams(QVariantMap &params, QString *err_msg) const
{
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    auto db = connectionPool()->get();
    if (!detail::ensureModelSchema(db, err_msg))
        return false;
    return detail::readParams(db, QStringLiteral("train_params"), params, err_msg);
}

bool ModelDataBase::replaceTrainParams(const QVariantMap &params, QString *err_msg) const
{
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    auto db = connectionPool()->get();
    if (!detail::ensureModelSchema(db, err_msg))
        return false;
    return detail::replaceParams(db, QStringLiteral("train_params"), params, err_msg);
}

bool ModelDataBase::readDatasets(QList<DatasetSelectionRecord> &selections, QString *err_msg) const
{
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    auto db = connectionPool()->get();
    if (!detail::ensureModelSchema(db, err_msg))
        return false;
    return detail::readDatasets(db, selections, err_msg);
}

bool ModelDataBase::replaceDatasets(const QList<DatasetSelectionRecord> &selections, QString *err_msg) const
{
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    auto db = connectionPool()->get();
    if (!detail::ensureModelSchema(db, err_msg))
        return false;
    return detail::replaceDatasets(db, selections, err_msg);
}

bool ModelDataBase::listTestTasks(QList<ModelTestTaskRecord> &tasks, QString *err_msg) const
{
    tasks.clear();
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    try
    {
        auto       db    = connectionPool()->get();
        const auto table = TestTasks{};
        if (!detail::ensureModelSchema(db, err_msg))
            return false;
        auto rows = db(sqlpp::select(table.taskId, table.name, table.ctime, table.mtime).from(table).unconditionally());
        for (const auto &row : rows)
        {
            ModelTestTaskRecord task;
            task.task_id = QString::fromStdString(row.taskId).trimmed();
            task.name    = QString::fromStdString(row.name);
            task.ctime   = static_cast<qint64>(row.ctime);
            task.mtime   = static_cast<qint64>(row.mtime);
            if (task.task_id.isEmpty() || task.name.trimmed().isEmpty() || task.ctime < 0 || task.mtime < 0)
                return setError(err_msg, QString("测试任务索引包含无效记录"));
            tasks.push_back(task);
        }
        std::sort(tasks.begin(), tasks.end(),
                  [](const ModelTestTaskRecord &lhs, const ModelTestTaskRecord &rhs)
                  { return std::tie(lhs.ctime, lhs.task_id) < std::tie(rhs.ctime, rhs.task_id); });
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QString("读取测试任务索引失败"));
    }
}

bool ModelDataBase::upsertTestTask(const ModelTestTaskRecord &task, QString *err_msg) const
{
    if (task.task_id.trimmed().isEmpty() || task.name.trimmed().isEmpty() || task.ctime < 0 || task.mtime < 0)
        return setError(err_msg, QString("测试任务索引记录无效"));
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    try
    {
        auto       db    = connectionPool()->get();
        const auto table = TestTasks{};
        if (!detail::ensureModelSchema(db, err_msg))
            return false;
        db(sqlpp::sqlite3::insert_or_replace_into(table)
               .set(table.taskId = task.task_id.trimmed().toStdString(), table.name = task.name.toStdString(),
                    table.ctime = static_cast<int64_t>(task.ctime), table.mtime = static_cast<int64_t>(task.mtime)));
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QString("写入测试任务索引失败"));
    }
}

bool ModelDataBase::removeTestTask(const QString &task_id, QString *err_msg) const
{
    if (task_id.trimmed().isEmpty())
        return setError(err_msg, QString("测试任务 ID 为空"));
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    try
    {
        auto       db    = connectionPool()->get();
        const auto table = TestTasks{};
        if (!detail::ensureModelSchema(db, err_msg))
            return false;
        db(sqlpp::remove_from(table).where(table.taskId == task_id.trimmed().toStdString()));
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QString("删除测试任务索引失败"));
    }
}

} // namespace dltool::database

