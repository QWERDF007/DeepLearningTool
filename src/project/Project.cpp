#include "project/Project.h"

#include "data/DataBase.h"

#include <spdlog/spdlog.h>

#include <QDateTime>
#include <QFile>
#include <algorithm>
#include <chrono>

namespace dltool::project {

Project::Project(const QString &name, const int method, const QString &path, const QString &description,
                 const QString image_base_path, const qint64 ctime, const qint64 mtime, QObject *parent)
    : QObject(parent)
    , name_(name)
    , method_(method)
    , path_(path)
    , description_(description)
    , image_base_path_(image_base_path)
    , ctime_(ctime)
    , mtime_(mtime)
{
}

Project::Project(const QString &path, QObject *parent)
    : QObject(parent)
    , path_(path)
{
}

Project::~Project() {}

void Project::initProject()
{
    if (database_ == nullptr)
        database_ = new data::ProjectDataBase(path_, this);
    spdlog::info("初始化项目, 创建数据库: {}", path_.toUtf8().constData());
    qint64  ctime = QDateTime::currentSecsSinceEpoch();
    QString err_msg;
    bool    ok = database_->initProject(name_, method_, path_, description_, image_base_path_, ctime, ctime, err_msg);
    if (ok)
    {
        spdlog::info("创建表: project");
    }
    else
    {
        spdlog::error("初始化项目失败, error: {}", err_msg.toUtf8().constData());
    }
}

void Project::openProject()
{
    if (database_ == nullptr)
        database_ = new data::ProjectDataBase(path_, this);
    spdlog::info("打开数据库: {}", path_.toUtf8().constData());
    QString err_msg;
    bool    ok = database_->openProject(name_, method_, path_, description_, image_base_path_, ctime_, mtime_, err_msg);
    if (ok)
    {
        spdlog::info("查询表: project");
    }
    else
    {
        spdlog::error("打开项目失败: {}, error: {}", path_.toUtf8().constData(), err_msg.toUtf8().constData());
    }
}

std::tuple<bool, QString> Project::isValid(const int method, const QString &path, bool is_new)
{
    bool file_exist = QFile::exists(path);
    if (is_new)
    {
        if (file_exist)
            return {false, "项目已存在"};
        // else if (!dltool::core::DeepLearningMethod::getInstance()->getMethodTypes().contains(method))
        // return false;
        else
            return {true, ""};
    }
    return {file_exist, file_exist ? "" : "项目不存在"};
}

RectentProjects::RectentProjects(const QString &path, QObject *parent)
    : QAbstractListModel(parent)
    , path_(path)
    , selection_(new QItemSelectionModel(this))
{
    init();
}

RectentProjects::~RectentProjects() {}

void RectentProjects::init()
{
    auto start = std::chrono::high_resolution_clock::now();
    if (database_ == nullptr)
        database_ = new data::RecentProjectsDataBase(path_, this);
    if (!project_infos.empty())
        project_infos.clear();
    spdlog::info("打开最近项目数据库: {}", path_.toUtf8().constData());
    std::vector<QString> paths;
    QString              err_msg;
    const int            size = database_->getProjects(paths, err_msg);
    if (err_msg.isEmpty())
    {
        spdlog::info("查询表: recent_projects, size: {}", size);
    }
    else
    {
        spdlog::error("查询表失败: recent_projects, error: {}", err_msg.toUtf8().constData());
    }

    beginResetModel();
    for (int i = 0; i < size; ++i)
    {
        ProjectBaseInfo info;
        info.path = paths[i];
        QString msg;
        bool    ok = data::ProjectDataBase::getProjectBaseInfo(info.path, info.name, info.mtime, msg);
        if (ok)
        {
            project_infos.emplace_back(info);
        }
        else
        {
            spdlog::error("获取基础信息失败: {}, error: {}", info.path.toUtf8().constData(), msg.toUtf8().constData());
        }
    }
    std::sort(project_infos.begin(), project_infos.end(),
              [](const ProjectBaseInfo &lhs, const ProjectBaseInfo &rhs) { return lhs.mtime > rhs.mtime; });
    endResetModel();
    connect(selection_, &QItemSelectionModel::selectionChanged, this, &RectentProjects::updateSelection);
    auto   end      = std::chrono::high_resolution_clock::now();
    double duration = std::chrono::duration<double, std::milli>(end - start).count();
    qInfo() << __FUNCTION__ << __LINE__ << "time elapsed:" << duration << "ms";
}

int RectentProjects::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(project_infos.size());
}

QVariant RectentProjects::data(const QModelIndex &index, int role) const
{
    const int row = index.row();
    if (row < 0 || row >= rowCount())
        return QVariant();
    switch (role)
    {
    case NameRole:
        return getName(index);
    case PathRole:
        return getPath(index);
    case ToolTipRole:
        return getTooltip(index);
    case SelectedRole:
        return getSelected(index);
    default:
        return QVariant();
    }
}

bool RectentProjects::setData(const QModelIndex &index, const QVariant &value, int role)
{
    return QAbstractListModel::setData(index, value, role);
}

QHash<int, QByteArray> RectentProjects::roleNames() const
{
    return {
        {    NameRole,     "name"},
        {    PathRole,     "path"},
        { ToolTipRole,  "tooltip"},
        {SelectedRole, "selected"},
    };
}

bool RectentProjects::insertRows(int row, int count, const QModelIndex &parent)
{
    if (count < 1 || row < 0 || row > rowCount(parent))
        return false;
    beginInsertRows(QModelIndex(), row, row + count - 1);
    project_infos.insert(project_infos.begin() + row, count, ProjectBaseInfo{});
    endInsertRows();
    return true;
}

bool RectentProjects::removeRows(int row, int count, const QModelIndex &parent)
{
    if (count <= 0 || row < 0 || (row + count) > rowCount(parent))
        return false;
    beginRemoveRows(QModelIndex(), row, row + count - 1);
    project_infos.erase(project_infos.begin() + row, project_infos.begin() + row + count);
    endRemoveRows();
    return true;
}

bool RectentProjects::addProject(const QString &path)
{
    if (database_ == nullptr)
    {
        spdlog::error("添加项目到最近项目失败, 数据库未初始化: {}", path_.toUtf8().constData());
        return false;
    }
    const int row = 0;
    if (!insertRow(row))
        return false;
    ProjectBaseInfo &info = project_infos[row];

    QString err_msg;
    bool    ok = database_->addProject(path, err_msg);
    if (ok)
    {
        spdlog::info("添加最近项目: {}", path.toUtf8().constData());
        info.path = path;
        data::ProjectDataBase::getProjectBaseInfo(info.path, info.name, info.mtime, err_msg);
        emit dataChanged(index(row), index(row), {NameRole, PathRole, ToolTipRole});
        selection_->select(index(0), QItemSelectionModel::ClearAndSelect);
        return true;
    }
    else
    {
        spdlog::error("添加最近项目失败: {}, error: {}", path.toUtf8().constData(), err_msg.toUtf8().constData());
    }
    return false;
}

bool RectentProjects::updateProject(const QString &path, const QString &new_name, const qint64 new_mtime)
{
    if (database_ == nullptr)
    {
        spdlog::error("更新最近项目失败, 数据库未初始化: {}", path_.toUtf8().constData());
        return false;
    }
    const int size = static_cast<int>(project_infos.size());
    bool      ok{false};
    for (int i = 0; i < size; ++i)
    {
        if (project_infos[i].path == path)
        {
            project_infos[i].name  = new_name;
            project_infos[i].mtime = new_mtime;
            emit dataChanged(index(i), index(i), {NameRole, ToolTipRole});
            ok = true;
            break;
        }
    }
    spdlog::info("更新最近项目: {}, ok: {}", path.toUtf8().constData(), ok);
    return ok;
}

bool RectentProjects::openProject(const QString &path)
{
    if (database_ == nullptr)
    {
        spdlog::error("打开最近项目失败, 数据库未初始化: {}", path_.toUtf8().constData());
        return false;
    }
    const int size = static_cast<int>(project_infos.size());
    for (int i = 0; i < size; ++i)
    {
        ProjectBaseInfo info = project_infos[i];
        if (info.path == path)
        {
            info.path = path;
            project_infos.erase(project_infos.begin() + i);
            project_infos.insert(project_infos.begin(), info);
            spdlog::info("打开最近项目: {}", path.toUtf8().constData());
            emit dataChanged(index(0), index(i), {NameRole, PathRole, ToolTipRole});
            selection_->select(index(0), QItemSelectionModel::ClearAndSelect);
            return true;
        }
    }
    return addProject(path);
}

bool RectentProjects::removeProject(const QString &path)
{
    if (database_ == nullptr)
    {
        spdlog::error("删除最近项目失败, 数据库未初始化: {}", path_.toUtf8().constData());
        return false;
    }
    spdlog::info("删除最近项目: {}", path.toUtf8().constData());
    for (size_t i = 0; i < project_infos.size(); ++i)
    {
        const ProjectBaseInfo& info = project_infos[i];
        if (info.path == path)
        {
            const int idx = static_cast<int>(i);
            removeRow(idx);
            emit dataChanged(index(idx), index(static_cast<int>(project_infos.size() - 1)), {NameRole, PathRole, ToolTipRole});
            selection_->select(index(0), QItemSelectionModel::ClearAndSelect);
            break;
        }
    }

    QString err_msg;
    bool ok = database_->removeProject(path, err_msg);
    if (!ok)
    {
        spdlog::error("删除最近项目失败: {}, error: {}", path.toUtf8().constData(), err_msg.toUtf8().constData());
    }
    return ok;
}

bool RectentProjects::setCurrentProjectPath(const QString &path)
{
    if (path == current_path_)
        return false;
    current_path_ = path;
    emit currentProjectPathChanged();
    return true;
}

QVariant RectentProjects::getName(const QModelIndex &index) const
{
    return project_infos.at(index.row()).name;
}

QVariant RectentProjects::getPath(const QModelIndex &index) const
{
    return project_infos.at(index.row()).path;
}

QVariant RectentProjects::getTooltip(const QModelIndex &index) const
{
    const ProjectBaseInfo &info = project_infos.at(index.row());

    QString mtime = QDateTime::fromSecsSinceEpoch(info.mtime).toString("yyyy/MM/dd hh:mm");
    QString msg   = QString("%1\n路径: %2\n修改时间: %3").arg(info.name, info.path, mtime);
    return msg;
}

QVariant RectentProjects::getSelected(const QModelIndex &index) const
{
    if (selection_ == nullptr)
        return false;
    const QModelIndexList &items = selection_->selectedIndexes();
    for (const QModelIndex selected_index : items)
    {
        if (selected_index == index)
            return true;
    }
    return false;
}

void RectentProjects::updateSelection(const QItemSelection &selected, const QItemSelection &deselected)
{
    const QModelIndexList &dselected_items = deselected.indexes();
    int                    top{-1};
    int                    bottom{-1};
    for (const QModelIndex &index : dselected_items)
    {
        const int row = index.row();
        if (top == -1)
            top = row;
        else
            top = std::min(top, row);
        bottom = std::max(bottom, row);
    }
    emit dataChanged(index(top), index(bottom), {SelectedRole});

    top    = -1;
    bottom = -1;

    const QModelIndexList &selected_items = selected.indexes();
    for (const QModelIndex &index : selected_items)
    {
        const int row = index.row();
        if (top == -1)
            top = row;
        else
            top = std::min(top, row);
        bottom = std::max(bottom, row);
        setCurrentProjectPath(getPath(index).toString());
    }
    emit dataChanged(index(top), index(bottom), {SelectedRole});
}

ProjectManager::ProjectManager(QObject *parent)
    : QObject(parent)
    , recent_projects_(new RectentProjects("./history.db", this))
{
}

ProjectManager::~ProjectManager() {}

Project *ProjectManager::createProject(const QString &name, const int method, const QString &path,
                                       const QString &description, const QString image_base_path)
{
    if (current_project_)
        closeProject();
    spdlog::info("创建项目 name: {}, path: {}", name.toUtf8().constData(), path.toUtf8().constData());
    const auto &[valid, msg] = Project::isValid(method, path, true);
    if (!valid)
    {
        spdlog::error("创建项目失败: {}, error: {}", path.toUtf8().constData(), msg.toUtf8().constData());
        return nullptr;
    }
    qint64 ctime     = QDateTime::currentSecsSinceEpoch();
    current_project_ = new Project(name, method, path, description, image_base_path, ctime, ctime, this);
    current_project_->initProject();
    recent_projects_->addProject(path);
    emit currentProjectChanged();
    return current_project_;
}

Project *ProjectManager::openProject(const QString &path)
{
    if (current_project_ && current_project_->path() == path)
        return current_project_;
    spdlog::info("打开项目: {}", path.toUtf8().constData());
    const auto &[valid, msg] = Project::isValid(-1, path, false);
    if (!valid)
    {
        spdlog::error("打开项目失败: {}, error: {}", path.toUtf8().constData(), msg.toUtf8().constData());
        return nullptr;
    }
    if (current_project_)
        closeProject();
    current_project_ = new Project(path, this);
    current_project_->openProject();
    recent_projects_->openProject(current_project_->path());
    emit currentProjectChanged();
    return current_project_;
}

void ProjectManager::closeProject()
{
    if (current_project_)
    {
        spdlog::info("关闭项目: {}", current_project_->path().toUtf8().constData());
        // current_project_->deleteLater();
        delete current_project_;
        current_project_ = nullptr;
        emit currentProjectChanged();
    }
    else
    {
        spdlog::error("关闭项目失败, 当前未打开项目");
    }
}

bool ProjectManager::updateProjectBaseInfo(const QString &path, const QString &new_name, const QString &new_description)
{
    spdlog::info("更新项目基础信息: {}", path.toUtf8().constData());
    QString      err_msg;
    const qint64 mtime = QDateTime::currentSecsSinceEpoch();
    bool         ok    = data::ProjectDataBase::updateProjectBaseInfo(path, new_name, new_description, mtime, err_msg);
    if (ok)
    {
        recent_projects_->updateProject(path, new_name, mtime);
    }
    else
    {
        spdlog::error("更新项目基础信息失败: {}, error: {}", path.toUtf8().constData(), err_msg.toUtf8().constData());
    }
    return ok;
}

bool ProjectManager::deleteProject(const QString &path)
{
    if (current_project_ && current_project_->path() == path)
        closeProject();
    spdlog::info("删除项目: {}", path.toUtf8().constData());
    removeFromRectentProjects(path);
    QFile file(path);
    bool ok = file.remove();
    if (!ok)
    {
        spdlog::error("删除项目失败: {}, error: {}", path.toUtf8().constData(), file.errorString().toUtf8().constData());
    }
    return ok;
}

bool ProjectManager::removeFromRectentProjects(const QString &path)
{
    return recent_projects_->removeProject(path);
}

QString ProjectManager::isProjectValid(const int method, const QString &path, bool is_new)
{
    const auto &[valid, msg] = Project::isValid(method, path, is_new);
    return msg;
}

QVariantMap ProjectManager::getProjectInfo(const QString &path)
{
    if (path.isEmpty())
    {
        return QVariantMap({
            {"name", ""},
            {"method", -1},
            {"path", ""},
            {"description", ""},
            {"image_base_path", ""},
            {"ctime", ""},
            {"mtime", ""},
        });
    }
    spdlog::info("获取项目信息: {}", path.toUtf8().constData());
    QVariantMap project_info;
    QString     err_msg;
    bool        ok = data::ProjectDataBase::getProjectInfo(path, project_info, err_msg);
    if (!ok)
    {
        spdlog::error("获取项目信息失败, {}, error: {}", path.toUtf8().constData(), err_msg.toUtf8().constData());
    }
    return project_info;
}

} // namespace dltool::project
