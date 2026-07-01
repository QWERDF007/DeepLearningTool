#include "database/DataBase.h"

#include "database/SqlDef.h"
#include "database/ddl/DatasetsTable.h"
#include "database/ddl/ImagesTable.h"
#include "database/ddl/LabelClassesTable.h"
#include "database/ddl/LabelsTable.h"
#include "database/ddl/ModelsTable.h"
#include "database/ddl/ProjectTable.h"
#include "database/ddl/RecentProjectsTable.h"
#include "database/ddl/SettingsTableTemplate.h"
#include "database/ddl/TagClassesTable.h"
#include "database/ddl/TagsTable.h"

#include <sqlpp11/custom_query.h>
#include <sqlpp11/sqlpp11.h>
#include <sqlpp11/transaction.h>
#include <sqlpp11/verbatim.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QRegularExpression>
#include <QSet>
#include <map>

namespace dltool::database {

const auto ProjectTable         = Project{};
const auto RectentProjectsTable = RecentProjects{};
const auto ImagesTable          = Images{};
const auto DatasetsTable        = Datasets{};
const auto LabelClassesTable    = LabelClasses{};
const auto LabelsTable          = Labels{};
const auto TagClassesTable      = TagClasses{};
const auto TagsTable            = Tags{};
const auto ModelsTable          = Models{};

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

QString DataBase::applicationDatabasePath(const QString &fileName)
{
    const QDir app_dir(QCoreApplication::applicationDirPath());
    return app_dir.filePath(QString("db/%1").arg(fileName));
}

bool DataBase::checkIntegrity(QString &err_msg) const
{
    sqlite3 *db = nullptr;
    int      rc = sqlite3_open_v2(path_.toUtf8().constData(), &db, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK)
    {
        const char *sqlite_message = db != nullptr ? sqlite3_errmsg(db) : "数据库句柄为空";
        err_msg                    = QString("无法打开数据库: %1").arg(QString::fromUtf8(sqlite_message));
        if (db != nullptr)
        {
            sqlite3_close(db);
        }
        return false;
    }

    sqlite3_stmt *stmt = nullptr;
    rc                 = sqlite3_prepare_v2(db, "PRAGMA quick_check", -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
    {
        err_msg = QString("无法检查数据库: %1").arg(QString::fromUtf8(sqlite3_errmsg(db)));
        sqlite3_close(db);
        return false;
    }

    bool ok = true;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        const unsigned char *text = sqlite3_column_text(stmt, 0);
        const QString message = text != nullptr ? QString::fromUtf8(reinterpret_cast<const char *>(text)) : QString();
        if (message.compare(QStringLiteral("ok"), Qt::CaseInsensitive) != 0)
        {
            err_msg = message;
            ok      = false;
            break;
        }
    }

    if (ok && rc != SQLITE_DONE)
    {
        err_msg = QString("数据库检查失败: %1").arg(QString::fromUtf8(sqlite3_errmsg(db)));
        ok      = false;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
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
        db.execute(SqlDef::SqlMap.at(SqlDef::CreateDatasets));
        db.execute(SqlDef::SqlMap.at(SqlDef::CreateImages));
        db.execute(SqlDef::SqlMap.at(SqlDef::CreateLabelClasses));
        db.execute(SqlDef::SqlMap.at(SqlDef::CreateLabels));
        db.execute(SqlDef::SqlMap.at(SqlDef::CreateTagClasses));
        db.execute(SqlDef::SqlMap.at(SqlDef::CreateTags));
        db.execute(SqlDef::SqlMap.at(SqlDef::CreateModels));
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
        if (!QFile::exists(path))
            return false;
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

bool ProjectDataBase::getLabelInfo(const QString &path, QVariantMap &label_info, QString &err_msg)
{
    try
    {
        label_info.insert("label_classes", "");
        label_info.insert("label_instances_images", "");

        sqlpp::sqlite3::connection db = DataBase::connect(path, SQLITE_OPEN_READONLY);

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

bool ProjectDataBase::getAllDatasets(std::vector<int64_t> &dataset_ids, std::vector<QString> &names,
                                     QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db   = pool_->get();
        auto data = db(sqlpp::select(DatasetsTable.id, DatasetsTable.name).from(DatasetsTable).unconditionally());
        for (const auto &row : data)
        {
            dataset_ids.emplace_back(row.id);
            names.emplace_back(QString::fromStdString(row.name));
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::addDataset(const QString &name, int64_t &dataset_id, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db = pool_->get();
        db(sqlpp::insert_into(DatasetsTable).set(DatasetsTable.name = name.toUtf8().constData()));
        dataset_id = static_cast<int64_t>(db.last_insert_id());
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::addDatasets(const std::vector<QString> &names, std::vector<int64_t> &dataset_ids,
                                  QString &err_msg) const
{
    dataset_ids.clear();
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        if (names.empty())
        {
            return true;
        }

        auto db = pool_->get();
        auto tx = sqlpp::start_transaction(db);
        try
        {
            dataset_ids.reserve(names.size());
            for (const QString &name : names)
            {
                db(sqlpp::insert_into(DatasetsTable).set(DatasetsTable.name = name.toUtf8().constData()));
                dataset_ids.emplace_back(static_cast<int64_t>(db.last_insert_id()));
            }
            tx.commit();
            return true;
        }
        catch (...)
        {
            tx.rollback();
            throw;
        }
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        dataset_ids.clear();
        return false;
    }
}

// std::optional<int64_t> ProjectDataBase::getDatasetId(const QString &name, QString &err_msg) const
// {
//     try
//     {
//         if (pool_ == nullptr)
//         {
//             err_msg = QString("打开数据库失败, %1").arg(path_);
//             return std::nullopt;
//         }
//         auto db   = pool_->get();
//         auto data = db(sqlpp::select(DatasetsTable.id)
//                            .from(DatasetsTable)
//                            .where(DatasetsTable.name == name.toUtf8().toStdString()));
//         if (!data.empty())
//         {
//             const auto &row = data.front();
//             return row.id;
//         }
//         return std::nullopt;
//     }
//     catch (const std::exception &e)
//     {
//         err_msg = e.what();
//         return std::nullopt;
//     }
// }

int64_t ProjectDataBase::getImagesCount(const int64_t dataset_id) const
{
    try
    {
        auto db   = pool_->get();
        auto data = db(
            sqlpp::select(sqlpp::count(ImagesTable.id)).from(ImagesTable).where(ImagesTable.datasetId == dataset_id));
        return static_cast<int64_t>(data.front().count);
    }
    catch (const std::exception &)
    {
        return 0;
    }
}

bool ProjectDataBase::updateDataset(const int64_t dataset_id, const QString &name, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db = pool_->get();
        db(sqlpp::update(DatasetsTable)
               .set(DatasetsTable.name = name.toUtf8().constData())
               .where(DatasetsTable.id == dataset_id));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::deleteDataset(const int64_t dataset_id, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db = pool_->get();
        db(sqlpp::remove_from(DatasetsTable).where(DatasetsTable.id == dataset_id));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::addImages(const int64_t dataset_id, const std::vector<QString> &paths,
                                std::vector<int64_t> &image_ids, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db = pool_->get();
        // db(sqlpp::insert_into(DatasetsTable).set(DatasetsTable.name = name.toUtf8().constData()));
        auto tx = sqlpp::start_transaction(db);
        try
        {
            for (const auto &path : paths)
            {
                db(sqlpp::insert_into(ImagesTable)
                       .set(ImagesTable.datasetId = dataset_id, ImagesTable.path = path.toUtf8().constData()));
                image_ids.emplace_back(static_cast<int64_t>(db.last_insert_id()));
            }
            tx.commit();
        }
        catch (...)
        {
            tx.rollback();
            throw;
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::addImages(const std::vector<int64_t> &dataset_ids, const std::vector<QString> &paths,
                                std::vector<int64_t> &image_ids, QString &err_msg) const
{
    image_ids.clear();
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        if (dataset_ids.size() != paths.size())
        {
            err_msg = QString("添加图像失败: 数据集 ID 和路径数量不一致");
            return false;
        }
        if (paths.empty())
        {
            return true;
        }

        auto db = pool_->get();
        auto tx = sqlpp::start_transaction(db);
        try
        {
            image_ids.reserve(paths.size());
            for (size_t i = 0; i < paths.size(); ++i)
            {
                db(sqlpp::insert_into(ImagesTable)
                       .set(ImagesTable.datasetId = dataset_ids[i], ImagesTable.path = paths[i].toUtf8().constData()));
                image_ids.emplace_back(static_cast<int64_t>(db.last_insert_id()));
            }
            tx.commit();
            return true;
        }
        catch (...)
        {
            tx.rollback();
            throw;
        }
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::updateImagesDataset(const std::vector<int64_t> &image_ids, const int64_t dataset_id,
                                          QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败: %1").arg(path_);
            return false;
        }
        if (image_ids.empty())
        {
            return true;
        }

        auto db = pool_->get();
        db(sqlpp::update(ImagesTable)
               .set(ImagesTable.datasetId = dataset_id)
               .where(ImagesTable.id.in(sqlpp::value_list(image_ids))));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::updateImagesDataset(const std::vector<int64_t> &image_ids,
                                          const std::vector<int64_t> &dataset_ids, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败: %1").arg(path_);
            return false;
        }
        if (image_ids.size() != dataset_ids.size())
        {
            err_msg = QString("移动图像失败: 图像 ID 和数据集 ID 数量不一致");
            return false;
        }
        if (image_ids.empty())
        {
            return true;
        }

        std::map<int64_t, std::vector<int64_t>> image_ids_by_dataset;
        for (size_t i = 0; i < image_ids.size(); ++i)
        {
            image_ids_by_dataset[dataset_ids[i]].push_back(image_ids[i]);
        }

        auto db = pool_->get();
        auto tx = sqlpp::start_transaction(db);
        try
        {
            for (const auto &[dataset_id, ids] : image_ids_by_dataset)
            {
                db(sqlpp::update(ImagesTable)
                       .set(ImagesTable.datasetId = dataset_id)
                       .where(ImagesTable.id.in(sqlpp::value_list(ids))));
            }
            tx.commit();
            return true;
        }
        catch (...)
        {
            tx.rollback();
            throw;
        }
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::getImage(const int64_t image_id, std::pair<int64_t, QString> &image, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db   = pool_->get();
        auto data = db(
            sqlpp::select(ImagesTable.datasetId, ImagesTable.path).from(ImagesTable).where(ImagesTable.id == image_id));
        if (!data.empty())
        {
            const auto &row = data.front();
            image.first     = row.datasetId;
            image.second    = QString::fromStdString(row.path);
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::getImages(const std::vector<int64_t> &image_ids, std::vector<std::pair<int64_t, QString>> &images,
                                QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db   = pool_->get();
        auto data = db(sqlpp::select(ImagesTable.datasetId, ImagesTable.path)
                           .from(ImagesTable)
                           .where(ImagesTable.id.in(sqlpp::value_list(image_ids))));
        for (const auto &row : data)
        {
            images.emplace_back(std::make_pair(row.datasetId, QString::fromStdString(row.path)));
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::deleteImages(const std::vector<int64_t> &image_ids, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db = pool_->get();
        db(sqlpp::remove_from(ImagesTable).where(ImagesTable.id.in(sqlpp::value_list(image_ids))));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::getImages(const int64_t dataset_id, std::vector<int64_t> &image_ids, std::vector<QString> &paths,
                                QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db   = pool_->get();
        auto data = db(sqlpp::select(ImagesTable.id, ImagesTable.path)
                           .from(ImagesTable)
                           .where(ImagesTable.datasetId == dataset_id));

        for (const auto &row : data)
        {
            image_ids.emplace_back(row.id);
            paths.emplace_back(QString::fromStdString(row.path));
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::getAllImages(std::vector<int64_t> &dataset_ids, std::vector<int64_t> &image_ids,
                                   std::vector<QString> &paths, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db   = pool_->get();
        auto data = db(
            sqlpp::select(ImagesTable.id, ImagesTable.datasetId, ImagesTable.path).from(ImagesTable).unconditionally());
        for (const auto &row : data)
        {
            dataset_ids.emplace_back(row.datasetId);
            image_ids.emplace_back(row.id);
            paths.emplace_back(QString::fromStdString(row.path));
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::getAllLabelClasses(std::vector<int64_t> &label_class_ids, std::vector<QString> &names,
                                         std::vector<QString> &colors, std::vector<QString> &shortcuts,
                                         std::vector<int64_t> &ordinal_indices, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db   = pool_->get();
        auto data = db(sqlpp::select(LabelClassesTable.id, LabelClassesTable.name, LabelClassesTable.color,
                                     LabelClassesTable.shortcut, LabelClassesTable.ordinalIndex)
                           .from(LabelClassesTable)
                           .unconditionally());
        for (const auto &row : data)
        {
            label_class_ids.emplace_back(row.id);
            names.emplace_back(QString::fromStdString(row.name));
            colors.emplace_back(QString::fromStdString(row.color));
            shortcuts.emplace_back(QString::fromStdString(row.shortcut));
            ordinal_indices.emplace_back(row.ordinalIndex);
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::addLabelClass(const QString &name, const QString &color, const QString &shortcut,
                                    const int64_t ordinal_index, int64_t &label_class_id, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db = pool_->get();
        db(sqlpp::insert_into(LabelClassesTable)
               .set(LabelClassesTable.name         = name.toUtf8().constData(),
                    LabelClassesTable.color        = color.toUtf8().constData(),
                    LabelClassesTable.shortcut     = shortcut.toUtf8().constData(),
                    LabelClassesTable.ordinalIndex = ordinal_index));
        label_class_id = static_cast<int64_t>(db.last_insert_id());
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::updateLabelClass(const int64_t label_class_id, const QString &name, const QString &color,
                                       const QString &shortcut, const int64_t ordinal_index, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db = pool_->get();
        db(sqlpp::update(LabelClassesTable)
               .set(LabelClassesTable.name         = name.toUtf8().constData(),
                    LabelClassesTable.color        = color.toUtf8().constData(),
                    LabelClassesTable.shortcut     = shortcut.toUtf8().constData(),
                    LabelClassesTable.ordinalIndex = ordinal_index)
               .where(LabelClassesTable.id == label_class_id));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::updateLabelClass(const std::vector<int64_t> &label_class_ids,
                                       const std::vector<int64_t> &ordinal_indexes, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        if (label_class_ids.size() != ordinal_indexes.size())
        {
            err_msg = QString("标签类别ID数量与序号数量不一致");
            return false;
        }
        auto db = pool_->get();
        for (size_t i = 0; i < label_class_ids.size(); ++i)
        {
            db(sqlpp::update(LabelClassesTable)
                   .set(LabelClassesTable.ordinalIndex = ordinal_indexes[i])
                   .where(LabelClassesTable.id == label_class_ids[i]));
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::deleteLabelClass(const int64_t label_class_id, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db = pool_->get();
        db(sqlpp::remove_from(LabelClassesTable).where(LabelClassesTable.id == label_class_id));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::getAllTagClasses(std::vector<int64_t> &tag_class_ids, std::vector<QString> &names,
                                       QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db   = pool_->get();
        auto data = db(sqlpp::select(TagClassesTable.id, TagClassesTable.name).from(TagClassesTable).unconditionally());
        for (const auto &row : data)
        {
            tag_class_ids.emplace_back(row.id);
            names.emplace_back(QString::fromStdString(row.name));
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::addTagClass(const QString &name, int64_t &tag_class_id, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db = pool_->get();
        db(sqlpp::insert_into(TagClassesTable).set(TagClassesTable.name = name.toUtf8().constData()));
        tag_class_id = static_cast<int64_t>(db.last_insert_id());
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::updateTagClass(const int64_t tag_class_id, const QString &name, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db = pool_->get();
        db(sqlpp::update(TagClassesTable)
               .set(TagClassesTable.name = name.toUtf8().constData())
               .where(TagClassesTable.id == tag_class_id));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::deleteTagClass(const int64_t tag_class_id, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db = pool_->get();
        db(sqlpp::remove_from(TagClassesTable).where(TagClassesTable.id == tag_class_id));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::getAllTags(std::vector<int64_t> &image_ids, std::vector<int64_t> &tag_ids, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db   = pool_->get();
        auto data = db(sqlpp::select(TagsTable.imageId, TagsTable.tagId).from(TagsTable).unconditionally());
        for (const auto &row : data)
        {
            image_ids.emplace_back(row.imageId);
            tag_ids.emplace_back(row.tagId);
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::addImagesTag(const std::vector<int64_t> &image_ids, const int64_t tag_id, QString &err_msg) const
{
    if (pool_ == nullptr)
    {
        err_msg = QString("打开数据库失败, %1").arg(path_);
        return false;
    }
    auto db = pool_->get();
    auto tx = sqlpp::start_transaction(db);
    try
    {
        for (const auto &image_id : image_ids)
        {
            db(sqlpp::insert_into(TagsTable).set(TagsTable.imageId = image_id, TagsTable.tagId = tag_id));
        }
        tx.commit();
        return true;
    }
    catch (const std::exception &e)
    {
        tx.rollback();
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::deleteImagesTag(const std::vector<int64_t> &image_ids, const int64_t tag_id,
                                      QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db = pool_->get();
        db(sqlpp::remove_from(TagsTable).where(TagsTable.imageId.in(sqlpp::value_list(image_ids))
                                               && TagsTable.tagId == tag_id));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::deleteImagesTagsByImagesId(const std::vector<int64_t> &image_ids, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db = pool_->get();
        db(sqlpp::remove_from(TagsTable).where(TagsTable.imageId.in(sqlpp::value_list(image_ids))));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::deleteImagesTagsByTagsId(const std::vector<int64_t> &tag_ids, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db = pool_->get();
        db(sqlpp::remove_from(TagsTable).where(TagsTable.tagId.in(sqlpp::value_list(tag_ids))));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::getAllModels(std::vector<int64_t> &model_ids, std::vector<QString> &uuids,
                                   std::vector<QString> &names, std::vector<QString> &network_structures,
                                   std::vector<QString> &training_results, std::vector<QString> &test_results,
                                   std::vector<qint64> &ctimes, std::vector<qint64> &mtimes, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("open database failed: %1").arg(path_);
            return false;
        }

        model_ids.clear();
        uuids.clear();
        names.clear();
        network_structures.clear();
        training_results.clear();
        test_results.clear();
        ctimes.clear();
        mtimes.clear();

        auto db = pool_->get();
        db.execute(SqlDef::SqlMap.at(SqlDef::CreateModels));
        auto data
            = db(sqlpp::select(ModelsTable.id, ModelsTable.uuid, ModelsTable.name, ModelsTable.networkStructure,
                               ModelsTable.trainingResult, ModelsTable.testResult, ModelsTable.ctime, ModelsTable.mtime)
                     .from(ModelsTable)
                     .unconditionally()
                     .order_by(ModelsTable.id.asc()));
        for (const auto &row : data)
        {
            model_ids.emplace_back(row.id);
            uuids.emplace_back(QString::fromStdString(row.uuid));
            names.emplace_back(QString::fromStdString(row.name));
            network_structures.emplace_back(QString::fromStdString(row.networkStructure));
            training_results.emplace_back(QString::fromStdString(row.trainingResult));
            test_results.emplace_back(QString::fromStdString(row.testResult));
            ctimes.emplace_back(row.ctime);
            mtimes.emplace_back(row.mtime);
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::addModel(const QString &uuid, const QString &name, const QString &network_structure,
                               const QString &training_result, const QString &test_result, const qint64 ctime,
                               const qint64 mtime, int64_t &model_id, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("open database failed: %1").arg(path_);
            return false;
        }

        auto db = pool_->get();
        db.execute(SqlDef::SqlMap.at(SqlDef::CreateModels));

        const QByteArray uuid_bytes              = uuid.toUtf8();
        const QByteArray name_bytes              = name.toUtf8();
        const QByteArray network_structure_bytes = network_structure.toUtf8();
        const QByteArray training_result_bytes   = training_result.toUtf8();
        const QByteArray test_result_bytes       = test_result.toUtf8();
        db(sqlpp::insert_into(ModelsTable)
               .set(ModelsTable.uuid = uuid_bytes.constData(), ModelsTable.name = name_bytes.constData(),
                    ModelsTable.networkStructure = network_structure_bytes.constData(),
                    ModelsTable.trainingResult   = training_result_bytes.constData(),
                    ModelsTable.testResult = test_result_bytes.constData(), ModelsTable.ctime = ctime,
                    ModelsTable.mtime = mtime));
        model_id = static_cast<int64_t>(db.last_insert_id());
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::updateModelName(const int64_t model_id, const QString &name, const qint64 mtime,
                                      QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("open database failed: %1").arg(path_);
            return false;
        }

        auto db = pool_->get();
        db.execute(SqlDef::SqlMap.at(SqlDef::CreateModels));

        const QByteArray name_bytes = name.toUtf8();
        db(sqlpp::update(ModelsTable)
               .set(ModelsTable.name = name_bytes.constData(), ModelsTable.mtime = mtime)
               .where(ModelsTable.id == model_id));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::deleteModel(const int64_t model_id, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("open database failed: %1").arg(path_);
            return false;
        }

        auto db = pool_->get();
        db.execute(SqlDef::SqlMap.at(SqlDef::CreateModels));
        db(sqlpp::remove_from(ModelsTable).where(ModelsTable.id == model_id));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::getAllLabels(std::vector<int64_t> &label_ids, std::vector<int64_t> &image_ids,
                                   std::vector<int64_t> &label_class_ids, std::vector<int64_t> &label_types,
                                   std::vector<std::vector<uint8_t>> &labels_data, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db   = pool_->get();
        auto data = db(sqlpp::select(LabelsTable.id, LabelsTable.imageId, LabelsTable.labelClassId,
                                     LabelsTable.regionType, LabelsTable.region)
                           .from(LabelsTable)
                           .unconditionally());
        for (const auto &row : data)
        {
            label_ids.emplace_back(row.id);
            image_ids.emplace_back(row.imageId);
            label_class_ids.emplace_back(row.labelClassId);
            label_types.emplace_back(row.regionType);
            labels_data.emplace_back(row.region.value());
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::addLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_class_ids,
                                const std::vector<int64_t>              &label_types,
                                const std::vector<std::vector<uint8_t>> &labels_data, std::vector<int64_t> &label_ids,
                                QString &err_msg) const
{
    label_ids.clear();
    if (pool_ == nullptr)
    {
        err_msg = QString("打开数据库失败, %1").arg(path_);
        return false;
    }

    if (image_ids.size() != label_class_ids.size() || image_ids.size() != label_types.size()
        || image_ids.size() != labels_data.size())
    {
        err_msg = QString("标注写入参数数量不一致: image_ids=%1, label_class_ids=%2, label_types=%3, labels=%4")
                      .arg(image_ids.size())
                      .arg(label_class_ids.size())
                      .arg(label_types.size())
                      .arg(labels_data.size());
        return false;
    }

    auto db = pool_->get();
    auto tx = sqlpp::start_transaction(db);
    try
    {
        auto prepared_insert
            = db.prepare(sqlpp::insert_into(LabelsTable)
                             .set(LabelsTable.imageId      = sqlpp::parameter(LabelsTable.imageId),
                                  LabelsTable.labelClassId = sqlpp::parameter(LabelsTable.labelClassId),
                                  LabelsTable.regionType   = sqlpp::parameter(LabelsTable.regionType),
                                  LabelsTable.region       = sqlpp::parameter(LabelsTable.region)));

        for (size_t i = 0; i < image_ids.size(); ++i)
        {
            prepared_insert.params.imageId      = image_ids[i];
            prepared_insert.params.labelClassId = label_class_ids[i];
            prepared_insert.params.regionType   = label_types[i];
            prepared_insert.params.region       = labels_data[i];
            db(prepared_insert);
            label_ids.emplace_back(static_cast<int64_t>(db.last_insert_id()));
        }
        tx.commit();
        return true;
    }
    catch (const std::exception &e)
    {
        tx.rollback();
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::updateLabelsData(const std::vector<int64_t>              &label_ids,
                                       const std::vector<std::vector<uint8_t>> &labels_data, QString &err_msg) const
{
    if (pool_ == nullptr)
    {
        err_msg = QString("打开数据库失败, %1").arg(path_);
        return false;
    }
    if (label_ids.size() != labels_data.size())
    {
        err_msg
            = QString("标注更新参数数量不一致: label_ids=%1, labels=%2").arg(label_ids.size()).arg(labels_data.size());
        return false;
    }

    auto db = pool_->get();
    auto tx = sqlpp::start_transaction(db);
    try
    {
        auto prepared_update = db.prepare(sqlpp::update(LabelsTable)
                                              .set(LabelsTable.region = sqlpp::parameter(LabelsTable.region))
                                              .where(LabelsTable.id == sqlpp::parameter(LabelsTable.id)));

        for (size_t i = 0; i < label_ids.size(); ++i)
        {
            prepared_update.params.region = labels_data[i];
            prepared_update.params.id     = label_ids[i];
            db(prepared_update);
        }
        tx.commit();
        return true;
    }
    catch (const std::exception &e)
    {
        tx.rollback();
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::updateLabelsClass(const std::vector<int64_t> &label_ids,
                                        const std::vector<int64_t> &label_class_ids, QString &err_msg) const
{
    if (pool_ == nullptr)
    {
        err_msg = QString("打开数据库失败, %1").arg(path_);
        return false;
    }
    auto db = pool_->get();
    auto tx = sqlpp::start_transaction(db);
    try
    {
        for (size_t i = 0; i < label_ids.size(); ++i)
        {
            db(sqlpp::update(LabelsTable)
                   .set(LabelsTable.labelClassId = label_class_ids[i])
                   .where(LabelsTable.id == label_ids[i]));
        }
        tx.commit();
        return true;
    }
    catch (const std::exception &e)
    {
        tx.rollback();
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::deleteLabels(const std::vector<int64_t> &label_ids, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db = pool_->get();
        db(sqlpp::remove_from(LabelsTable).where(LabelsTable.id.in(sqlpp::value_list(label_ids))));
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
    if (pool_ != nullptr)
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
            err_msg = QString("打开数据库失败, %1").arg(path_);
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
            err_msg = QString("打开数据库失败, %1").arg(path_);
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

SettingsDataBase::SettingsDataBase(const QString &path, QObject *parent)
    : DataBase(path, parent)
{
}

SettingsDataBase::~SettingsDataBase() {}

namespace {

QString variantToText(const QVariant &v)
{
    if (!v.isValid() || v.isNull())
        return {};
    if (v.userType() == QMetaType::Bool)
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.userType() == QMetaType::Double || v.userType() == QMetaType::Float)
        return QString::number(v.toDouble(), 'g', 17);
    if (v.canConvert<QVariantMap>() && v.userType() != QMetaType::QString)
        return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(v.toMap())).toJson(QJsonDocument::Compact));
    if (v.canConvert<QVariantList>() && v.userType() != QMetaType::QString)
        return QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(v.toList())).toJson(QJsonDocument::Compact));
    return v.toString();
}

/// 每个 key-value 表通用的 save 逻辑
bool isValidSettingsTableName(const QString &table_name)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    return pattern.match(table_name).hasMatch();
}

QString sqlString(const QString &value)
{
    QString escaped = value;
    escaped.replace(QStringLiteral("'"), QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(escaped);
}

QString sqlNullableString(const QString &value)
{
    return value.isNull() ? QStringLiteral("NULL") : sqlString(value);
}

QString fieldText(const QVariantMap &field, const QString &key, const QString &fallback = {})
{
    return variantToText(field.value(key, fallback));
}

int fieldInt(const QVariantMap &field, const QString &key, const int fallback)
{
    return field.value(key, fallback).toInt();
}

QVariantMap normalizedField(const QVariant &field, const int ordinal_index)
{
    QVariantMap map = field.toMap();
    if (!map.contains(QStringLiteral("ordinal_index")))
        map.insert(QStringLiteral("ordinal_index"), ordinal_index);
    return map;
}

QString buildInsertSettingsRowSql(const QString &table_name, const QVariantMap &field)
{
    const QString name          = fieldText(field, QStringLiteral("name_en"));
    const QString property_name = fieldText(field, QStringLiteral("property_name"), name);
    const QString default_value
        = fieldText(field, QStringLiteral("default_value"), fieldText(field, QStringLiteral("value")));
    const QString value   = fieldText(field, QStringLiteral("value"), default_value);
    const int     visible = field.value(QStringLiteral("visible"), true).toBool() ? 1 : 0;

    return QStringLiteral(
               "INSERT INTO %1 (name_en, name_cn, property_name, value, default_value, value_type, "
               "value_range, control_type, options, options_map, section, description, visible, ordinal_index, mtime) "
               "VALUES (%2, %3, %4, %5, %6, %7, %8, %9, %10, %11, %12, %13, %14, %15, %16)")
        .arg(table_name, sqlString(name), sqlNullableString(fieldText(field, QStringLiteral("name_cn"))),
             sqlNullableString(property_name), sqlString(value), sqlNullableString(default_value),
             sqlString(fieldText(field, QStringLiteral("value_type"), QStringLiteral("string"))),
             sqlNullableString(fieldText(field, QStringLiteral("value_range"))),
             sqlNullableString(fieldText(field, QStringLiteral("control_type"), QStringLiteral("text"))),
             sqlNullableString(fieldText(field, QStringLiteral("options"))),
             sqlNullableString(fieldText(field, QStringLiteral("options_map"))),
             sqlNullableString(fieldText(field, QStringLiteral("section"))),
             sqlNullableString(fieldText(field, QStringLiteral("description"))), QString::number(visible),
             QString::number(fieldInt(field, QStringLiteral("ordinal_index"), 0)),
             QString::number(QDateTime::currentSecsSinceEpoch()));
}

QString buildUpdateSettingsSchemaSql(const QString &table_name, const QVariantMap &field)
{
    const QString name          = fieldText(field, QStringLiteral("name_en"));
    const QString property_name = fieldText(field, QStringLiteral("property_name"), name);
    const QString default_value
        = fieldText(field, QStringLiteral("default_value"), fieldText(field, QStringLiteral("value")));
    const int visible = field.value(QStringLiteral("visible"), true).toBool() ? 1 : 0;

    return QStringLiteral(
               "UPDATE %1 SET name_cn = %2, property_name = %3, default_value = %4, value_type = %5, "
               "value_range = %6, control_type = %7, options = %8, options_map = %9, section = %10, "
               "description = %11, visible = %12, ordinal_index = %13, mtime = %14 WHERE name_en = %15")
        .arg(table_name, sqlNullableString(fieldText(field, QStringLiteral("name_cn"))),
             sqlNullableString(property_name), sqlNullableString(default_value),
             sqlString(fieldText(field, QStringLiteral("value_type"), QStringLiteral("string"))),
             sqlNullableString(fieldText(field, QStringLiteral("value_range"))),
             sqlNullableString(fieldText(field, QStringLiteral("control_type"), QStringLiteral("text"))),
             sqlNullableString(fieldText(field, QStringLiteral("options"))),
             sqlNullableString(fieldText(field, QStringLiteral("options_map"))),
             sqlNullableString(fieldText(field, QStringLiteral("section"))),
             sqlNullableString(fieldText(field, QStringLiteral("description"))), QString::number(visible),
             QString::number(fieldInt(field, QStringLiteral("ordinal_index"), 0)),
             QString::number(QDateTime::currentSecsSinceEpoch()), sqlString(name));
}

template<typename Database>
bool tableColumns(Database &db, const QString &table_name, QSet<QString> &columns, QString &err_msg)
{
    try
    {
        const QString sql  = QStringLiteral("SELECT name AS a FROM pragma_table_info(%1)").arg(sqlString(table_name));
        auto          rows = db(sqlpp::custom_query(sqlpp::verbatim(sql.toStdString()))
                                    .with_result_type_of(sqlpp::select(sqlpp::value("").as(sqlpp::alias::a))));
        for (const auto &row : rows) columns.insert(QString::fromStdString(row.a));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

QVector<QPair<QString, QString>> expectedSettingsColumns()
{
    return {
        {           QStringLiteral("id"), QStringLiteral("INTEGER")},
        {      QStringLiteral("name_en"),    QStringLiteral("TEXT")},
        {      QStringLiteral("name_cn"),    QStringLiteral("TEXT")},
        {QStringLiteral("property_name"),    QStringLiteral("TEXT")},
        {        QStringLiteral("value"),    QStringLiteral("TEXT")},
        {QStringLiteral("default_value"),    QStringLiteral("TEXT")},
        {   QStringLiteral("value_type"),    QStringLiteral("TEXT")},
        {  QStringLiteral("value_range"),    QStringLiteral("TEXT")},
        { QStringLiteral("control_type"),    QStringLiteral("TEXT")},
        {      QStringLiteral("options"),    QStringLiteral("TEXT")},
        {  QStringLiteral("options_map"),    QStringLiteral("TEXT")},
        {      QStringLiteral("section"),    QStringLiteral("TEXT")},
        {  QStringLiteral("description"),    QStringLiteral("TEXT")},
        {      QStringLiteral("visible"), QStringLiteral("INTEGER")},
        {QStringLiteral("ordinal_index"), QStringLiteral("INTEGER")},
        {        QStringLiteral("mtime"), QStringLiteral("INTEGER")},
    };
}

bool hasExpectedSettingsColumns(const QSet<QString> &columns)
{
    if (columns.contains(QStringLiteral("key")))
        return false;
    for (const auto &column : expectedSettingsColumns())
    {
        if (!columns.contains(column.first))
            return false;
    }
    return true;
}

template<typename Database>
bool validateSettingsColumns(Database &db, const QString &table_name, QString &err_msg)
{
    QSet<QString> columns;
    if (!tableColumns(db, table_name, columns, err_msg))
        return false;
    if (!hasExpectedSettingsColumns(columns))
    {
        err_msg = QStringLiteral("settings table schema does not match template: %1").arg(table_name);
        return false;
    }
    return true;
}

template<typename Database>
bool rowExists(Database &db, const QString &table_name, const QString &name, QString &err_msg)
{
    try
    {
        const QString sql
            = QStringLiteral("SELECT COUNT(*) AS a FROM %1 WHERE name_en = %2").arg(table_name, sqlString(name));
        auto rows = db(sqlpp::custom_query(sqlpp::verbatim(sql.toStdString()))
                           .with_result_type_of(sqlpp::select(sqlpp::value(0).as(sqlpp::alias::a))));
        return !rows.empty() && rows.front().a > 0;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

} // namespace

// ── Load / save (key-value 表，每表结构相同) ──

// 宏：简化重复的 sqlpp11 select all rows 代码
bool SettingsDataBase::ensureSettingsTable(const QString &table_name, QString &err_msg) const
{
    if (!isValidSettingsTableName(table_name))
    {
        err_msg = QStringLiteral("invalid settings table name: %1").arg(table_name);
        return false;
    }
    if (pool_ == nullptr)
    {
        err_msg = QStringLiteral("settings database is not open: %1").arg(path_);
        return false;
    }

    try
    {
        auto          db = pool_->get();
        QSet<QString> columns;
        if (!tableColumns(db, table_name, columns, err_msg))
            return false;
        if (!columns.isEmpty() && !hasExpectedSettingsColumns(columns))
            db.execute(QStringLiteral("DROP TABLE IF EXISTS %1").arg(table_name).toStdString());
        db.execute(ddl::createSettingsTableSql(table_name.toStdString()));
        return validateSettingsColumns(db, table_name, err_msg);
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool SettingsDataBase::syncSettingsSchema(const QString &table_name, const QVariantList &fields, QString &err_msg) const
{
    if (!ensureSettingsTable(table_name, err_msg))
        return false;

    try
    {
        auto db = pool_->get();
        auto tx = sqlpp::start_transaction(db);
        try
        {
            for (int i = 0; i < fields.size(); ++i)
            {
                const QVariantMap field = normalizedField(fields.at(i), i);
                const QString     name  = fieldText(field, QStringLiteral("name_en"));
                if (name.isEmpty())
                    continue;

                if (rowExists(db, table_name, name, err_msg))
                    db.execute(buildUpdateSettingsSchemaSql(table_name, field).toStdString());
                else
                    db.execute(buildInsertSettingsRowSql(table_name, field).toStdString());
            }
            tx.commit();
            return true;
        }
        catch (...)
        {
            tx.rollback();
            throw;
        }
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

QVariantMap SettingsDataBase::loadSettings(const QString &table_name, QString &err_msg) const
{
    QVariantMap result;
    if (!ensureSettingsTable(table_name, err_msg))
        return result;

    try
    {
        auto          db  = pool_->get();
        const QString sql = QStringLiteral("SELECT name_en AS a, value AS b FROM %1 ORDER BY ordinal_index ASC, id ASC")
                                .arg(table_name);
        auto rows = db(sqlpp::custom_query(sqlpp::verbatim(sql.toStdString()))
                           .with_result_type_of(sqlpp::select(sqlpp::value("").as(sqlpp::alias::a),
                                                              sqlpp::value("").as(sqlpp::alias::b))));
        for (const auto &row : rows) result.insert(QString::fromStdString(row.a), QString::fromStdString(row.b));
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
    }
    return result;
}

bool SettingsDataBase::saveSettings(const QString &table_name, const QVariantMap &row, QString &err_msg) const
{
    if (!ensureSettingsTable(table_name, err_msg))
        return false;

    try
    {
        auto db = pool_->get();
        auto tx = sqlpp::start_transaction(db);
        try
        {
            for (auto it = row.cbegin(); it != row.cend(); ++it)
            {
                const QString name  = it.key();
                const QString value = variantToText(it.value());
                if (name.isEmpty())
                    continue;

                if (rowExists(db, table_name, name, err_msg))
                {
                    const QString sql = QStringLiteral("UPDATE %1 SET value = %2, mtime = %3 WHERE name_en = %4")
                                            .arg(table_name, sqlString(value),
                                                 QString::number(QDateTime::currentSecsSinceEpoch()), sqlString(name));
                    db.execute(sql.toStdString());
                }
                else
                {
                    const QVariantMap field{
                        {      QStringLiteral("name_en"),                     name},
                        {        QStringLiteral("value"),                    value},
                        {QStringLiteral("default_value"),                    value},
                        {   QStringLiteral("value_type"), QStringLiteral("string")},
                        { QStringLiteral("control_type"),   QStringLiteral("text")},
                        {      QStringLiteral("visible"),                     true},
                        {QStringLiteral("ordinal_index"),                        0},
                    };
                    db.execute(buildInsertSettingsRowSql(table_name, field).toStdString());
                }
            }
            tx.commit();
            return true;
        }
        catch (...)
        {
            tx.rollback();
            throw;
        }
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

} // namespace dltool::database
