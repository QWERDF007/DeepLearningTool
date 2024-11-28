#include "data/DataBase.h"

#include "data/SqlDef.h"
#include "data/ddl/ProjectTable.h"
#include "data/ddl/RecentProjectsTable.h"

#include <sqlpp11/sqlpp11.h>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

namespace dltool::data {

const auto ProjectTable         = Project{};
const auto RectentProjectsTable = RecentProjects{};

DataBase::DataBase(const QString &path, QObject *parent)
    : QObject(parent)
    , path_(path)
{
    createDataBase();
}

DataBase::~DataBase()
{
    if (pool_)
        delete pool_;
}

sqlpp::sqlite3::connection DataBase::connect(const QString &path, const int flags)
{
    auto config              = std::make_shared<sqlpp::sqlite3::connection_config>();
    config->path_to_database = path.toUtf8().constData();
    config->flags            = flags;
    return sqlpp::sqlite3::connection(config);
}

void DataBase::createDataBase()
{
    auto config              = std::make_shared<sqlpp::sqlite3::connection_config>();
    config->path_to_database = path_.toUtf8().constData();
    config->flags            = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;

    const QDir &dir = QFileInfo(path_).dir();
    dir.mkpath(dir.path());

    pool_ = new sqlpp::sqlite3::connection_pool{config, capacity_};
}

ProjectDataBase::ProjectDataBase(const QString &path, QObject *parent)
    : DataBase(path, parent)
{
}

ProjectDataBase::~ProjectDataBase() {}

bool ProjectDataBase::initProject(const QString &name, const int method, const QString &path,
                                  const QString &description, const QString image_base_path, const qint64 ctime,
                                  const qint64 mtime, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path);
            return false;
        }
        // create project table
        auto db = pool_->get();
        db.execute(SqlDef::SqlMap.at(SqlDef::CreateProject));
        db(sqlpp::insert_into(ProjectTable)
               .set(ProjectTable.name = name.toUtf8().constData(), ProjectTable.method = method,
                    ProjectTable.path          = path.toUtf8().constData(),
                    ProjectTable.description   = description.toUtf8().constData(),
                    ProjectTable.imageBasePath = image_base_path.toUtf8().constData(), ProjectTable.ctime = ctime,
                    ProjectTable.mtime = mtime));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::openProject(QString &name, int &method, QString &path, QString &description,
                                  QString image_base_path, qint64 &ctime, qint64 &mtime, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path);
            return false;
        }
        auto db = pool_->get();
        auto data
            = db(sqlpp::select(ProjectTable.name, ProjectTable.method, ProjectTable.path, ProjectTable.description,
                               ProjectTable.imageBasePath, ProjectTable.ctime, ProjectTable.mtime)
                     .from(ProjectTable)
                     .unconditionally());
        if (!data.empty())
        {
            const auto &row = data.front();

            name            = QString::fromStdString(row.name);
            method          = row.method;
            path            = QString::fromStdString(row.path);
            description     = QString::fromStdString(row.description);
            image_base_path = QString::fromStdString(row.imageBasePath);
            ctime           = row.ctime;
            mtime           = row.mtime;

            data.pop_front();
        }
        return path == path_;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::updateProject(const QString &name, const QString &path, const QString &description,
                                    const QString &image_base_path, const qint64 mtime, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path);
            return false;
        }
        auto db = pool_->get();
        db(sqlpp::update(ProjectTable)
               .set(ProjectTable.name = name.toUtf8().constData(), ProjectTable.path = path.toUtf8().constData(),
                    ProjectTable.description   = description.toUtf8().constData(),
                    ProjectTable.imageBasePath = image_base_path.toUtf8().constData(), ProjectTable.mtime = mtime)
               .unconditionally());
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::getProjectBaseInfo(const QString &path, QString &name, qint64 &mtime, QString &err_msg)
{
    try
    {
        sqlpp::sqlite3::connection db = DataBase::connect(path, SQLITE_OPEN_READONLY);
        auto data = db(sqlpp::select(ProjectTable.name, ProjectTable.mtime).from(ProjectTable).unconditionally());
        if (!data.empty())
        {
            const auto &row = data.front();

            name  = QString::fromStdString(row.name);
            mtime = row.mtime;

            data.pop_front();
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::updateProjectBaseInfo(const QString &path, const QString &new_name,
                                            const QString &new_description, const qint64 new_mtime, QString &err_msg)
{
    try
    {
        sqlpp::sqlite3::connection db = DataBase::connect(path, SQLITE_OPEN_READWRITE);
        db(sqlpp::update(ProjectTable)
               .set(ProjectTable.name        = new_name.toUtf8().constData(),
                    ProjectTable.description = new_description.toUtf8().constData(), ProjectTable.mtime = new_mtime)
               .unconditionally());
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::getProjectInfo(const QString &path, QVariantMap &project_info, QString &err_msg)
{
    try
    {
        sqlpp::sqlite3::connection db = DataBase::connect(path, SQLITE_OPEN_READONLY);

        auto data
            = db(sqlpp::select(ProjectTable.name, ProjectTable.method, ProjectTable.path, ProjectTable.description,
                               ProjectTable.imageBasePath, ProjectTable.ctime, ProjectTable.mtime)
                     .from(ProjectTable)
                     .unconditionally());
        if (!data.empty())
        {
            const auto &row = data.front();

            project_info.insert("name", QString::fromStdString(row.name));
            project_info.insert("method", static_cast<int>(row.method));
            project_info.insert("path", QString::fromStdString(row.path));
            project_info.insert("description", QString::fromStdString(row.description));
            project_info.insert("image_base_path", QString::fromStdString(row.imageBasePath));
            project_info.insert("ctime", QDateTime::fromSecsSinceEpoch(row.ctime).toString("yyyy/MM/dd hh:mm"));
            project_info.insert("mtime", QDateTime::fromSecsSinceEpoch(row.mtime).toString("yyyy/MM/dd hh:mm"));

            data.pop_front();
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

RecentProjectsDataBase::RecentProjectsDataBase(const QString &path, QObject *parent)
    : DataBase(path, parent)
{
    if (pool_ != nullptr && !QFile::exists(path))
    {
        auto db = pool_->get();
        db.execute(SqlDef::SqlMap.at(SqlDef::CreateRecentProjects));
    }
}

RecentProjectsDataBase::~RecentProjectsDataBase() {}

bool RecentProjectsDataBase::addProject(const QString &path, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path);
            return false;
        }
        auto db = pool_->get();
        db(sqlpp::insert_into(RectentProjectsTable).set(RectentProjectsTable.path = path.toUtf8().constData()));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool RecentProjectsDataBase::removeProject(const QString &path, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path);
            return false;
        }
        auto db = pool_->get();
        db(sqlpp::remove_from(RectentProjectsTable).where(RectentProjectsTable.path == path.toUtf8().toStdString()));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

int RecentProjectsDataBase::getProjects(std::vector<QString> &paths, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return 0;
        }
        auto db   = pool_->get();
        auto data = db(sqlpp::select(RectentProjectsTable.path).from(RectentProjectsTable).unconditionally());
        for (const auto &row : data)
        {
            QString path = QString::fromStdString(row.path);
            paths.emplace_back(path);
        }
        return static_cast<int>(paths.size());
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

} // namespace dltool::data
