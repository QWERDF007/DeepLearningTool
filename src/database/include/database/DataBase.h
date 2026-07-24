#pragma once

#include "dltool/database/Export.h"

#include <sqlpp11/sqlite3/connection_pool.h>

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <utility>
#include <vector>

namespace dltool::database {

enum class TagType : int
{
    Image = 0,
    Label = 1,
};

class DATABASE_API DataBase : public QObject
{
public:
    DataBase(const QString &path, QObject *parent = nullptr);
    virtual ~DataBase();

    /**
     * @brief 获取数据库连接池指针
     * 
     * @return sqlpp::sqlite3::connection_pool* 数据库连接池指针
     * @details 返回当前数据库的连接池对象指针,可用于获取数据库连接进行操作
     */
    sqlpp::sqlite3::connection_pool *connectionPool()
    {
        return pool_;
    }

    QString path() const
    {
        return path_;
    }

    /**
     * @brief 连接到 SQLite 数据库
     * 
     * @param path 数据库文件路径
     * @param flags SQLite 连接标志
     * @return sqlpp::sqlite3::connection 数据库连接对象
     */
    static sqlpp::sqlite3::connection connect(const QString &path, const int flags);

    /**
     * @brief 获取应用程序目录下 db 子目录中的数据库文件路径
     *
     * @param fileName 数据库文件名，例如 settings.db 或 history.db
     * @return QString 数据库完整路径
     */
    static QString applicationDatabasePath(const QString &fileName);

    /**
     * @brief 检查数据库文件是否仍然可被 SQLite 正常读写。
     *
     * @param err_msg 检查失败时返回 SQLite 的诊断信息
     * @return true 数据库检查通过
     */
    bool checkIntegrity(QString &err_msg) const;

protected:
    QString path_;

    std::size_t capacity_{5};

    sqlpp::sqlite3::connection_pool *pool_{nullptr};

private:
    /**
     * @brief 创建数据库连接池
     * 
     * @details 该函数创建一个 SQLite 数据库连接池, 用于管理数据库连接。
     * 主要功能:
     * 1. 设置数据库文件路径和访问标志
     * 2. 创建数据库文件所在目录(如果不存在)
     * 3. 初始化连接池
     */
    void createDataBase();
};

class DATABASE_API ProjectDataBase : public DataBase
{
public:
    ProjectDataBase(const QString &path, QObject *parent = nullptr);
    ~ProjectDataBase();

    /**
     * @brief 初始化项目数据库, 创建必要的表结构并插入项目基本信息。
     * 
     * @param name 项目名称
     * @param method 项目类型
     * @param path 项目路径
     * @param description 项目描述
     * @param image_base_path 图片基础路径
     * @param ctime 创建时间
     * @param mtime 修改时间
     * @param err_msg 错误信息
     * @return true/false 初始化成功/失败
     * @details 该函数主要检查数据库连接池是否可用, 创建表并插入项目基本信息
     */
    bool initProject(const QString &name, const int method, const QString &path, const QString &description,
                     const QString image_base_path, const qint64 ctime, const qint64 mtime, QString &err_msg) const;

    /**
     * @brief 打开项目数据库, 获取项目基本信息
     * 
     * @param name 项目名称
     * @param method 项目类型
     * @param path 项目路径
     * @param description 项目描述
     * @param image_base_path 图片基础路径
     * @param ctime 创建时间
     * @param mtime 修改时间
     * @param err_msg 错误信息
     * @return true/false 打开数据库成功/失败
     */
    bool openProject(QString &name, int &method, QString &path, QString &description, QString image_base_path,
                     qint64 &ctime, qint64 &mtime, QString &err_msg) const;
    bool updateProject(const QString &name, const QString &path, const QString &description,
                       const QString &image_base_path, const qint64 mtime, QString &err_msg) const;

    static bool getProjectBaseInfo(const QString &path, QString &name, qint64 &mtime, QString &err_msg);
    static bool updateProjectBaseInfo(const QString &path, const QString &new_name, const QString &new_description,
                                      const qint64 new_mtime, QString &err_msg);

    static bool getProjectInfo(const QString &path, QVariantMap &project_info, QString &err_msg);
    static bool getLabelInfo(const QString &path, QVariantMap &label_info, QString &err_msg);

    bool getAllDatasets(std::vector<int64_t> &dataset_ids, std::vector<QString> &names, QString &err_msg) const;

    bool addDataset(const QString &name, int64_t &dataset_id, QString &err_msg) const;
    bool addDatasets(const std::vector<QString> &names, std::vector<int64_t> &dataset_ids, QString &err_msg) const;
    bool updateDataset(const int64_t dataset_id, const QString &name, QString &err_msg) const;
    bool deleteDataset(const int64_t dataset_id, QString &err_msg) const;
    /**
     * @brief 原子删除数据集及其图像、标注和 Tag 关系。
     *
     * 旧项目数据库未依赖外键级联，因此必须显式删除从属记录，避免留下孤立
     * labels/tags。该操作只访问数据库，可安全地在工作线程中执行。
     */
    bool deleteDatasetsWithContents(const std::vector<int64_t> &dataset_ids, QString &err_msg) const;

    // std::optional<int64_t> getDatasetId(const QString &name, QString &err_msg) const;

    int64_t getImagesCount(const int64_t dataset_id) const;

    bool addImages(const int64_t dataset_id, const std::vector<QString> &paths, std::vector<int64_t> &image_ids,
                   QString &err_msg) const;
    bool addImages(const std::vector<int64_t> &dataset_ids, const std::vector<QString> &paths,
                   std::vector<int64_t> &image_ids, QString &err_msg) const;
    bool updateImagesDataset(const std::vector<int64_t> &image_ids, const int64_t dataset_id, QString &err_msg) const;
    bool updateImagesDataset(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &dataset_ids,
                             QString &err_msg) const;
    bool getImage(const int64_t image_id, std::pair<int64_t, QString> &image, QString &err_msg) const;

    bool deleteImages(const std::vector<int64_t> &image_ids, QString &err_msg) const;
    bool getImages(const int64_t dataset_id, std::vector<int64_t> &image_ids, std::vector<QString> &paths,
                   QString &err_msg) const;
    bool getAllImages(std::vector<int64_t> &dataset_ids, std::vector<int64_t> &image_ids, std::vector<QString> &paths,
                      std::vector<std::vector<uint8_t>> &extra_data, QString &err_msg) const;
    bool updateImagesExtraData(const std::vector<int64_t> &image_ids,
                               const std::vector<std::vector<uint8_t>> &extra_data, QString &err_msg) const;

    bool getAllLabelClasses(std::vector<int64_t> &label_class_ids, std::vector<QString> &names,
                            std::vector<QString> &colors, std::vector<QString> &shortcuts,
                            std::vector<int64_t> &ordinal_indices,
                            std::vector<std::vector<uint8_t>> &extra_data, QString &err_msg) const;

    bool addLabelClass(const QString &name, const QString &color, const QString &shortcut, const int64_t ordinal_index,
                       const std::vector<uint8_t> &extra_data, int64_t &label_class_id, QString &err_msg) const;
    bool updateLabelClass(const int64_t label_class_id, const QString &name, const QString &color,
                          const QString &shortcut, const int64_t ordinal_index,
                          const std::vector<uint8_t> &extra_data, QString &err_msg) const;
    bool updateLabelClass(const std::vector<int64_t> &label_class_ids, const std::vector<int64_t> &ordinal_indexes,
                          QString &err_msg) const;
    bool deleteLabelClass(const int64_t label_class_id, QString &err_msg) const;

    bool getAllTagClasses(std::vector<int64_t> &tag_class_ids, std::vector<QString> &names, QString &err_msg) const;
    bool addTagClass(const QString &name, int64_t &tag_class_id, QString &err_msg) const;
    bool updateTagClass(const int64_t tag_class_id, const QString &name, QString &err_msg) const;
    bool deleteTagClass(const int64_t tag_class_id, QString &err_msg) const;

    // Each target has one row in tags; the associated tag-class IDs are returned as a vector.
    bool getAllTags(std::vector<int64_t> &image_ids, std::vector<std::vector<int64_t>> &image_tag_ids,
                    std::vector<int64_t> &label_ids, std::vector<std::vector<int64_t>> &label_tag_ids,
                    QString &err_msg) const;

    bool addTagsToImages(const std::vector<int64_t> &image_ids, int64_t tag_id, QString &err_msg) const;
    bool removeTagsFromImages(const std::vector<int64_t> &image_ids, int64_t tag_id, QString &err_msg) const;
    bool removeTagsForImages(const std::vector<int64_t> &image_ids, QString &err_msg) const;
    bool addTagsToLabels(const std::vector<int64_t> &label_ids, int64_t tag_id, QString &err_msg) const;
    bool removeTagsFromLabels(const std::vector<int64_t> &label_ids, int64_t tag_id, QString &err_msg) const;
    bool removeTagsForLabels(const std::vector<int64_t> &label_ids, QString &err_msg) const;

    bool getAllModels(std::vector<int64_t> &model_ids, std::vector<QString> &uuids, std::vector<QString> &names,
                      std::vector<QString> &framework_names, std::vector<QString> &model_architectures,
                      std::vector<qint64> &ctimes, std::vector<qint64> &mtimes,
                      std::vector<std::vector<uint8_t>> &extra_data, QString &err_msg) const;
    bool addModel(const QString &uuid, const QString &name, const QString &framework_name,
                  const QString &model_architecture, const qint64 ctime, const qint64 mtime, int64_t &model_id,
                  QString &err_msg) const;
    bool updateModelName(const int64_t model_id, const QString &name, const qint64 mtime, QString &err_msg) const;
    bool updateModelExtraData(const int64_t model_id, const std::vector<uint8_t> &extra_data,
                              QString &err_msg) const;
    bool updateModelMtime(const int64_t model_id, const qint64 mtime, QString &err_msg) const;
    bool deleteModel(const int64_t model_id, QString &err_msg) const;

    bool getAllLabels(std::vector<int64_t> &label_ids, std::vector<int64_t> &image_ids,
                      std::vector<int64_t> &label_class_ids, std::vector<int64_t> &label_types,
                      std::vector<std::vector<uint8_t>> &labels_data, QString &err_msg) const;

    bool addLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_class_ids,
                   const std::vector<int64_t> &label_types, const std::vector<std::vector<uint8_t>> &labels_data,
                   std::vector<int64_t> &label_ids, QString &err_msg) const;
    bool updateLabelsData(const std::vector<int64_t> &label_ids, const std::vector<std::vector<uint8_t>> &labels_data,
                          QString &err_msg) const;
    bool updateLabelsClass(const std::vector<int64_t> &label_ids, const std::vector<int64_t> &label_class_ids,
                           QString &err_msg) const;
    bool deleteLabels(const std::vector<int64_t> &label_ids, QString &err_msg) const;
};

class DATABASE_API RecentProjectsDataBase : public DataBase
{
public:
    RecentProjectsDataBase(const QString &path, QObject *parent = nullptr);
    ~RecentProjectsDataBase();

    bool addProject(const QString &path, QString &err_msg) const;
    bool removeProject(const QString &path, QString &err_msg) const;
    int  getProjects(std::vector<QString> &paths, QString &err_msg) const;
};

/**
 * @brief 设置数据库。
 *
 * 每个设置分类使用独立的单行表，通过列名直接读写，不再使用 EAV 键值对模式。
 * 各 Settings 类通过 "load" / "save" 前缀的方法直接操作对应表。
 */
class DATABASE_API SettingsDataBase : public DataBase
{
public:
    SettingsDataBase(const QString &path, QObject *parent = nullptr);
    ~SettingsDataBase();

    bool        ensureSettingsTable(const QString &table_name, QString &err_msg) const;
    QVariantMap loadSettings(const QString &table_name, QString &err_msg) const;
    bool        saveSettings(const QString &table_name, const QVariantMap &row, QString &err_msg) const;
};

} // namespace dltool::database
