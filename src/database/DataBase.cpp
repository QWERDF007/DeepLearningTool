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
#include <algorithm>
#include <cstdint>
#include <map>

#include <sqlite3.h>

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

namespace {

using TagIds = std::vector<int64_t>;

constexpr int kImageTagType = static_cast<int>(TagType::Image);
constexpr int kLabelTagType = static_cast<int>(TagType::Label);

// A tag relation stores the complete set of tag-class IDs for one image or label.
// Keeping the encoding here makes the database schema independent from Qt containers.
std::vector<uint8_t> encodeTagIds(const TagIds &tag_ids)
{
    std::vector<uint8_t> encoded;
    encoded.reserve(tag_ids.size() * sizeof(int64_t));
    for (const int64_t tag_id : tag_ids)
    {
        const uint64_t value = static_cast<uint64_t>(tag_id);
        for (size_t byte_index = 0; byte_index < sizeof(value); ++byte_index)
            encoded.push_back(static_cast<uint8_t>((value >> (byte_index * 8)) & 0xff));
    }
    return encoded;
}

TagIds decodeTagIds(const std::vector<uint8_t> &encoded)
{
    if (encoded.empty() || encoded.size() % sizeof(int64_t) != 0)
        return {};

    TagIds tag_ids;
    tag_ids.reserve(encoded.size() / sizeof(int64_t));
    for (size_t offset = 0; offset < encoded.size(); offset += sizeof(int64_t))
    {
        uint64_t value = 0;
        for (size_t byte_index = 0; byte_index < sizeof(value); ++byte_index)
            value |= static_cast<uint64_t>(encoded[offset + byte_index]) << (byte_index * 8);
        tag_ids.push_back(static_cast<int64_t>(value));
    }
    return tag_ids;
}

void appendTagId(TagIds &tag_ids, const int64_t tag_id)
{
    if (std::find(tag_ids.begin(), tag_ids.end(), tag_id) == tag_ids.end())
        tag_ids.push_back(tag_id);
}

bool removeTagId(TagIds &tag_ids, const int64_t tag_id)
{
    const auto found = std::remove(tag_ids.begin(), tag_ids.end(), tag_id);
    if (found == tag_ids.end())
        return false;
    tag_ids.erase(found, tag_ids.end());
    return true;
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
    return app_dir.filePath(QString("db/%1").arg(fileName));
}

bool DataBase::checkIntegrity(QString &err_msg) const
{
    sqlite3 *db = nullptr;
    int      rc = sqlite3_open_v2(path_.toUtf8().constData(), &db, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK)
    {
        const QString sqlite_message = db != nullptr ? QString::fromUtf8(sqlite3_errmsg(db))
                                                      : QString("数据库句柄为空");
        err_msg                       = QString("无法打开数据库: %1").arg(sqlite_message);
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
        const QString sqlite_message = QString::fromUtf8(sqlite3_errmsg(db));
        err_msg                      = QString("无法检查数据库: %1").arg(sqlite_message);
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
        const QString sqlite_message = QString::fromUtf8(sqlite3_errmsg(db));
        err_msg                      = QString("数据库检查失败: %1").arg(sqlite_message);
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

bool ProjectDataBase::deleteDatasetsWithContents(const std::vector<int64_t> &dataset_ids, QString &err_msg) const
{
    if (dataset_ids.empty())
    {
        return true;
    }

    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }

        std::vector<int64_t> unique_dataset_ids = dataset_ids;
        std::sort(unique_dataset_ids.begin(), unique_dataset_ids.end());
        unique_dataset_ids.erase(std::unique(unique_dataset_ids.begin(), unique_dataset_ids.end()),
                                 unique_dataset_ids.end());

        auto db = pool_->get();
        auto tx = sqlpp::start_transaction(db);
        try
        {
            // Use a subquery instead of expanding every image ID in the caller.  Apart from
            // being much smaller, this keeps all dependent-row cleanup in one SQLite transaction.
            const auto images_in_datasets
                = sqlpp::select(ImagesTable.id)
                      .from(ImagesTable)
                      .where(ImagesTable.datasetId.in(sqlpp::value_list(unique_dataset_ids)));

            const auto labels_in_datasets
                = sqlpp::select(LabelsTable.id).from(LabelsTable).where(LabelsTable.imageId.in(images_in_datasets));

            db(sqlpp::remove_from(TagsTable).where(TagsTable.labelId.in(labels_in_datasets)));
            db(sqlpp::remove_from(TagsTable).where(TagsTable.imageId.in(images_in_datasets)));
            db(sqlpp::remove_from(LabelsTable).where(LabelsTable.imageId.in(images_in_datasets)));
            db(sqlpp::remove_from(ImagesTable)
                   .where(ImagesTable.datasetId.in(sqlpp::value_list(unique_dataset_ids))));
            db(sqlpp::remove_from(DatasetsTable).where(DatasetsTable.id.in(sqlpp::value_list(unique_dataset_ids))));
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

bool ProjectDataBase::deleteImages(const std::vector<int64_t> &image_ids, QString &err_msg) const
{
    if (image_ids.empty())
    {
        return true;
    }

    try
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
            const auto labels_for_images
                = sqlpp::select(LabelsTable.id)
                      .from(LabelsTable)
                      .where(LabelsTable.imageId.in(sqlpp::value_list(image_ids)));

            db(sqlpp::remove_from(TagsTable).where(TagsTable.labelId.in(labels_for_images)));
            db(sqlpp::remove_from(TagsTable).where(TagsTable.imageId.in(sqlpp::value_list(image_ids))));
            db(sqlpp::remove_from(LabelsTable).where(LabelsTable.imageId.in(sqlpp::value_list(image_ids))));
            db(sqlpp::remove_from(ImagesTable).where(ImagesTable.id.in(sqlpp::value_list(image_ids))));
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
                                   std::vector<QString> &paths, std::vector<std::vector<uint8_t>> &extra_data,
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
        auto data = db(sqlpp::select(ImagesTable.id, ImagesTable.datasetId, ImagesTable.path, ImagesTable.extraData)
                           .from(ImagesTable)
                           .unconditionally());
        for (const auto &row : data)
        {
            dataset_ids.emplace_back(row.datasetId);
            image_ids.emplace_back(row.id);
            paths.emplace_back(QString::fromStdString(row.path));
            extra_data.emplace_back(row.extraData.is_null() ? std::vector<uint8_t>{} : row.extraData.value());
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::updateImagesExtraData(const std::vector<int64_t>              &image_ids,
                                            const std::vector<std::vector<uint8_t>> &extra_data,
                                            QString                                &err_msg) const
{
    if (pool_ == nullptr)
    {
        err_msg = QString("打开数据库失败, %1").arg(path_);
        return false;
    }
    if (image_ids.size() != extra_data.size())
    {
        err_msg = QString("图像ID数量与扩展数据数量不一致");
        return false;
    }

    auto db = pool_->get();
    auto tx = sqlpp::start_transaction(db);
    try
    {
        auto prepared_update = db.prepare(sqlpp::update(ImagesTable)
                                              .set(ImagesTable.extraData = sqlpp::parameter(ImagesTable.extraData))
                                              .where(ImagesTable.id == sqlpp::parameter(ImagesTable.id)));
        for (size_t i = 0; i < image_ids.size(); ++i)
        {
            prepared_update.params.extraData = extra_data[i];
            prepared_update.params.id        = image_ids[i];
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

bool ProjectDataBase::getAllLabelClasses(std::vector<int64_t> &label_class_ids, std::vector<QString> &names,
                                         std::vector<QString> &colors, std::vector<QString> &shortcuts,
                                         std::vector<int64_t> &ordinal_indices,
                                         std::vector<std::vector<uint8_t>> &extra_data, QString &err_msg) const
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
                                     LabelClassesTable.shortcut, LabelClassesTable.ordinalIndex,
                                     LabelClassesTable.extraData)
                           .from(LabelClassesTable)
                           .unconditionally());
        for (const auto &row : data)
        {
            label_class_ids.emplace_back(row.id);
            names.emplace_back(QString::fromStdString(row.name));
            colors.emplace_back(QString::fromStdString(row.color));
            shortcuts.emplace_back(QString::fromStdString(row.shortcut));
            ordinal_indices.emplace_back(row.ordinalIndex);
            extra_data.emplace_back(row.extraData.is_null() ? std::vector<uint8_t>{} : row.extraData.value());
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
                                    const int64_t ordinal_index, const std::vector<uint8_t> &extra_data,
                                    int64_t &label_class_id, QString &err_msg) const
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
                    LabelClassesTable.ordinalIndex = ordinal_index,
                    LabelClassesTable.extraData    = extra_data));
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
                                       const QString &shortcut, const int64_t ordinal_index,
                                       const std::vector<uint8_t> &extra_data, QString &err_msg) const
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
                    LabelClassesTable.ordinalIndex = ordinal_index,
                    LabelClassesTable.extraData    = extra_data)
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
                                       std::vector<std::vector<uint8_t>> &extra_data, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db   = pool_->get();
        auto data = db(sqlpp::select(TagClassesTable.id, TagClassesTable.name, TagClassesTable.extraData)
                           .from(TagClassesTable)
                           .unconditionally());
        for (const auto &row : data)
        {
            tag_class_ids.emplace_back(row.id);
            names.emplace_back(QString::fromStdString(row.name));
            extra_data.emplace_back(row.extraData.is_null() ? std::vector<uint8_t>{} : row.extraData.value());
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::addTagClass(const QString &name, const std::vector<uint8_t> &extra_data,
                                  int64_t &tag_class_id, QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db = pool_->get();
        db(sqlpp::insert_into(TagClassesTable)
               .set(TagClassesTable.name = name.toUtf8().constData(), TagClassesTable.extraData = extra_data));
        tag_class_id = static_cast<int64_t>(db.last_insert_id());
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::updateTagClass(const int64_t tag_class_id, const QString &name,
                                     const std::vector<uint8_t> &extra_data, QString &err_msg) const
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
               .set(TagClassesTable.name = name.toUtf8().constData(), TagClassesTable.extraData = extra_data)
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
        auto tx = sqlpp::start_transaction(db);
        try
        {
            auto data = db(sqlpp::select(TagsTable.id, TagsTable.tagIds).from(TagsTable).unconditionally());
            for (const auto &row : data)
            {
                TagIds tag_ids = decodeTagIds(row.tagIds);
                if (!removeTagId(tag_ids, tag_class_id))
                    continue;

                if (tag_ids.empty())
                {
                    db(sqlpp::remove_from(TagsTable).where(TagsTable.id == row.id));
                }
                else
                {
                    db(sqlpp::update(TagsTable)
                           .set(TagsTable.tagIds = encodeTagIds(tag_ids))
                           .where(TagsTable.id == row.id));
                }
            }
            db(sqlpp::remove_from(TagClassesTable).where(TagClassesTable.id == tag_class_id));
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

bool ProjectDataBase::getAllTags(std::vector<int64_t> &image_ids,
                                 std::vector<std::vector<int64_t>> &image_tag_ids,
                                 std::vector<int64_t> &label_ids,
                                 std::vector<std::vector<int64_t>> &label_tag_ids,
                                 QString &err_msg) const
{
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        image_ids.clear();
        image_tag_ids.clear();
        label_ids.clear();
        label_tag_ids.clear();

        auto db   = pool_->get();
        auto data = db(sqlpp::select(TagsTable.imageId, TagsTable.labelId, TagsTable.tagIds, TagsTable.type)
                           .from(TagsTable)
                           .unconditionally());
        for (const auto &row : data)
        {
            const TagIds tag_ids = decodeTagIds(row.tagIds);
            const int type = static_cast<int>(row.type.value());
            if (type == kImageTagType && !row.imageId.is_null())
            {
                image_ids.emplace_back(row.imageId.value());
                image_tag_ids.emplace_back(tag_ids);
            }
            else if (type == kLabelTagType && !row.labelId.is_null())
            {
                label_ids.emplace_back(row.labelId.value());
                label_tag_ids.emplace_back(tag_ids);
            }
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::addTagsToImages(const std::vector<int64_t> &image_ids, const int64_t tag_id,
                                      QString &err_msg) const
{
    if (image_ids.empty())
        return true;
    if (pool_ == nullptr)
    {
        err_msg = QString("打开数据库失败, %1").arg(path_);
        return false;
    }
    auto db = pool_->get();
    auto tx = sqlpp::start_transaction(db);
    try
    {
        for (const int64_t image_id : image_ids)
        {
            auto data = db(sqlpp::select(TagsTable.id, TagsTable.tagIds)
                               .from(TagsTable)
                               .where(TagsTable.imageId == image_id && TagsTable.type == kImageTagType));
            if (data.empty())
            {
                db(sqlpp::insert_into(TagsTable).set(TagsTable.imageId = image_id,
                                                     TagsTable.tagIds = encodeTagIds({tag_id}),
                                                     TagsTable.type = kImageTagType));
                continue;
            }

            const auto &row = data.front();
            TagIds tag_ids  = decodeTagIds(row.tagIds);
            const size_t old_size = tag_ids.size();
            appendTagId(tag_ids, tag_id);
            if (tag_ids.size() != old_size)
            {
                db(sqlpp::update(TagsTable)
                       .set(TagsTable.tagIds = encodeTagIds(tag_ids))
                       .where(TagsTable.id == row.id));
            }
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

bool ProjectDataBase::removeTagsFromImages(const std::vector<int64_t> &image_ids, const int64_t tag_id,
                                           QString &err_msg) const
{
    if (image_ids.empty())
        return true;
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db = pool_->get();
        auto tx = sqlpp::start_transaction(db);
        for (const int64_t image_id : image_ids)
        {
            auto data = db(sqlpp::select(TagsTable.id, TagsTable.tagIds)
                               .from(TagsTable)
                               .where(TagsTable.imageId == image_id && TagsTable.type == kImageTagType));
            if (data.empty())
                continue;

            const auto &row = data.front();
            TagIds tag_ids  = decodeTagIds(row.tagIds);
            if (!removeTagId(tag_ids, tag_id))
                continue;
            if (tag_ids.empty())
                db(sqlpp::remove_from(TagsTable).where(TagsTable.id == row.id));
            else
                db(sqlpp::update(TagsTable)
                       .set(TagsTable.tagIds = encodeTagIds(tag_ids))
                       .where(TagsTable.id == row.id));
        }
        tx.commit();
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::removeTagsForImages(const std::vector<int64_t> &image_ids, QString &err_msg) const
{
    if (image_ids.empty())
        return true;
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db = pool_->get();
        db(sqlpp::remove_from(TagsTable).where(TagsTable.imageId.in(sqlpp::value_list(image_ids))
                                               && TagsTable.type == kImageTagType));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::addTagsToLabels(const std::vector<int64_t> &label_ids, const int64_t tag_id,
                                      QString &err_msg) const
{
    if (label_ids.empty())
        return true;
    if (pool_ == nullptr)
    {
        err_msg = QString("打开数据库失败, %1").arg(path_);
        return false;
    }
    auto db = pool_->get();
    auto tx = sqlpp::start_transaction(db);
    try
    {
        for (const int64_t label_id : label_ids)
        {
            auto data = db(sqlpp::select(TagsTable.id, TagsTable.tagIds)
                               .from(TagsTable)
                               .where(TagsTable.labelId == label_id && TagsTable.type == kLabelTagType));
            if (data.empty())
            {
                db(sqlpp::insert_into(TagsTable).set(TagsTable.labelId = label_id,
                                                     TagsTable.tagIds = encodeTagIds({tag_id}),
                                                     TagsTable.type = kLabelTagType));
                continue;
            }

            const auto &row = data.front();
            TagIds tag_ids  = decodeTagIds(row.tagIds);
            const size_t old_size = tag_ids.size();
            appendTagId(tag_ids, tag_id);
            if (tag_ids.size() != old_size)
            {
                db(sqlpp::update(TagsTable)
                       .set(TagsTable.tagIds = encodeTagIds(tag_ids))
                       .where(TagsTable.id == row.id));
            }
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

bool ProjectDataBase::removeTagsFromLabels(const std::vector<int64_t> &label_ids, const int64_t tag_id,
                                           QString &err_msg) const
{
    if (label_ids.empty())
        return true;
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db = pool_->get();
        auto tx = sqlpp::start_transaction(db);
        for (const int64_t label_id : label_ids)
        {
            auto data = db(sqlpp::select(TagsTable.id, TagsTable.tagIds)
                               .from(TagsTable)
                               .where(TagsTable.labelId == label_id && TagsTable.type == kLabelTagType));
            if (data.empty())
                continue;

            const auto &row = data.front();
            TagIds tag_ids  = decodeTagIds(row.tagIds);
            if (!removeTagId(tag_ids, tag_id))
                continue;
            if (tag_ids.empty())
                db(sqlpp::remove_from(TagsTable).where(TagsTable.id == row.id));
            else
                db(sqlpp::update(TagsTable)
                       .set(TagsTable.tagIds = encodeTagIds(tag_ids))
                       .where(TagsTable.id == row.id));
        }
        tx.commit();
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::removeTagsForLabels(const std::vector<int64_t> &label_ids, QString &err_msg) const
{
    if (label_ids.empty())
        return true;
    try
    {
        if (pool_ == nullptr)
        {
            err_msg = QString("打开数据库失败, %1").arg(path_);
            return false;
        }
        auto db = pool_->get();
        db(sqlpp::remove_from(TagsTable).where(TagsTable.labelId.in(sqlpp::value_list(label_ids))
                                               && TagsTable.type == kLabelTagType));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::getAllModels(std::vector<int64_t> &model_ids, std::vector<QString> &uuids,
                                   std::vector<QString> &names, std::vector<QString> &framework_names,
                                   std::vector<QString> &model_architectures,
                                   std::vector<qint64> &ctimes, std::vector<qint64> &mtimes,
                                   std::vector<std::vector<uint8_t>> &extra_data, QString &err_msg) const
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
        framework_names.clear();
        model_architectures.clear();
        ctimes.clear();
        mtimes.clear();
        extra_data.clear();

        auto db = pool_->get();
        db.execute(SqlDef::SqlMap.at(SqlDef::CreateModels));
        auto data
            = db(sqlpp::select(ModelsTable.id, ModelsTable.uuid, ModelsTable.name, ModelsTable.frameworkName,
                               ModelsTable.modelArchitecture, ModelsTable.ctime, ModelsTable.mtime,
                               ModelsTable.extraData)
                     .from(ModelsTable)
                     .unconditionally()
                     .order_by(ModelsTable.id.asc()));
        for (const auto &row : data)
        {
            model_ids.emplace_back(row.id);
            uuids.emplace_back(QString::fromStdString(row.uuid));
            names.emplace_back(QString::fromStdString(row.name));
            framework_names.emplace_back(QString::fromStdString(row.frameworkName));
            model_architectures.emplace_back(QString::fromStdString(row.modelArchitecture));
            ctimes.emplace_back(row.ctime);
            mtimes.emplace_back(row.mtime);
            extra_data.emplace_back(row.extraData.is_null() ? std::vector<uint8_t>{} : row.extraData.value());
        }
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::addModel(const QString &uuid, const QString &name, const QString &framework_name,
                               const QString &model_architecture, const qint64 ctime, const qint64 mtime,
                               int64_t &model_id, QString &err_msg) const
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

        const QByteArray uuid_bytes               = uuid.toUtf8();
        const QByteArray name_bytes               = name.toUtf8();
        const QByteArray framework_name_bytes     = framework_name.toUtf8();
        const QByteArray model_architecture_bytes = model_architecture.toUtf8();
        db(sqlpp::insert_into(ModelsTable)
               .set(ModelsTable.uuid = uuid_bytes.constData(), ModelsTable.name = name_bytes.constData(),
                    ModelsTable.frameworkName     = framework_name_bytes.constData(),
                    ModelsTable.modelArchitecture = model_architecture_bytes.constData(),
                    ModelsTable.ctime = ctime, ModelsTable.mtime = mtime));
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

bool ProjectDataBase::updateModelExtraData(const int64_t model_id, const std::vector<uint8_t> &extra_data,
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
        db(sqlpp::update(ModelsTable)
               .set(ModelsTable.extraData = extra_data)
               .where(ModelsTable.id == model_id));
        return true;
    }
    catch (const std::exception &e)
    {
        err_msg = e.what();
        return false;
    }
}

bool ProjectDataBase::updateModelMtime(const int64_t model_id, const qint64 mtime, QString &err_msg) const
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
        db(sqlpp::update(ModelsTable)
               .set(ModelsTable.mtime = mtime)
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
    if (label_ids.empty())
    {
        return true;
    }

    try
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
            db(sqlpp::remove_from(TagsTable).where(TagsTable.labelId.in(sqlpp::value_list(label_ids))));
            db(sqlpp::remove_from(LabelsTable).where(LabelsTable.id.in(sqlpp::value_list(label_ids))));
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

} // namespace

// ── Load / save (key-value 表，每表结构相同) ──

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
        auto db = pool_->get();
        db.execute(ddl::createSettingsTableSql(table_name.toStdString()));
        return true;
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
        const QString sql = QStringLiteral("SELECT name_en AS a, value AS b FROM %1").arg(table_name);
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

                const QString sql
                    = QStringLiteral("INSERT OR REPLACE INTO %1 (name_en, value, mtime) VALUES (%2, %3, %4)")
                          .arg(table_name, sqlString(name), sqlString(value),
                               QString::number(QDateTime::currentSecsSinceEpoch()));
                db.execute(sql.toStdString());
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
