#pragma once

/**
 * @file ProjectRepository.h
 * @brief data 模块私有：project 表访问仓储。
 *
 * 池化连接的方法经共享事务上下文访问连接；基于路径的静态查询工具
 * 自建只读/读写连接（无事务语义，单语句自动提交）。
 */

#include "DatabaseContext.h"

#include <QVariantMap>
#include <QString>

#include <QtGlobal>

namespace dltool::database {

class ProjectRepository
{
public:
    static bool initProject(DatabaseContext &context, const QString &name, const int method, const QString &path,
                            const QString &description, const QString image_base_path, const qint64 ctime,
                            const qint64 mtime, QString &err_msg);

    static bool openProject(DatabaseContext &context, const QString &db_path, QString &name, int &method,
                            QString &path, QString &description, QString image_base_path, qint64 &ctime, qint64 &mtime,
                            QString &err_msg);

    static bool updateProject(DatabaseContext &context, const QString &name, const QString &path,
                              const QString &description, const QString &image_base_path, const qint64 mtime,
                              QString &err_msg);

    // ---- 基于路径的一次性查询工具（自建连接，不经连接池） ----
    static bool getProjectBaseInfo(const QString &path, QString &name, qint64 &mtime, QString &err_msg);
    static bool updateProjectBaseInfo(const QString &path, const QString &new_name, const QString &new_description,
                                      const qint64 new_mtime, QString &err_msg);
    static bool getProjectInfo(const QString &path, QVariantMap &project_info, QString &err_msg);
    static bool getLabelInfo(const QString &path, QVariantMap &label_info, QString &err_msg);
};

} // namespace dltool::database
