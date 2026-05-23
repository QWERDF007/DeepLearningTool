#include "database/DataBase.h"

#include "database/SqlDef.h"
#include "database/ddl/DatasetsTable.h"
#include "database/ddl/ImagesTable.h"
#include "database/ddl/LabelClassesTable.h"
#include "database/ddl/LabelsTable.h"
#include "database/ddl/ProjectTable.h"
#include "database/ddl/RecentProjectsTable.h"
#include "database/ddl/SettingsTable.h"
#include "database/ddl/TagClassesTable.h"
#include "database/ddl/TagsTable.h"

#include <sqlpp11/sqlpp11.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaType>

#include <sqlite3.h>

namespace dltool::database {

const auto ProjectTable         = Project{};
const auto RectentProjectsTable = RecentProjects{};
const auto SettingsTable        = Settings{};
const auto ImagesTable          = Images{};
const auto DatasetsTable        = Datasets{};
const auto LabelClassesTable    = LabelClasses{};
const auto LabelsTable          = Labels{};
const auto TagClassesTable      = TagClasses{};
const auto TagsTable            = Tags{};

namespace {

QString settingValueType(const QVariant &value)
{
    switch (value.userType())
    {
    case QMetaType::Bool:
        return QStringLiteral("bool");
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return QStringLiteral("integer");
    case QMetaType::Float:
    case QMetaType::Double:
        return QStringLiteral("double");
    default:
        return QStringLiteral("string");
    }
}

QString settingValueToText(const QVariant &value)
{
    switch (value.userType())
    {
    case QMetaType::Bool:
        return value.toBool() ? QStringLiteral("1") : QStringLiteral("0");
    case QMetaType::Float:
    case QMetaType::Double:
        return QString::number(value.toDouble(), 'g', 17);
    default:
        return value.toString();
    }
}

QVariant settingValueFromText(const QString &text, const QString &type, const QVariant &default_value)
{
    bool ok = false;
    if (type == QStringLiteral("bool"))
    {
        return text == QStringLiteral("1") || text.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
    }
    if (type == QStringLiteral("integer"))
    {
        if (default_value.userType() == QMetaType::Int)
        {
            const int value = text.toInt(&ok);
            return ok ? QVariant(value) : default_value;
        }

        const qlonglong value = text.toLongLong(&ok);
        return ok ? QVariant(value) : default_value;
    }
    if (type == QStringLiteral("double"))
    {
        const double value = text.toDouble(&ok);
        return ok ? QVariant(value) : default_value;
    }
    return text;
}

} // namespace

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
    return app_dir.filePath(QStringLiteral("db/%1").arg(fileName));
}

bool DataBase::checkIntegrity(QString &err_msg) const
{
    sqlite3 *db = nullptr;
    int      rc = sqlite3_open_v2(path_.toUtf8().constData(), &db, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK)
    {
        const char *sqlite_message = db != nullptr ? sqlite3_errmsg(db) : "数据库句柄为空";
        err_msg = QStringLiteral("无法打开数据库: %1").arg(QString::fromUtf8(sqlite_message));
        if (db != nullptr)
        {
            sqlite3_close(db);
        }
        return false;
    }

    sqlite3_stmt *stmt = nullptr;
    rc = sqlite3_prepare_v2(db, "PRAGMA quick_check", -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
    {
        err_msg = QStringLiteral("无法检查数据库: %1").arg(QString::fromUtf8(sqlite3_errmsg(db)));
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
        err_msg = QStringLiteral("数据库检查失败: %1").arg(QString::fromUtf8(sqlite3_errmsg(db)));
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
        err_msg = QStringLiteral("标注写入参数数量不一致: image_ids=%1, label_class_ids=%2, label_types=%3, labels=%4")
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
        auto prepared_insert = db.prepare(sqlpp::insert_into(LabelsTable)
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
        err_msg = QStringLiteral("标注更新参数数量不一致: label_ids=%1, labels=%2")
                      .arg(label_ids.size())
                      .arg(labels_data.size());
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
    if (pool_ != nullptr)
    {
        auto db = pool_->get();
        db.execute(SqlDef::SqlMap.at(SqlDef::CreateSettings));
    }
}

SettingsDataBase::~SettingsDataBase() {}

QVariant SettingsDataBase::value(const QString &group, const QString &key, const QVariant &default_value,
                                 QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败: %1").arg(path_);
            return default_value;
        }

        const std::string group_name  = group.toUtf8().toStdString();
        const std::string setting_key = key.toUtf8().toStdString();

        auto db   = pool_->get();
        auto data = db(sqlpp::select(SettingsTable.settingValue, SettingsTable.valueType)
                           .from(SettingsTable)
                           .where(SettingsTable.groupName == group_name && SettingsTable.settingKey == setting_key));
        if (data.empty())
        {
            return default_value;
        }

        const auto &row = data.front();
        return settingValueFromText(QString::fromStdString(row.settingValue), QString::fromStdString(row.valueType),
                                    default_value);
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return default_value;
    }
}

bool SettingsDataBase::setValue(const QString &group, const QString &key, const QVariant &value, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败: %1").arg(path_);
            return false;
        }

        const std::string group_name    = group.toUtf8().toStdString();
        const std::string setting_key   = key.toUtf8().toStdString();
        const std::string setting_value = settingValueToText(value).toUtf8().toStdString();
        const std::string value_type    = settingValueType(value).toUtf8().toStdString();
        const qint64      mtime         = QDateTime::currentSecsSinceEpoch();

        auto db   = pool_->get();
        auto data = db(sqlpp::select(SettingsTable.id)
                           .from(SettingsTable)
                           .where(SettingsTable.groupName == group_name && SettingsTable.settingKey == setting_key));
        if (data.empty())
        {
            db(sqlpp::insert_into(SettingsTable)
                   .set(SettingsTable.groupName = group_name, SettingsTable.settingKey = setting_key,
                        SettingsTable.settingValue = setting_value, SettingsTable.valueType = value_type,
                        SettingsTable.mtime = mtime));
        }
        else
        {
            const auto id = data.front().id;
            db(sqlpp::update(SettingsTable)
                   .set(SettingsTable.settingValue = setting_value, SettingsTable.valueType = value_type,
                        SettingsTable.mtime = mtime)
                   .where(SettingsTable.id == id));
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

} // namespace dltool::database
