#include "database/ModelTaskDataBase.h"

#include "DatabaseSqliteUtils.h"
#include "DatabaseValueUtils.h"

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

ModelTaskDataBase::ModelTaskDataBase(const QString &path, QObject *parent)
    : DataBase(path, parent)
{
    ensureSchema(nullptr);
}

ModelTaskDataBase::~ModelTaskDataBase() = default;

bool ModelTaskDataBase::ensureSchema(QString *err_msg) const
{
    SqliteConnection connection(path_, err_msg);
    return connection.isOpen() && detail::ensureTaskSchema(connection.handle(), err_msg);
}

bool ModelTaskDataBase::readTaskInfo(TaskInfoRecord &info, QString *err_msg) const
{
    info = {};
    SqliteConnection connection(path_, err_msg);
    if (!connection.isOpen() || !detail::ensureTaskSchema(connection.handle(), err_msg))
        return false;

    SqliteStatement statement(connection.handle(),
                              QStringLiteral("SELECT task_id, ctime, mtime FROM task_info LIMIT 1"), err_msg);
    if (!statement.isValid())
        return false;
    const int rc = sqlite3_step(statement.handle());
    if (rc == SQLITE_DONE)
        return setError(err_msg, QString("task_info 为空"));
    if (rc != SQLITE_ROW)
        return setError(err_msg, sqliteError(connection.handle(), QString("读取 task_info 失败")));

    info.task_id = detail::columnText(statement.handle(), 0).trimmed();
    info.ctime = sqlite3_column_int64(statement.handle(), 1);
    info.mtime = sqlite3_column_int64(statement.handle(), 2);
    if (info.task_id.isEmpty() || info.ctime < 0 || info.mtime < 0)
        return setError(err_msg, QString("task_info 字段无效"));
    return true;
}

bool ModelTaskDataBase::upsertTaskInfo(const TaskInfoRecord &info, QString *err_msg) const
{
    if (info.task_id.trimmed().isEmpty() || info.ctime < 0 || info.mtime < 0)
        return setError(err_msg, QString("task_info 字段无效"));
    SqliteConnection connection(path_, err_msg);
    if (!connection.isOpen() || !detail::ensureTaskSchema(connection.handle(), err_msg))
        return false;
    if (!detail::executeSql(connection.handle(), QStringLiteral("DELETE FROM task_info"), err_msg))
        return false;

    SqliteStatement statement(connection.handle(),
                              QStringLiteral("INSERT INTO task_info (task_id, ctime, mtime) VALUES (?, ?, ?)"),
                              err_msg);
    if (!statement.isValid())
        return false;
    const QByteArray task_id = info.task_id.trimmed().toUtf8();
    if (sqlite3_bind_text(statement.handle(), 1, task_id.constData(), task_id.size(), SQLITE_TRANSIENT) != SQLITE_OK
        || sqlite3_bind_int64(statement.handle(), 2, info.ctime) != SQLITE_OK
        || sqlite3_bind_int64(statement.handle(), 3, info.mtime) != SQLITE_OK
        || sqlite3_step(statement.handle()) != SQLITE_DONE)
        return setError(err_msg, sqliteError(connection.handle(), QString("写入 task_info 失败")));
    return true;
}

bool ModelTaskDataBase::readTestParams(QVariantMap &params, QString *err_msg) const
{
    SqliteConnection connection(path_, err_msg);
    if (!connection.isOpen() || !detail::ensureTaskSchema(connection.handle(), err_msg))
        return false;
    return detail::readParams(connection.handle(), QStringLiteral("test_params"), params, err_msg);
}

bool ModelTaskDataBase::replaceTestParams(const QVariantMap &params, QString *err_msg) const
{
    SqliteConnection connection(path_, err_msg);
    if (!connection.isOpen() || !detail::ensureTaskSchema(connection.handle(), err_msg))
        return false;
    return detail::replaceParams(connection.handle(), QStringLiteral("test_params"), params, err_msg);
}

bool ModelTaskDataBase::readDatasets(QList<DatasetSelectionRecord> &selections, QString *err_msg) const
{
    SqliteConnection connection(path_, err_msg);
    if (!connection.isOpen() || !detail::ensureTaskSchema(connection.handle(), err_msg))
        return false;
    return detail::readDatasets(connection.handle(), selections, err_msg);
}

bool ModelTaskDataBase::replaceDatasets(const QList<DatasetSelectionRecord> &selections, QString *err_msg) const
{
    SqliteConnection connection(path_, err_msg);
    if (!connection.isOpen() || !detail::ensureTaskSchema(connection.handle(), err_msg))
        return false;
    return detail::replaceDatasets(connection.handle(), selections, err_msg);
}

bool ModelTaskDataBase::clearPredictions(QString *err_msg) const
{
    SqliteConnection connection(path_, err_msg);
    if (!connection.isOpen() || !detail::ensureTaskSchema(connection.handle(), err_msg))
        return false;
    return detail::executeSql(connection.handle(), QStringLiteral("DELETE FROM prediction"), err_msg);
}

bool ModelTaskDataBase::readPredictions(QHash<qint64, QVariant> &predictions, QString *err_msg) const
{
    predictions.clear();
    SqliteConnection connection(path_, err_msg);
    if (!connection.isOpen() || !detail::ensureTaskSchema(connection.handle(), err_msg))
        return false;
    SqliteStatement statement(connection.handle(), QStringLiteral("SELECT image_id, data FROM prediction ORDER BY image_id"),
                              err_msg);
    if (!statement.isValid())
        return false;
    while (true)
    {
        const int rc = sqlite3_step(statement.handle());
        if (rc == SQLITE_DONE)
            break;
        if (rc != SQLITE_ROW)
            return setError(err_msg, sqliteError(connection.handle(), QString("读取 prediction 失败")));

        const qint64 image_id = sqlite3_column_int64(statement.handle(), 0);
        const QByteArray encoded = sqlite3_column_text(statement.handle(), 1) != nullptr
            ? QByteArray(reinterpret_cast<const char *>(sqlite3_column_text(statement.handle(), 1)))
            : QByteArray();
        QString value_error;
        const QVariant value = detail::jsonToVariant(encoded, &value_error);
        if (!value_error.isEmpty())
            return setError(err_msg, QString("读取图像 %1 的预测失败: %2").arg(image_id).arg(value_error));
        if (image_id < 0)
            return setError(err_msg, QString("prediction.image_id 无效"));
        predictions.insert(image_id, value);
    }
    return true;
}

bool ModelTaskDataBase::upsertPrediction(const PredictionRecord &prediction, QString *err_msg) const
{
    if (prediction.image_id < 0)
        return setError(err_msg, QString("prediction.image_id 无效"));
    QString value_error;
    const QByteArray encoded = detail::variantToJson(prediction.data, &value_error);
    if (!value_error.isEmpty())
        return setError(err_msg, value_error);

    SqliteConnection connection(path_, err_msg);
    if (!connection.isOpen() || !detail::ensureTaskSchema(connection.handle(), err_msg))
        return false;
    SqliteStatement statement(connection.handle(),
                              QStringLiteral("INSERT INTO prediction (image_id, data) VALUES (?, ?) "
                                             "ON CONFLICT(image_id) DO UPDATE SET data=excluded.data"),
                              err_msg);
    if (!statement.isValid())
        return false;
    if (sqlite3_bind_int64(statement.handle(), 1, prediction.image_id) != SQLITE_OK
        || sqlite3_bind_text(statement.handle(), 2, encoded.constData(), encoded.size(), SQLITE_TRANSIENT)
               != SQLITE_OK
        || sqlite3_step(statement.handle()) != SQLITE_DONE)
        return setError(err_msg, sqliteError(connection.handle(), QString("写入 prediction 失败")));
    return true;
}

} // namespace dltool::database
