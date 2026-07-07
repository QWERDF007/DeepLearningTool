#include "project/Projects.h"

#include "core/CoreDef.h"
#include "database/DataBase.h"
#include "feature/FeatureManager.h"
#include "model/ModelManager.h"
#include "settings/GlobalSettings.h"

#include <spdlog/spdlog.h>

#include <QDateTime>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QTimer>
#include <algorithm>
#include <chrono>

namespace dltool::project {

namespace {

constexpr int kDefaultMaxRecentProjects = 10;
constexpr int kMinRecentProjects        = 1;
constexpr int kMaxRecentProjects        = 50;

int configuredMaxRecentProjects()
{
    bool ok                  = false;
    int  max_recent_projects = dltool::settings::GlobalSettings::getInstance()
                                  ->valueForField(dltool::settings::generated::field::Software::MaxRecentProjects,
                                                  kDefaultMaxRecentProjects)
                                  .toInt(&ok);
    if (!ok)
        max_recent_projects = kDefaultMaxRecentProjects;
    return std::clamp(max_recent_projects, kMinRecentProjects, kMaxRecentProjects);
}

} // namespace

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
    database_ = new dltool::database::ProjectDataBase(path_, this);
}

Project::Project(const QString &path, QObject *parent)
    : QObject(parent)
    , path_(path)
{
    database_ = new dltool::database::ProjectDataBase(path_, this);
}

Project::~Project()
{
    delete feature_manager_;
    feature_manager_ = nullptr;

    if (task_manager_ != nullptr)
        task_manager_->setModelManager(nullptr);
    task_manager_ = nullptr;

    delete model_manager_;
    model_manager_ = nullptr;

    delete data_manager_;
    data_manager_ = nullptr;

    delete database_;
    database_ = nullptr;
}

void Project::init()
{
    data_manager_  = new data::DataManager(method_, database_, this);
    model_manager_ = new model::ModelManager(method_, database_, data_manager_, this);
    model::TaskManager::getInstance()->setModelManager(model_manager_);
    feature_manager_ = new dltool::feature::FeatureManager(data_manager_, this);

    // 初始化图像提供器（会自动从 QML 上下文获取引擎）
    data_manager_->initializeQmlEngine(qml_engine_);
}

void Project::initProject()
{
    spdlog::info("初始化项目, 创建数据库: {}", path_.toUtf8().constData());
    qint64  ctime = QDateTime::currentSecsSinceEpoch();
    QString err_msg;
    bool    ok = database_->initProject(name_, method_, path_, description_, image_base_path_, ctime, ctime, err_msg);
    if (!ok)
    {
        spdlog::error("初始化项目失败, error: {}", err_msg.toUtf8().constData());
    }
    init();
}

void Project::openProject()
{
    spdlog::info("打开项目: {}", path_.toUtf8().constData());
    QString err_msg;
    bool    ok = database_->openProject(name_, method_, path_, description_, image_base_path_, ctime_, mtime_, err_msg);
    if (!ok)
    {
        spdlog::error("打开项目失败, error: {}", err_msg.toUtf8().constData());
    }
    init();
}

std::tuple<bool, QString> Project::isValid(const int method, const QString &path, bool is_new)
{
    bool file_exist = QFile::exists(path);
    if (is_new)
    {
        if (file_exist)
            return {false, "项目已存在"};
        else if (!dltool::core::DeepLearningMethod::isSupportedMethod(method))
            return {false, QString("项目类型非法: %1").arg(method)};
        else
            return {true, ""};
    }
    else
    {
        if (!file_exist)
            return {false, "项目不存在"};
        auto      info           = ProjectManager::getInstance()->getProjectInfo(path);
        const int project_method = info.value("method", -1).toInt();
        if (!dltool::core::DeepLearningMethod::isSupportedMethod(project_method))
            return {false, QString("项目类型非法: %1").arg(method)};
    }
    return {true, ""};
}

RectentProjects::RectentProjects(const QString &path, QObject *parent)
    : QAbstractListModel(parent)
    , path_(path)
    , database_(new dltool::database::RecentProjectsDataBase(path_, this))
    , selection_(new QItemSelectionModel(this))
{
    init();
}

RectentProjects::~RectentProjects() {}

void RectentProjects::init()
{
    auto start = std::chrono::high_resolution_clock::now();
    if (!project_infos.empty())
        project_infos.clear();
    std::vector<QString> paths;
    QString              err_msg;
    const int            size = database_->getProjects(paths, err_msg);
    if (!err_msg.isEmpty())
    {
        spdlog::error("获取最近项目失败, error: {}", err_msg.toUtf8().constData());
        return;
    }

    beginResetModel();
    for (int i = 0; i < size; ++i)
    {
        ProjectBaseInfo info;
        info.path = paths[i];
        QString msg;
        bool    ok = dltool::database::ProjectDataBase::getProjectBaseInfo(info.path, info.name, info.mtime, msg);
        if (ok)
        {
            project_infos.emplace_back(info);
        }
        else
        {
            removeProject(info.path);
            spdlog::error("获取基础信息失败: {}, error: {}", info.path.toUtf8().constData(), msg.toUtf8().constData());
        }
    }
    std::sort(project_infos.begin(), project_infos.end(),
              [](const ProjectBaseInfo &lhs, const ProjectBaseInfo &rhs) { return lhs.mtime > rhs.mtime; });
    endResetModel();
    connect(selection_, &QItemSelectionModel::selectionChanged, this, &RectentProjects::updateSelection);
    connect(selection_, &QItemSelectionModel::currentChanged, this, &RectentProjects::onCurrentChanged);
    auto   end      = std::chrono::high_resolution_clock::now();
    double duration = std::chrono::duration<double, std::milli>(end - start).count();
    qInfo() << __FUNCTION__ << __LINE__ << "time elapsed:" << duration << "ms";
}

int RectentProjects::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return visibleProjectCount();
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
    const int visible_count = rowCount(parent);
    if (count < 1 || row < 0 || row > visible_count)
        return false;
    const bool notify_insert = visible_count < configuredMaxRecentProjects();
    if (notify_insert)
        beginInsertRows(QModelIndex(), row, row + count - 1);
    project_infos.insert(project_infos.begin() + row, count, ProjectBaseInfo{});
    if (notify_insert)
        endInsertRows();
    return true;
}

bool RectentProjects::removeRows(int row, int count, const QModelIndex &parent)
{
    const int visible_count = rowCount(parent);
    if (count <= 0 || row < 0 || (row + count) > visible_count)
        return false;
    beginRemoveRows(QModelIndex(), row, row + count - 1);
    project_infos.erase(project_infos.begin() + row, project_infos.begin() + row + count);
    endRemoveRows();
    return true;
}

bool RectentProjects::addProject(const QString &path)
{
    QString err_msg;
    bool    ok = database_->addProject(path, err_msg);
    if (ok)
    {
        spdlog::info("添加最近项目: {}", path.toUtf8().constData());
        ProjectBaseInfo info;
        info.path = path;
        dltool::database::ProjectDataBase::getProjectBaseInfo(info.path, info.name, info.mtime, err_msg);

        const int  old_visible_count = rowCount();
        const bool append_visible    = old_visible_count < configuredMaxRecentProjects();
        if (append_visible)
            beginInsertRows(QModelIndex(), 0, 0);
        else
            beginResetModel();

        project_infos.insert(project_infos.begin(), info);

        if (append_visible)
            endInsertRows();
        else
            endResetModel();

        selection_->select(index(0), QItemSelectionModel::ClearAndSelect);
        selection_->setCurrentIndex(index(0), QItemSelectionModel::ClearAndSelect);
        return true;
    }
    else
    {
        spdlog::error("添加最近项目失败: {}, error: {}", path.toUtf8().constData(), err_msg.toUtf8().constData());
    }
    return false;
}

bool RectentProjects::updateProjectBaseInfo(const QString &path, const QString &new_name, const qint64 new_mtime)
{
    const int size = static_cast<int>(project_infos.size());
    bool      ok{false};
    for (int i = 0; i < size; ++i)
    {
        if (project_infos[i].path == path)
        {
            project_infos[i].name  = new_name;
            project_infos[i].mtime = new_mtime;
            if (i < rowCount())
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
    const int size = static_cast<int>(project_infos.size());
    for (int i = 0; i < size; ++i)
    {
        ProjectBaseInfo info = project_infos[i];
        if (info.path == path) // 将对应位置的数据删除，并重新插入到队首
        {
            info.path              = path;
            const bool was_visible = i < rowCount();
            if (!was_visible)
                beginResetModel();
            project_infos.erase(project_infos.begin() + i);
            project_infos.insert(project_infos.begin(), info);
            if (was_visible)
                emit dataChanged(index(0), index(i), {NameRole, PathRole, ToolTipRole});
            else
                endResetModel();
            selection_->select(index(0), QItemSelectionModel::ClearAndSelect);
            selection_->setCurrentIndex(index(0), QItemSelectionModel::ClearAndSelect);
            return true;
        }
    }
    return addProject(path);
}

bool RectentProjects::removeProject(const QString &path)
{
    spdlog::info("删除最近项目: {}", path.toUtf8().constData());
    for (size_t i = 0; i < project_infos.size(); ++i)
    {
        const ProjectBaseInfo &info = project_infos[i];
        if (info.path == path)
        {
            const int  idx                    = static_cast<int>(i);
            const bool was_visible            = idx < rowCount();
            const bool has_hidden_replacement = static_cast<int>(project_infos.size()) > rowCount();
            selection_->clear();
            if (was_visible && has_hidden_replacement)
            {
                beginResetModel();
                project_infos.erase(project_infos.begin() + idx);
                endResetModel();
            }
            else if (was_visible)
            {
                removeRow(idx);
            }
            else
            {
                project_infos.erase(project_infos.begin() + idx);
            }
            if (project_infos.size() > 0)
            {
                selection_->select(index(0), QItemSelectionModel::ClearAndSelect);
                selection_->setCurrentIndex(index(0), QItemSelectionModel::ClearAndSelect);
            }
            else
            {
                selection_->clear();
            }
            break;
        }
    }

    QString err_msg;
    bool    ok = database_->removeProject(path, err_msg);
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

    QString mtime = info.mtime == 0 ? "" : QDateTime::fromSecsSinceEpoch(info.mtime).toString("yyyy/MM/dd hh:mm");
    QString msg   = QString("%1\n路径: %2\n修改时间: %3").arg(info.name, info.path, mtime);
    return msg;
}

QVariant RectentProjects::getSelected(const QModelIndex &index) const
{
    const QModelIndexList &items = selection_->selectedIndexes();
    for (const QModelIndex selected_index : items)
    {
        if (selected_index == index)
            return true;
    }
    return false;
}

int RectentProjects::visibleProjectCount() const
{
    return std::min(static_cast<int>(project_infos.size()), configuredMaxRecentProjects());
}

void RectentProjects::updateSelection(const QItemSelection &selected, const QItemSelection &deselected)
{
    const auto emitSelectionChanged = [this](const QItemSelection &selection)
    {
        const QModelIndexList items = selection.indexes();
        int                   top{-1};
        int                   bottom{-1};
        for (const QModelIndex &index : items)
        {
            const int row = index.row();
            if (row < 0 || row >= rowCount())
                continue;
            if (top == -1)
                top = row;
            else
                top = std::min(top, row);
            bottom = std::max(bottom, row);
        }
        if (top >= 0 && bottom >= top)
            emit dataChanged(index(top), index(bottom), {SelectedRole});
    };

    emitSelectionChanged(deselected);
    emitSelectionChanged(selected);
}

void RectentProjects::onCurrentChanged(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous)
    if (current.row() < 0 || current.row() >= rowCount())
        setCurrentProjectPath("");
    else
        setCurrentProjectPath(getPath(current).toString());
}

ProjectManager::ProjectManager(QObject *parent)
    : QObject(parent)
    , recent_projects_(
          new RectentProjects(dltool::database::DataBase::applicationDatabasePath(QStringLiteral("history.db")), this))
{
    // 尝试从 QML 上下文获取引擎
    QQmlEngine *qmlEngine = QQmlEngine::contextForObject(this) ? QQmlEngine::contextForObject(this)->engine() : nullptr;
    if (qmlEngine)
    {
        qml_engine_ = qobject_cast<QQmlApplicationEngine *>(qmlEngine);
    }
}

ProjectManager::~ProjectManager()
{
    if (current_project_)
    {
        Project *project = current_project_;
        updateProjectMtime(project->path());
        spdlog::info("关闭项目: {}", project->path().toUtf8().constData());
        current_project_ = nullptr;
        delete project;
    }
}

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
    current_project_->setQmlEngine(qml_engine_);
    current_project_->initProject();
    recent_projects_->addProject(path);
    emit currentProjectChanged();
    emit projectActivated();
    return current_project_;
}

Project *ProjectManager::openProject(const QString &path)
{
    if (current_project_ && current_project_->path() == path)
    {
        recent_projects_->openProject(path);
        emit projectActivated();
        return current_project_;
    }
    const auto &[valid, msg] = Project::isValid(-1, path, false);
    if (!valid)
    {
        spdlog::error("打开项目失败: {}, error: {}", path.toUtf8().constData(), msg.toUtf8().constData());
        return nullptr;
    }
    if (current_project_)
        closeProject();
    current_project_ = new Project(path, this);
    current_project_->setQmlEngine(qml_engine_);
    current_project_->openProject();
    recent_projects_->openProject(current_project_->path());
    emit currentProjectChanged();
    emit projectActivated();
    return current_project_;
}

void ProjectManager::closeProject()
{
    if (current_project_)
    {
        Project *project = current_project_;
        updateProjectMtime(project->path());
        spdlog::info("关闭项目: {}", project->path().toUtf8().constData());
        current_project_ = nullptr;
        emit currentProjectChanged();
        QTimer::singleShot(0, project, [project]() { project->deleteLater(); });
    }
    else
    {
        spdlog::error("关闭项目失败, 当前未打开项目");
    }
}

bool ProjectManager::updateProjectBaseInfo(const QString &path, const QString &name, const QString &description)
{
    spdlog::info("更新项目: {}", path.toUtf8().constData());
    QString      err_msg;
    const qint64 mtime = QDateTime::currentSecsSinceEpoch();
    bool         ok = dltool::database::ProjectDataBase::updateProjectBaseInfo(path, name, description, mtime, err_msg);
    if (ok)
    {
        recent_projects_->updateProjectBaseInfo(path, name, mtime);
    }
    else
    {
        spdlog::error("更新项目失败: {}, error: {}", path.toUtf8().constData(), err_msg.toUtf8().constData());
    }
    return ok;
}

void ProjectManager::updateProjectMtime(const QString &path)
{
    QVariantMap project_info;
    QString     err_msg;
    dltool::database::ProjectDataBase::getProjectInfo(path, project_info, err_msg);
    QString name        = project_info["name"].toString();
    QString description = project_info["description"].toString();
    updateProjectBaseInfo(path, name, description);
}

void ProjectManager::deleteProject(const QString &path)
{
    const auto remove_project_file = [this, path]()
    {
        spdlog::info("删除项目: {}", path.toUtf8().constData());
        removeFromRectentProjects(path);
        QFile file(path);
        bool  ok = file.remove();
        if (!ok)
        {
            spdlog::error("删除项目失败: {}, error: {}", path.toUtf8().constData(),
                          file.errorString().toUtf8().constData());
        }
    };

    if (current_project_ && current_project_->path() == path)
    {
        Project *project = current_project_;
        connect(
            project, &QObject::destroyed, this, [this, remove_project_file](QObject *)
            { QTimer::singleShot(0, this, remove_project_file); }, Qt::SingleShotConnection);
        closeProject();
        return;
    }

    remove_project_file();
}

void ProjectManager::removeFromRectentProjects(const QString &path)
{
    recent_projects_->removeProject(path);
}

QString ProjectManager::isProjectValid(const int method, const QString &path, bool is_new)
{
    const auto &[valid, msg] = Project::isValid(method, path, is_new);
    return msg;
}

QVariantMap ProjectManager::getProjectInfo(const QString &path)
{
    if (path.isEmpty() || !QFile::exists(path))
    {
        return QVariantMap({
            {           "name", ""},
            {         "method", -1},
            {           "path", ""},
            {    "description", ""},
            {"image_base_path", ""},
            {          "ctime", ""},
            {          "mtime", ""},
        });
    }
    QVariantMap project_info;
    QString     err_msg;
    bool        ok = dltool::database::ProjectDataBase::getProjectInfo(path, project_info, err_msg);
    if (!ok)
    {
        spdlog::error("获取项目信息失败, {}, error: {}", path.toUtf8().constData(), err_msg.toUtf8().constData());
    }
    return project_info;
}

QVariantMap ProjectManager::getLabelInfo(const QString &path)
{
    if (path.isEmpty() || !QFile::exists(path))
    {
        return QVariantMap({
            {         "label_classes", ""},
            {"label_instances_images", ""},
        });
    }
    QVariantMap label_info;
    QString     err_msg;
    bool        ok = dltool::database::ProjectDataBase::getLabelInfo(path, label_info, err_msg);
    if (!ok)
    {
        spdlog::error("获取项目信息失败, {}, error: {}", path.toUtf8().constData(), err_msg.toUtf8().constData());
    }
    return label_info;
}

} // namespace dltool::project
