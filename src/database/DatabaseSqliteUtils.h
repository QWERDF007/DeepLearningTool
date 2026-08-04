#pragma once

#include "database/ModelDatabaseTypes.h"

#include <QList>
#include <QString>
#include <QVariantMap>

#include <sqlite3.h>

namespace dltool::database::detail {

class SqliteConnection
{
public:
    explicit SqliteConnection(const QString &path, QString *err_msg = nullptr);
    ~SqliteConnection();

    SqliteConnection(const SqliteConnection &) = delete;
    SqliteConnection &operator=(const SqliteConnection &) = delete;

    sqlite3 *handle() const
    {
        return handle_;
    }

    bool isOpen() const
    {
        return handle_ != nullptr;
    }

private:
    sqlite3 *handle_{nullptr};
};

class SqliteStatement
{
public:
    SqliteStatement(sqlite3 *db, const QString &sql, QString *err_msg = nullptr);
    ~SqliteStatement();

    SqliteStatement(const SqliteStatement &) = delete;
    SqliteStatement &operator=(const SqliteStatement &) = delete;

    sqlite3_stmt *handle() const
    {
        return handle_;
    }

    bool isValid() const
    {
        return handle_ != nullptr;
    }

private:
    sqlite3_stmt *handle_{nullptr};
};

bool executeSql(sqlite3 *db, const QString &sql, QString *err_msg = nullptr);
QString columnText(sqlite3_stmt *statement, int column);

bool ensureModelSchema(sqlite3 *db, QString *err_msg = nullptr);
bool ensureTaskSchema(sqlite3 *db, QString *err_msg = nullptr);

bool readParams(sqlite3 *db, const QString &table_name, QVariantMap &params, QString *err_msg = nullptr);
bool replaceParams(sqlite3 *db, const QString &table_name, const QVariantMap &params,
                   QString *err_msg = nullptr);

bool readDatasets(sqlite3 *db, QList<DatasetSelectionRecord> &selections, QString *err_msg = nullptr);
bool replaceDatasets(sqlite3 *db, const QList<DatasetSelectionRecord> &selections,
                     QString *err_msg = nullptr);

} // namespace dltool::database::detail

