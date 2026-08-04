#include "DatabaseSqliteUtils.h"

#include "DatabaseValueUtils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QVariant>

#include <algorithm>
#include <initializer_list>

namespace dltool::database::detail {

namespace {

QString sqliteError(sqlite3 *db, const QString &prefix)
{
    const QString detail = db != nullptr ? QString::fromUtf8(sqlite3_errmsg(db)) : QString("数据库句柄为空");
    return prefix.isEmpty() ? detail : prefix + QStringLiteral(": ") + detail;
}

bool setError(QString *err_msg, const QString &message)
{
    if (err_msg != nullptr)
        *err_msg = message;
    return false;
}

QByteArray classIdsJson(const QList<qint64> &class_ids, QString *err_msg)
{
    QJsonArray array;
    for (const qint64 class_id : class_ids)
        array.append(class_id);
    const QByteArray value = QJsonDocument(array).toJson(QJsonDocument::Compact);
    if (value.isEmpty())
        setError(err_msg, QString("数据集类别列表 JSON 编码失败"));
    return value;
}

QList<qint64> parseClassIds(const QByteArray &value, QString *err_msg)
{
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(value, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isArray())
    {
        setError(err_msg, QString("数据库中的 class_ids 不是 JSON 数组"));
        return {};
    }

    QList<qint64> result;
    for (const QJsonValue &item : document.array())
    {
        bool ok = false;
        const qint64 class_id = item.toVariant().toLongLong(&ok);
        if (!ok || class_id < 0)
            return setError(err_msg, QString("数据库中的 class_ids 包含无效类别 ID")), QList<qint64>{};
        if (!result.contains(class_id))
            result.push_back(class_id);
    }
    return result;
}

bool tableNameIsSupported(const QString &table_name)
{
    return table_name == QStringLiteral("train_params") || table_name == QStringLiteral("test_params");
}

struct TableColumn
{
    QString name;
    QString type;
    int not_null{0};
    int primary_key{0};
};

bool ensureTableSchema(sqlite3 *db, const QString &table_name, const QString &create_sql,
                       const std::initializer_list<TableColumn> expected, QString *err_msg)
{
    if (!executeSql(db, create_sql, err_msg))
        return false;

    SqliteStatement statement(db, QString("PRAGMA table_info(%1)").arg(table_name), err_msg);
    if (!statement.isValid())
        return false;

    QList<TableColumn> actual;
    while (true)
    {
        const int rc = sqlite3_step(statement.handle());
        if (rc == SQLITE_DONE)
            break;
        if (rc != SQLITE_ROW)
            return setError(err_msg, sqliteError(db, QString("读取数据库表结构失败: %1").arg(table_name)));

        actual.push_back({columnText(statement.handle(), 1), columnText(statement.handle(), 2),
                          sqlite3_column_int(statement.handle(), 3), sqlite3_column_int(statement.handle(), 5)});
    }

    if (actual.size() != static_cast<qsizetype>(expected.size()))
        return setError(err_msg, QString("数据库表结构不匹配: %1").arg(table_name));

    qsizetype index = 0;
    for (const TableColumn &column : expected)
    {
        const TableColumn &actual_column = actual.at(index++);
        if (actual_column.name != column.name
            || actual_column.type.compare(column.type, Qt::CaseInsensitive) != 0
            || actual_column.not_null != column.not_null || actual_column.primary_key != column.primary_key)
            return setError(err_msg, QString("数据库表结构不匹配: %1").arg(table_name));
    }
    return true;
}

} // namespace

SqliteConnection::SqliteConnection(const QString &path, QString *err_msg)
{
    if (path.trimmed().isEmpty())
    {
        if (err_msg != nullptr)
            *err_msg = QString("数据库路径为空");
        return;
    }

    const QByteArray encoded_path = path.toUtf8();
    const int rc = sqlite3_open_v2(encoded_path.constData(), &handle_,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK)
    {
        const QString message = sqliteError(handle_, QString("打开 SQLite 数据库失败"));
        if (handle_ != nullptr)
            sqlite3_close(handle_);
        handle_ = nullptr;
        if (err_msg != nullptr)
            *err_msg = message;
        return;
    }
    sqlite3_busy_timeout(handle_, 5000);
}

SqliteConnection::~SqliteConnection()
{
    if (handle_ != nullptr)
        sqlite3_close(handle_);
}

SqliteStatement::SqliteStatement(sqlite3 *db, const QString &sql, QString *err_msg)
{
    if (db == nullptr)
    {
        if (err_msg != nullptr)
            *err_msg = QString("SQLite 数据库句柄为空");
        return;
    }
    const QByteArray encoded_sql = sql.toUtf8();
    if (sqlite3_prepare_v2(db, encoded_sql.constData(), encoded_sql.size(), &handle_, nullptr) != SQLITE_OK)
    {
        if (err_msg != nullptr)
            *err_msg = sqliteError(db, QString("准备 SQLite 语句失败"));
        handle_ = nullptr;
    }
}

SqliteStatement::~SqliteStatement()
{
    if (handle_ != nullptr)
        sqlite3_finalize(handle_);
}

bool executeSql(sqlite3 *db, const QString &sql, QString *err_msg)
{
    if (db == nullptr)
        return setError(err_msg, QString("SQLite 数据库句柄为空"));
    const QByteArray encoded_sql = sql.toUtf8();
    char *sqlite_error = nullptr;
    const int rc = sqlite3_exec(db, encoded_sql.constData(), nullptr, nullptr, &sqlite_error);
    if (rc == SQLITE_OK)
        return true;

    const QString message = sqlite_error != nullptr ? QString::fromUtf8(sqlite_error)
                                                    : sqliteError(db, QString("执行 SQLite 语句失败"));
    if (sqlite_error != nullptr)
        sqlite3_free(sqlite_error);
    return setError(err_msg, message);
}

QString columnText(sqlite3_stmt *statement, const int column)
{
    if (statement == nullptr)
        return {};
    const unsigned char *value = sqlite3_column_text(statement, column);
    return value == nullptr ? QString() : QString::fromUtf8(reinterpret_cast<const char *>(value));
}

bool ensureModelSchema(sqlite3 *db, QString *err_msg)
{
    return ensureTableSchema(db, QStringLiteral("train_params"),
                             QStringLiteral(R"SQL(
CREATE TABLE IF NOT EXISTS train_params (
    name_en TEXT NOT NULL PRIMARY KEY,
    value   TEXT NOT NULL
)
)SQL"),
                             {{QStringLiteral("name_en"), QStringLiteral("TEXT"), 1, 1},
                              {QStringLiteral("value"), QStringLiteral("TEXT"), 1, 0}},
                             err_msg)
        && ensureTableSchema(db, QStringLiteral("datasets"),
                             QStringLiteral(R"SQL(
CREATE TABLE IF NOT EXISTS datasets (
    type       TEXT    NOT NULL,
    dataset_id INTEGER NOT NULL,
    class_ids  TEXT    NOT NULL,
    PRIMARY KEY (type, dataset_id)
)
)SQL"),
                             {{QStringLiteral("type"), QStringLiteral("TEXT"), 1, 1},
                              {QStringLiteral("dataset_id"), QStringLiteral("INTEGER"), 1, 2},
                              {QStringLiteral("class_ids"), QStringLiteral("TEXT"), 1, 0}},
                             err_msg)
        && ensureTableSchema(db, QStringLiteral("test_tasks"),
                             QStringLiteral(R"SQL(
CREATE TABLE IF NOT EXISTS test_tasks (
    task_id TEXT    NOT NULL PRIMARY KEY,
    name    TEXT    NOT NULL UNIQUE COLLATE NOCASE,
    ctime   INTEGER NOT NULL,
    mtime   INTEGER NOT NULL
)
)SQL"),
                             {{QStringLiteral("task_id"), QStringLiteral("TEXT"), 1, 1},
                              {QStringLiteral("name"), QStringLiteral("TEXT"), 1, 0},
                              {QStringLiteral("ctime"), QStringLiteral("INTEGER"), 1, 0},
                              {QStringLiteral("mtime"), QStringLiteral("INTEGER"), 1, 0}},
                             err_msg);
}

bool ensureTaskSchema(sqlite3 *db, QString *err_msg)
{
    return ensureTableSchema(db, QStringLiteral("task_info"),
                             QStringLiteral(R"SQL(
CREATE TABLE IF NOT EXISTS task_info (
    task_id TEXT    NOT NULL PRIMARY KEY,
    ctime   INTEGER NOT NULL,
    mtime   INTEGER NOT NULL
)
)SQL"),
                             {{QStringLiteral("task_id"), QStringLiteral("TEXT"), 1, 1},
                              {QStringLiteral("ctime"), QStringLiteral("INTEGER"), 1, 0},
                              {QStringLiteral("mtime"), QStringLiteral("INTEGER"), 1, 0}},
                             err_msg)
        && ensureTableSchema(db, QStringLiteral("test_params"),
                             QStringLiteral(R"SQL(
CREATE TABLE IF NOT EXISTS test_params (
    name_en TEXT NOT NULL PRIMARY KEY,
    value   TEXT NOT NULL
)
)SQL"),
                             {{QStringLiteral("name_en"), QStringLiteral("TEXT"), 1, 1},
                              {QStringLiteral("value"), QStringLiteral("TEXT"), 1, 0}},
                             err_msg)
        && ensureTableSchema(db, QStringLiteral("datasets"),
                             QStringLiteral(R"SQL(
CREATE TABLE IF NOT EXISTS datasets (
    type       TEXT    NOT NULL,
    dataset_id INTEGER NOT NULL,
    class_ids  TEXT    NOT NULL,
    PRIMARY KEY (type, dataset_id)
)
)SQL"),
                             {{QStringLiteral("type"), QStringLiteral("TEXT"), 1, 1},
                              {QStringLiteral("dataset_id"), QStringLiteral("INTEGER"), 1, 2},
                              {QStringLiteral("class_ids"), QStringLiteral("TEXT"), 1, 0}},
                             err_msg)
        && ensureTableSchema(db, QStringLiteral("prediction"),
                             QStringLiteral(R"SQL(
CREATE TABLE IF NOT EXISTS prediction (
    image_id INTEGER NOT NULL PRIMARY KEY,
    data     TEXT    NOT NULL
)
)SQL"),
                             {{QStringLiteral("image_id"), QStringLiteral("INTEGER"), 1, 1},
                              {QStringLiteral("data"), QStringLiteral("TEXT"), 1, 0}},
                             err_msg);
}

bool readParams(sqlite3 *db, const QString &table_name, QVariantMap &params, QString *err_msg)
{
    params.clear();
    if (!tableNameIsSupported(table_name))
        return setError(err_msg, QString("不支持的参数表"));

    SqliteStatement statement(db, QStringLiteral("SELECT name_en, value FROM ") + table_name
                                  + QStringLiteral(" ORDER BY name_en"),
                              err_msg);
    if (!statement.isValid())
        return false;

    QList<NamedValue> values;
    while (true)
    {
        const int rc = sqlite3_step(statement.handle());
        if (rc == SQLITE_DONE)
            break;
        if (rc != SQLITE_ROW)
            return setError(err_msg, sqliteError(db, QString("读取参数失败")));

        const QString name = columnText(statement.handle(), 0).trimmed();
        if (name.isEmpty())
            return setError(err_msg, QString("数据库参数名为空"));
        QString value_error;
        const QVariant value = jsonToVariant(sqlite3_column_text(statement.handle(), 1) != nullptr
                                                  ? QByteArray(reinterpret_cast<const char *>(
                                                        sqlite3_column_text(statement.handle(), 1)))
                                                  : QByteArray(),
                                              &value_error);
        if (!value_error.isEmpty())
            return setError(err_msg, QString("读取参数 '%1' 失败: %2").arg(name, value_error));
        values.push_back({name, value});
    }
    params = unflattenValues(values);
    return true;
}

bool replaceParams(sqlite3 *db, const QString &table_name, const QVariantMap &params, QString *err_msg)
{
    if (!tableNameIsSupported(table_name))
        return setError(err_msg, QString("不支持的参数表"));
    if (!executeSql(db, QStringLiteral("BEGIN IMMEDIATE"), err_msg))
        return false;
    bool committed = false;
    const auto rollback = [&]()
    {
        if (!committed)
            executeSql(db, QStringLiteral("ROLLBACK"), nullptr);
    };

    if (!executeSql(db, QStringLiteral("DELETE FROM ") + table_name, err_msg))
    {
        rollback();
        return false;
    }
    SqliteStatement statement(db, QStringLiteral("INSERT INTO ") + table_name
                                  + QStringLiteral(" (name_en, value) VALUES (?, ?)"),
                              err_msg);
    if (!statement.isValid())
    {
        rollback();
        return false;
    }
    for (const NamedValue &entry : flattenValues(params))
    {
        QString value_error;
        const QByteArray encoded = variantToJson(entry.value, &value_error);
        if (!value_error.isEmpty())
        {
            rollback();
            return setError(err_msg, QString("编码参数 '%1' 失败: %2").arg(entry.name, value_error));
        }
        sqlite3_reset(statement.handle());
        sqlite3_clear_bindings(statement.handle());
        if (sqlite3_bind_text(statement.handle(), 1, entry.name.toUtf8().constData(), -1, SQLITE_TRANSIENT)
                != SQLITE_OK
            || sqlite3_bind_text(statement.handle(), 2, encoded.constData(), encoded.size(), SQLITE_TRANSIENT)
                   != SQLITE_OK
            || sqlite3_step(statement.handle()) != SQLITE_DONE)
        {
            rollback();
            return setError(err_msg, sqliteError(db, QString("写入参数失败")));
        }
    }
    if (!executeSql(db, QStringLiteral("COMMIT"), err_msg))
    {
        rollback();
        return false;
    }
    committed = true;
    return true;
}

bool readDatasets(sqlite3 *db, QList<DatasetSelectionRecord> &selections, QString *err_msg)
{
    selections.clear();
    SqliteStatement statement(db, QStringLiteral("SELECT type, dataset_id, class_ids FROM datasets "
                                                 "ORDER BY type, dataset_id"),
                              err_msg);
    if (!statement.isValid())
        return false;
    while (true)
    {
        const int rc = sqlite3_step(statement.handle());
        if (rc == SQLITE_DONE)
            break;
        if (rc != SQLITE_ROW)
            return setError(err_msg, sqliteError(db, QString("读取数据集选择失败")));

        QString class_error;
        const QByteArray class_ids = sqlite3_column_text(statement.handle(), 2) != nullptr
            ? QByteArray(reinterpret_cast<const char *>(sqlite3_column_text(statement.handle(), 2)))
            : QByteArray();
        const QList<qint64> parsed = parseClassIds(class_ids, &class_error);
        if (!class_error.isEmpty())
            return setError(err_msg, class_error);
        DatasetSelectionRecord record;
        record.type = columnText(statement.handle(), 0);
        record.dataset_id = sqlite3_column_int64(statement.handle(), 1);
        record.class_ids = parsed;
        if (record.type.trimmed().isEmpty() || record.dataset_id < 0)
            return setError(err_msg, QString("数据库数据集选择字段无效"));
        selections.push_back(record);
    }
    return true;
}

bool replaceDatasets(sqlite3 *db, const QList<DatasetSelectionRecord> &selections, QString *err_msg)
{
    if (!executeSql(db, QStringLiteral("BEGIN IMMEDIATE"), err_msg))
        return false;
    bool committed = false;
    const auto rollback = [&]()
    {
        if (!committed)
            executeSql(db, QStringLiteral("ROLLBACK"), nullptr);
    };
    if (!executeSql(db, QStringLiteral("DELETE FROM datasets"), err_msg))
    {
        rollback();
        return false;
    }
    SqliteStatement statement(db, QStringLiteral("INSERT INTO datasets "
                                                 "(type, dataset_id, class_ids) VALUES (?, ?, ?)"),
                              err_msg);
    if (!statement.isValid())
    {
        rollback();
        return false;
    }
    for (const DatasetSelectionRecord &record : selections)
    {
        if (record.type.trimmed().isEmpty() || record.dataset_id < 0)
        {
            rollback();
            return setError(err_msg, QString("数据集选择字段无效"));
        }
        QString class_error;
        const QByteArray class_ids = classIdsJson(record.class_ids, &class_error);
        if (!class_error.isEmpty())
        {
            rollback();
            return false;
        }
        sqlite3_reset(statement.handle());
        sqlite3_clear_bindings(statement.handle());
        if (sqlite3_bind_text(statement.handle(), 1, record.type.toUtf8().constData(), -1, SQLITE_TRANSIENT)
                != SQLITE_OK
            || sqlite3_bind_int64(statement.handle(), 2, record.dataset_id) != SQLITE_OK
            || sqlite3_bind_text(statement.handle(), 3, class_ids.constData(), class_ids.size(), SQLITE_TRANSIENT)
                   != SQLITE_OK
            || sqlite3_step(statement.handle()) != SQLITE_DONE)
        {
            rollback();
            return setError(err_msg, sqliteError(db, QString("写入数据集选择失败")));
        }
    }
    if (!executeSql(db, QStringLiteral("COMMIT"), err_msg))
    {
        rollback();
        return false;
    }
    committed = true;
    return true;
}

} // namespace dltool::database::detail
