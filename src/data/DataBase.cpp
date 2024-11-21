#include "data/DataBase.h"

#include "data/SqlDef.h"
#include "data/ddl/ProjectTable.h"
#include "data/ddl/RecentProjectsTable.h"

#include <sqlpp11/sqlpp11.h>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

namespace dltool::data {

const auto project_table = Project{};
const auto rectent_projects_table = RecentProjects{};

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

ProjectDataBase::~ProjectDataBase()
{

}

bool ProjectDataBase::initProject(const QString &name, const int method, const QString &path,
                                  const QString &description, const QString image_base_path, const qint64 ctime, const qint64 mtime) const
{
    if (pool_ == nullptr)
    {
        return false;
    }
    // create project table
    auto db = pool_->get();
    db.execute(SqlDef::SqlMap.at(SqlDef::CreateProject));
    db(sqlpp::insert_into(project_table).set(
        project_table.name             = name.toUtf8().constData(),
        project_table.method           = method,
        project_table.path             = path.toUtf8().constData(),
        project_table.description      = description.toUtf8().constData(),
        project_table.imageBasePath    = image_base_path.toUtf8().constData(),
        project_table.ctime            = ctime,
        project_table.mtime            = mtime
    ));
    return true;
}

bool ProjectDataBase::openProject(QString &name, int &method, QString &path, QString &description,
                                  QString image_base_path, qint64 &ctime, qint64 &mtime) const
{
    if (pool_ == nullptr)
    {
        return false;
    }
    auto db   = pool_->get();
    auto data = db(sqlpp::select(
        project_table.name,
        project_table.method,
        project_table.path,
        project_table.description,
        project_table.imageBasePath,
        project_table.ctime,
        project_table.mtime
    ).from(project_table).unconditionally());
    if (!data.empty())
    {
        const auto& row = data.front();

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

bool ProjectDataBase::updateProject(const QString &name, const QString &path, const QString &description, const QString &image_base_path, const qint64 mtime) const
{
    if (pool_ == nullptr)
    {
        return false;
    }
    auto db        = pool_->get();
    db(sqlpp::update(project_table).set(
        project_table.name          = name.toUtf8().constData(),
        project_table.path          = path.toUtf8().constData(),
        project_table.description   = description.toUtf8().constData(),
        project_table.imageBasePath = image_base_path.toUtf8().constData(),
        project_table.mtime         = mtime
    ).unconditionally());
    return true;
}

void ProjectDataBase::getProjectBaseInfo(const QString &path, QString &name, qint64 &mtime)
{
    sqlpp::sqlite3::connection db = DataBase::connect(path, SQLITE_OPEN_READONLY);
    auto data = db(sqlpp::select(
       project_table.name,
       project_table.mtime
    ).from(project_table).unconditionally());
    if (!data.empty())
    {
        const auto & row = data.front();

        name  = QString::fromStdString(row.name);
        mtime = row.mtime;

        data.pop_front();
    }
}

QVariantMap ProjectDataBase::getProjectInfo(const QString &path)
{
    QVariantMap project_info;
    sqlpp::sqlite3::connection db = DataBase::connect(path, SQLITE_OPEN_READONLY);
    auto data = db(sqlpp::select(
        project_table.name,
        project_table.method,
        project_table.path,
        project_table.description,
        project_table.imageBasePath,
        project_table.ctime,
        project_table.mtime
    ).from(project_table).unconditionally());
    if (!data.empty())
    {
        const auto & row = data.front();

        project_info.insert("name", QString::fromStdString(row.name));
        project_info.insert("method", static_cast<int>(row.method));
        project_info.insert("path", QString::fromStdString(row.path));
        project_info.insert("description", QString::fromStdString(row.description));
        project_info.insert("image_base_path", QString::fromStdString(row.imageBasePath));
        project_info.insert("ctime", QDateTime::fromSecsSinceEpoch(row.ctime).toString("yyyy/MM/dd hh:mm"));
        project_info.insert("mtime", QDateTime::fromSecsSinceEpoch(row.mtime).toString("yyyy/MM/dd hh:mm"));

        data.pop_front();
    }
    return project_info;
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

RecentProjectsDataBase::~RecentProjectsDataBase()
{

}

bool RecentProjectsDataBase::addProject(const QString &path) const
{
    if (pool_ == nullptr)
        return false;
    auto db = pool_->get();
    db(sqlpp::insert_into(rectent_projects_table).set(rectent_projects_table.path  = path.toUtf8().constData()));
    return true;
}

bool RecentProjectsDataBase::deleteProject(const QString &path) const
{
    if (pool_ == nullptr)
        return false;
    auto db = pool_->get();
    db(sqlpp::remove_from(rectent_projects_table).where(rectent_projects_table.path==path.toUtf8().toStdString()));
    return true;
}

int RecentProjectsDataBase::getProjects(std::vector<QString> &paths) const
{
    if (pool_ == nullptr)
        return 0;
    auto db = pool_->get();
    auto data = db(sqlpp::select(rectent_projects_table.path).from(rectent_projects_table).unconditionally());
    for (const auto & row : data)
    {
        QString path = QString::fromStdString(row.path);
        paths.emplace_back(path);
    }
    return static_cast<int>(paths.size());
}


} // namespace dltool::data
