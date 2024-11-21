#pragma once

#include "DataExport.h"

#include <sqlpp11/sqlite3/connection_pool.h>

#include <QObject>

namespace dltool::data {

class DATA_API DataBase : public QObject
{
public:
    DataBase(const QString &path, QObject *parent = nullptr);
    virtual ~DataBase();

    sqlpp::sqlite3::connection_pool *connectionPool()
    {
        return pool_;
    }

    static sqlpp::sqlite3::connection connect(const QString& path, const int flags);

protected:
    QString path_;

    std::size_t capacity_{5};

    sqlpp::sqlite3::connection_pool *pool_{nullptr};

private:
    void createDataBase();
};

class DATA_API ProjectDataBase : public DataBase
{
public:
    ProjectDataBase(const QString &path, QObject *parent = nullptr);
    ~ProjectDataBase();

    bool initProject(const QString &name, const int method, const QString &path, const QString &description,
                     const QString image_base_path, const qint64 ctime, const qint64 mtime) const;
    bool openProject(QString &name, int &method, QString &path, QString &description, QString image_base_path,
                     qint64 &ctime, qint64 &mtime) const;
    bool updateProject(const QString &name, const QString &path, const QString &description,
                       const QString &image_base_path, const qint64 mtime) const;

    static void getProjectBaseInfo(const QString &path, QString &name, qint64 &mtime);
    static QVariantMap getProjectInfo(const QString &path);
};

class DATA_API RecentProjectsDataBase : public DataBase
{
public:
    RecentProjectsDataBase(const QString &path, QObject *parent = nullptr);
    ~RecentProjectsDataBase();

    bool addProject(const QString &path) const;
    bool deleteProject(const QString &path) const;
    int  getProjects(std::vector<QString> &paths) const;
};

} // namespace dltool::data
