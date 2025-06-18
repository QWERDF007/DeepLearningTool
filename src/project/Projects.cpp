#include "project/Projects.h"

#include "data/DataBase.h"
#include "data/DataFormat.h"
#include "project/Datasets.h"
#include "project/ImageTags.h"
#include "project/Images.h"
#include "project/LabelClasses.h"

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
    init();
}

Project::Project(const QString &path, QObject *parent)
    : QObject(parent)
    , path_(path)
{
    init();
}

Project::~Project() {}

void Project::init()
{
    database_        = new data::ProjectDataBase(path_, this);
    datasets_        = new DatasetsListModel(database_, this);
    image_instances_ = new ImageInstancesListModel(database_, this);
    label_classes_   = new LabelClassesListModel(database_, this);
    image_tags_      = new ImageTagsListModel(database_, image_instances_, this);
    // 添加/删除图像时
    connect(image_instances_, &ImageInstancesListModel::statsChanged, datasets_, &DatasetsListModel::statsChanged);
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
    // emit datasetsChanged();
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
    // emit datasetsChanged();
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

QList<QString> Project::getAllDatasetsName() const
{
    return datasets_->getAllDatasetsName();
}

int Project::getDatasetId(const QString &dataset_name) const
{
    return datasets_->getDatasetId(dataset_name);
}

QString Project::getDatasetName(const int dataset_id) const
{
    return datasets_->getDatasetName(dataset_id);
}

void Project::addDataset(const QString &name)
{
    datasets_->addDataset(name);
}

void Project::updateDataset(const int64_t dataset_id, const QString &name)
{
    datasets_->updateDataset(dataset_id, name);
}

void Project::deleteDataset(const int64_t dataset_id)
{
    image_instances_->deleteImageInstances(dataset_id);
    datasets_->deleteDataset(dataset_id);
}

void Project::importData(const int64_t dataset_id, const int data_format, const QString &image_dir,
                         const QString &data_dir)
{
    qInfo() << __FUNCTION__ << __LINE__ << "dataset_id" << dataset_id << "data_format" << data_format << "image_dir"
            << image_dir << "data_dir" << data_dir;
    if (!data::DataFormat::isDataFormatSupported(data_format))
    {
        spdlog::error("导入数据失败, 数据格式不支持: {}", data_format);
        return;
    }
    image_instances_->addImageInstances(dataset_id, image_dir);
}

void Project::deleteSelected()
{
    image_tags_->removeImagesTags(image_instances_->getSelectedImagesId());
    image_instances_->deleteSelected();
}

QVariantMap Project::getImageInstanceInfo(const int64_t image_id)
{
    QVariantMap   info       = image_instances_->getImageInstanceInfo(image_id);
    const int64_t dataset_id = info.value("dataset_id").toInt();
    info["datasetName"]      = datasets_->getDatasetName(dataset_id);
    return info;
}

void Project::addLabelClass(const QString &name, const QString &color, const QString &shortcut)
{
    label_classes_->addLabelClass(name, color, shortcut);
}

void Project::updateLabelClass(const int64_t label_class_id, const QString &name, const QString &color,
                               const QString &shortcut, const int64_t ordinal_index)
{
    label_classes_->updateLabelClass(label_class_id, name, color, shortcut, ordinal_index);
}

void Project::deleteLabelClass(const int64_t label_class_id)
{
    // TODO: 删除对应标签类别的标注
    label_classes_->deleteLabelClass(label_class_id);
}

void Project::addTagClass(const QString &name)
{
    image_tags_->addTagClass(name);
}

RectentProjects::RectentProjects(const QString &path, QObject *parent)
    : QAbstractListModel(parent)
    , path_(path)
    , database_(new data::RecentProjectsDataBase(path_, this))
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
    spdlog::info("获取最近项目: {}", path_.toUtf8().constData());
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
        bool    ok = data::ProjectDataBase::getProjectBaseInfo(info.path, info.name, info.mtime, msg);
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
        selection_->setCurrentIndex(index(0), QItemSelectionModel::ClearAndSelect);
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
    const int size = static_cast<int>(project_infos.size());
    for (int i = 0; i < size; ++i)
    {
        ProjectBaseInfo info = project_infos[i];
        if (info.path == path) // 将对应位置的数据删除，并重新插入到队首
        {
            info.path = path;
            project_infos.erase(project_infos.begin() + i);
            project_infos.insert(project_infos.begin(), info);
            emit dataChanged(index(0), index(i), {NameRole, PathRole, ToolTipRole});
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
            const int idx = static_cast<int>(i);
            removeRow(idx);
            if (rowCount() > 0)
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
    }
    emit dataChanged(index(top), index(bottom), {SelectedRole});
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
    , recent_projects_(new RectentProjects("./history.db", this))
{
}

ProjectManager::~ProjectManager()
{
    if (current_project_)
        closeProject();
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
    current_project_->initProject();
    recent_projects_->addProject(path);
    emit currentProjectChanged();
    return current_project_;
}

Project *ProjectManager::openProject(const QString &path)
{
    if (current_project_ && current_project_->path() == path)
        return current_project_;
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
        updateProject(current_project_->path()); // 更新修改时间
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

void ProjectManager::updateProject(const QString &path, const QString &name, const QString &description)
{
    spdlog::info("更新项目: {}", path.toUtf8().constData());
    QString      err_msg;
    const qint64 mtime = QDateTime::currentSecsSinceEpoch();
    bool         ok    = data::ProjectDataBase::updateProjectBaseInfo(path, name, description, mtime, err_msg);
    if (ok)
    {
        recent_projects_->updateProject(path, name, mtime);
    }
    else
    {
        spdlog::error("更新项目失败: {}, error: {}", path.toUtf8().constData(), err_msg.toUtf8().constData());
    }
}

void ProjectManager::updateProject(const QString &path)
{
    QVariantMap project_info;
    QString     err_msg;
    data::ProjectDataBase::getProjectInfo(path, project_info, err_msg);
    QString name        = project_info["name"].toString();
    QString description = project_info["description"].toString();
    updateProject(path, name, description);
}

void ProjectManager::deleteProject(const QString &path)
{
    if (current_project_ && current_project_->path() == path)
        closeProject();
    spdlog::info("删除项目: {}", path.toUtf8().constData());
    removeFromRectentProjects(path);
    QFile file(path);
    bool  ok = file.remove();
    if (!ok)
    {
        spdlog::error("删除项目失败: {}, error: {}", path.toUtf8().constData(),
                      file.errorString().toUtf8().constData());
    }
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
