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

    static sqlpp::sqlite3::connection connect(const QString &path, const int flags);

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
                     const QString image_base_path, const qint64 ctime, const qint64 mtime, QString &err_msg) const;
    bool openProject(QString &name, int &method, QString &path, QString &description, QString image_base_path,
                     qint64 &ctime, qint64 &mtime, QString &err_msg) const;
    bool updateProject(const QString &name, const QString &path, const QString &description,
                       const QString &image_base_path, const qint64 mtime, QString &err_msg) const;

    static bool getProjectBaseInfo(const QString &path, QString &name, qint64 &mtime, QString &err_msg);
    static bool updateProjectBaseInfo(const QString &path, const QString &new_name, const QString &new_description,
                                      const qint64 new_mtime, QString &err_msg);

    static bool getProjectInfo(const QString &path, QVariantMap &project_info, QString &err_msg);

    std::vector<std::pair<int, QString>> getAllDatasets(QString &err_msg) const;

    bool addDataset(const QString &name, int64_t& dataset_id, QString &err_msg) const;
    int  getDatasetId(const QString &name, QString &err_msg) const;
    bool updateDataset(const QString &old_name, const QString &new_name, QString &err_msg) const;
    bool deleteDataset(const QString &name, QString &err_msg) const;
};

class DATA_API RecentProjectsDataBase : public DataBase
{
public:
    RecentProjectsDataBase(const QString &path, QObject *parent = nullptr);
    ~RecentProjectsDataBase();

    bool addProject(const QString &path, QString &err_msg) const;
    bool removeProject(const QString &path, QString &err_msg) const;
    int  getProjects(std::vector<QString> &paths, QString &err_msg) const;
};

} // namespace dltool::data
