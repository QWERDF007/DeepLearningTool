#pragma once

#include "database/ModelDatabaseTypes.h"

#include <sqlpp11/connection.h>
#include <sqlpp11/connection_pool.h>
#include <sqlpp11/sqlite3/connection.h>

#include <QList>
#include <QString>
#include <QVariantMap>

namespace dltool::database::detail {

bool ensureModelSchema(sqlpp::pooled_connection<sqlpp::sqlite3::connection_base> &db, QString *err_msg = nullptr);
bool ensureTaskSchema(sqlpp::pooled_connection<sqlpp::sqlite3::connection_base> &db, QString *err_msg = nullptr);

bool readParams(sqlpp::pooled_connection<sqlpp::sqlite3::connection_base> &db, const QString &table_name, QVariantMap &params,
                QString *err_msg = nullptr);
bool replaceParams(sqlpp::pooled_connection<sqlpp::sqlite3::connection_base> &db, const QString &table_name, const QVariantMap &params,
                   QString *err_msg = nullptr);

bool readDatasets(sqlpp::pooled_connection<sqlpp::sqlite3::connection_base> &db, QList<DatasetSelectionRecord> &selections,
                  QString *err_msg = nullptr);
bool replaceDatasets(sqlpp::pooled_connection<sqlpp::sqlite3::connection_base> &db, const QList<DatasetSelectionRecord> &selections,
                     QString *err_msg = nullptr);

} // namespace dltool::database::detail

