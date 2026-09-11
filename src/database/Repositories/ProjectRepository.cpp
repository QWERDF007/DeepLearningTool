#include "ProjectRepository.h"

#include "database/DataBase.h"
#include "database/DatabaseSchema.h"
#include "database/ddl/LabelClassesTable.h"
#include "database/ddl/LabelsTable.h"
#include "database/ddl/ImagesTable.h"
#include "database/ddl/ProjectTable.h"

#include <sqlpp11/sqlpp11.h>

#include <QDateTime>
#include <QFile>

namespace dltool::database {

namespace {
const auto ProjectTable      = Project{};
const auto ImagesTable       = Images{};
const auto LabelClassesTable = LabelClasses{};
const auto LabelsTable       = Labels{};
} // namespace

bool ProjectRepository::initProject(DatabaseContext &context, const QString &name, const int method,
                                    const QString &path, const QString &description, const QString image_base_path,
                                    const qint64 ctime, const qint64 mtime, QString &err_msg)
{
    try
    {
        auto &db = context.db();
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

bool ProjectRepository::openProject(DatabaseContext &context, const QString &db_path, QString &name, int &method,
                                    QString &path, QString &description, QString image_base_path, qint64 &ctime,
                                    qint64 &mtime, QString &err_msg)
{
    try
    {
        auto &db = context.db();
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
        return path == db_path;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectRepository::updateProject(DatabaseContext &context, const QString &name, const QString &path,
                                      const QString &description, const QString &image_base_path, const qint64 mtime,
                                      QString &err_msg)
{
    try
    {
        auto &db = context.db();
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

bool ProjectRepository::getProjectBaseInfo(const QString &path, QString &name, qint64 &mtime, QString &err_msg)
{
    try
    {
        if (!QFile::exists(path))
            return false;
        sqlpp::sqlite3::connection db = DataBase::connect(path, SQLITE_OPEN_READONLY);
        if (!detail::ensureSchema(db.native_handle(), detail::SchemaKind::Project, &err_msg))
            return false;
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

bool ProjectRepository::updateProjectBaseInfo(const QString &path, const QString &new_name,
                                              const QString &new_description, const qint64 new_mtime, QString &err_msg)
{
    try
    {
        sqlpp::sqlite3::connection db = DataBase::connect(path, SQLITE_OPEN_READWRITE);
        if (!detail::ensureSchema(db.native_handle(), detail::SchemaKind::Project, &err_msg))
            return false;
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

bool ProjectRepository::getProjectInfo(const QString &path, QVariantMap &project_info, QString &err_msg)
{
    try
    {
        sqlpp::sqlite3::connection db = DataBase::connect(path, SQLITE_OPEN_READONLY);
        if (!detail::ensureSchema(db.native_handle(), detail::SchemaKind::Project, &err_msg))
            return false;

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

bool ProjectRepository::getLabelInfo(const QString &path, QVariantMap &label_info, QString &err_msg)
{
    try
    {
        label_info.insert("label_classes", "");
        label_info.insert("label_instances_images", "");

        sqlpp::sqlite3::connection db = DataBase::connect(path, SQLITE_OPEN_READONLY);
        if (!detail::ensureSchema(db.native_handle(), detail::SchemaKind::Project, &err_msg))
            return false;

        QString classes_info;
        int     label_classes_cnt = 0;
        // 查询每个类别的标注实例数量
        auto    query1 = sqlpp::select(LabelClassesTable.name, count(LabelsTable.id))
                          .from(LabelClassesTable.left_outer_join(LabelsTable)
                                    .on(LabelClassesTable.id == LabelsTable.labelClassId))
                          .unconditionally()
                          .group_by(LabelClassesTable.id, LabelClassesTable.name)
                          .order_by(count(LabelsTable.id).desc());
        for (const auto &row : db(query1))
        {
            ++label_classes_cnt;
            classes_info
                += QString("%1 (%2), ").arg(QString::fromStdString(row.name)).arg(static_cast<int64_t>(row.count));
        }
        if (label_classes_cnt)
        {
            classes_info.chop(2);
            label_info["label_classes"] = QString("%1 : %2").arg(label_classes_cnt).arg(classes_info);
        }

        QString image_instances_info;
        int     image_cnt{0}, labelled_image_cnt{0}, label_cnt{0};
        // 查询每个图像的标注实例数量
        auto    query2 = sqlpp::select(LabelsTable.imageId, count(LabelsTable.id))
                          .from(LabelsTable)
                          .unconditionally()
                          .group_by(LabelsTable.imageId);
        for (const auto &row : db(query2))
        {
            ++labelled_image_cnt;
            label_cnt += row.count;
        }
        // 查询图像数量
        auto query3 = sqlpp::select(count(ImagesTable.id)).from(ImagesTable).unconditionally();
        auto data   = db(query3);
        if (!data.empty())
        {
            image_cnt = data.front().count;
        }

        image_instances_info
            = QString("%1 个实例在 %2 张图像中 / %3 图像").arg(label_cnt).arg(labelled_image_cnt).arg(image_cnt);
        label_info["label_instances_images"] = image_instances_info;
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

} // namespace dltool::database
