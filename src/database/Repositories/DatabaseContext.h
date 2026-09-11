#pragma once

/**
 * @file DatabaseContext.h
 * @brief data 模块私有：仓储共享事务上下文。
 *
 * 由 ProjectDataBase 在事务边界内构造并传给各仓储；仓储只通过它访问
 * 连接，不自行开启、提交或回滚事务——事务裁决权集中在 facade。
 */

#include <sqlpp11/sqlpp11.h>
#include <sqlpp11/sqlite3/connection.h>
#include <sqlpp11/sqlite3/connection_pool.h>

namespace dltool::database {

class DatabaseContext
{
public:
    using PooledConnection = sqlpp::pooled_connection<sqlpp::sqlite3::connection_base>;

    explicit DatabaseContext(PooledConnection &connection) : connection_(connection) {}

    PooledConnection &db()
    {
        return connection_;
    }

private:
    PooledConnection &connection_;
};

} // namespace dltool::database
