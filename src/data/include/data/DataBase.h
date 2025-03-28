#pragma once

#include "DataExport.h"

#include <sqlpp11/sqlite3/connection_pool.h>

#include <QObject>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace dltool::data {

class DATA_API DataBase : public QObject
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

    /**
     * @brief 连接到 SQLite 数据库
     * 
     * @param path 数据库文件路径
     * @param flags SQLite 连接标志
     * @return sqlpp::sqlite3::connection 数据库连接对象
     */
    static sqlpp::sqlite3::connection connect(const QString &path, const int flags);

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

class DATA_API ProjectDataBase : public DataBase
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

    std::vector<std::pair<int, QString>> getAllDatasets(QString &err_msg) const;

    bool addDataset(const QString &name, int64_t &dataset_id, QString &err_msg) const;
    bool updateDataset(const QString &old_name, const QString &new_name, QString &err_msg) const;
    bool deleteDataset(const int64_t dataset_id, QString &err_msg) const;

    std::optional<int64_t> getDatasetId(const QString &name, QString &err_msg) const;

    int64_t getImagesCount(const int64_t dataset_id) const;

    bool addImages(const int64_t dataset_id, const std::vector<QString> &paths, std::vector<int64_t> &image_ids,
                   QString &err_msg) const;
    bool getImage(const int64_t image_id, std::pair<int64_t, QString> &image, QString &err_msg) const;
    bool getImages(const std::vector<int64_t> &image_ids, std::vector<std::pair<int64_t, QString>> &images,
                   QString &err_msg) const;
    bool deleteImages(const std::vector<int64_t> &image_ids, QString &err_msg) const;
    std::vector<std::pair<int64_t, QString>> getImages(const int64_t dataset_id, QString &err_msg) const;
    std::map<int64_t, std::vector<std::pair<int64_t, QString>>> getAllImages(QString &err_msg) const;
};

class DATA_API RecentProjectsDataBase : public DataBase
{
public:
    RecentProjectsDataBase(const QString &path, QObject *parent = nullptr);
    ~RecentProjectsDataBase();

    bool addProject(const QString &path, QString &err_msg) const;
    bool removeProject(const QString &path, QString &err_msg) const;
    int  getProjects(std::vector<QString> &paths, QString &err_msg) const;
};

} // namespace dltool::data
