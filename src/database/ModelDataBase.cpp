#include "database/ModelDataBase.h"

#include "DatabaseSqliteUtils.h"

#include <sqlite3.h>

namespace dltool::database {

using detail::SqliteConnection;
using detail::SqliteStatement;

namespace {

bool setError(QString *err_msg, const QString &message)
{
    if (err_msg != nullptr)
        *err_msg = message;
    return false;
}

QString sqliteError(sqlite3 *db, const QString &prefix)
{
    const QString detail = db != nullptr ? QString::fromUtf8(sqlite3_errmsg(db)) : QString("数据库句柄为空");
    return prefix + QStringLiteral(": ") + detail;
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
    SqliteConnection connection(path_, err_msg);
    return connection.isOpen() && detail::ensureModelSchema(connection.handle(), err_msg);
}

bool ModelDataBase::readTrainParams(QVariantMap &params, QString *err_msg) const
{
    SqliteConnection connection(path_, err_msg);
    if (!connection.isOpen() || !detail::ensureModelSchema(connection.handle(), err_msg))
        return false;
    return detail::readParams(connection.handle(), QStringLiteral("train_params"), params, err_msg);
}

bool ModelDataBase::replaceTrainParams(const QVariantMap &params, QString *err_msg) const
{
    SqliteConnection connection(path_, err_msg);
    if (!connection.isOpen() || !detail::ensureModelSchema(connection.handle(), err_msg))
        return false;
    return detail::replaceParams(connection.handle(), QStringLiteral("train_params"), params, err_msg);
}

bool ModelDataBase::readDatasets(QList<DatasetSelectionRecord> &selections, QString *err_msg) const
{
    SqliteConnection connection(path_, err_msg);
    if (!connection.isOpen() || !detail::ensureModelSchema(connection.handle(), err_msg))
        return false;
    return detail::readDatasets(connection.handle(), selections, err_msg);
}

bool ModelDataBase::replaceDatasets(const QList<DatasetSelectionRecord> &selections, QString *err_msg) const
{
    SqliteConnection connection(path_, err_msg);
    if (!connection.isOpen() || !detail::ensureModelSchema(connection.handle(), err_msg))
        return false;
    return detail::replaceDatasets(connection.handle(), selections, err_msg);
}

bool ModelDataBase::listTestTasks(QList<ModelTestTaskRecord> &tasks, QString *err_msg) const
{
    tasks.clear();
    SqliteConnection connection(path_, err_msg);
    if (!connection.isOpen() || !detail::ensureModelSchema(connection.handle(), err_msg))
        return false;

    SqliteStatement statement(connection.handle(),
                              QStringLiteral("SELECT task_id, name, ctime, mtime FROM test_tasks "
                                             "ORDER BY ctime, task_id"),
                              err_msg);
    if (!statement.isValid())
        return false;
    while (true)
    {
        const int rc = sqlite3_step(statement.handle());
        if (rc == SQLITE_DONE)
            break;
        if (rc != SQLITE_ROW)
            return setError(err_msg, sqliteError(connection.handle(), QString("读取测试任务索引失败")));

        ModelTestTaskRecord task;
        task.task_id = detail::columnText(statement.handle(), 0).trimmed();
        task.name = detail::columnText(statement.handle(), 1);
        task.ctime = sqlite3_column_int64(statement.handle(), 2);
        task.mtime = sqlite3_column_int64(statement.handle(), 3);
        if (task.task_id.isEmpty() || task.name.trimmed().isEmpty() || task.ctime < 0 || task.mtime < 0)
            return setError(err_msg, QString("测试任务索引包含无效记录"));
        tasks.push_back(task);
    }
    return true;
}

bool ModelDataBase::upsertTestTask(const ModelTestTaskRecord &task, QString *err_msg) const
{
    if (task.task_id.trimmed().isEmpty() || task.name.trimmed().isEmpty() || task.ctime < 0 || task.mtime < 0)
        return setError(err_msg, QString("测试任务索引记录无效"));

    SqliteConnection connection(path_, err_msg);
    if (!connection.isOpen() || !detail::ensureModelSchema(connection.handle(), err_msg))
        return false;
    SqliteStatement statement(connection.handle(),
                              QStringLiteral("INSERT INTO test_tasks (task_id, name, ctime, mtime) "
                                             "VALUES (?, ?, ?, ?) "
                                             "ON CONFLICT(task_id) DO UPDATE SET "
                                             "name=excluded.name, ctime=excluded.ctime, mtime=excluded.mtime"),
                              err_msg);
    if (!statement.isValid())
        return false;
    const QByteArray task_id = task.task_id.toUtf8();
    const QByteArray name = task.name.toUtf8();
    if (sqlite3_bind_text(statement.handle(), 1, task_id.constData(), task_id.size(), SQLITE_TRANSIENT) != SQLITE_OK
        || sqlite3_bind_text(statement.handle(), 2, name.constData(), name.size(), SQLITE_TRANSIENT) != SQLITE_OK
        || sqlite3_bind_int64(statement.handle(), 3, task.ctime) != SQLITE_OK
        || sqlite3_bind_int64(statement.handle(), 4, task.mtime) != SQLITE_OK
        || sqlite3_step(statement.handle()) != SQLITE_DONE)
        return setError(err_msg, sqliteError(connection.handle(), QString("写入测试任务索引失败")));
    return true;
}

bool ModelDataBase::removeTestTask(const QString &task_id, QString *err_msg) const
{
    if (task_id.trimmed().isEmpty())
        return setError(err_msg, QString("测试任务 ID 为空"));
    SqliteConnection connection(path_, err_msg);
    if (!connection.isOpen() || !detail::ensureModelSchema(connection.handle(), err_msg))
        return false;
    SqliteStatement statement(connection.handle(), QStringLiteral("DELETE FROM test_tasks WHERE task_id = ?"),
                              err_msg);
    if (!statement.isValid())
        return false;
    const QByteArray encoded = task_id.trimmed().toUtf8();
    if (sqlite3_bind_text(statement.handle(), 1, encoded.constData(), encoded.size(), SQLITE_TRANSIENT) != SQLITE_OK
        || sqlite3_step(statement.handle()) != SQLITE_DONE)
        return setError(err_msg, sqliteError(connection.handle(), QString("删除测试任务索引失败")));
    return true;
}

} // namespace dltool::database
