#pragma once

#include "IModel.h"
#include "dltool/model/Export.h"
#include "model/ModelRegistry.h"

#include <QAbstractListModel>
#include <QStringList>
#include <QVariantMap>
#include <QtQml>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class QSortFilterProxyModel;
class QProcess;

namespace dltool::database {
class ProjectDataBase;
} // namespace dltool::database

namespace dltool::data {
class DataManager;
} // namespace dltool::data

namespace dltool::model {

/**
 * @brief 模型管理器，负责模型的增删改查、缓存管理和 TensorBoard 启动
 */
class MODEL_API ModelManager : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ModelManager)
    QML_UNCREATABLE("Can not create ModelManager directly!")
public:
    /**
     * @brief 构造模型管理器
     * @param method 深度学习方法
     * @param database 项目数据库
     * @param data_manager 数据管理器
     * @param parent 父对象
     */
    explicit ModelManager(const int method, dltool::database::ProjectDataBase *database,
                          dltool::data::DataManager *data_manager, QObject *parent = nullptr);
    ~ModelManager();

    Q_PROPERTY(int method READ method CONSTANT FINAL)
    Q_PROPERTY(QAbstractItemModel *userVisibleModel READ userVisibleModel CONSTANT FINAL)

    enum Role
    {
        ModelIdRole = Qt::UserRole + 1,
        UuidRole,
        NameRole,
        FrameworkNameRole,
        ModelArchitectureRole,
        CtimeRole,
        MtimeRole,
        ExtraDataRole,
    };

    /**
     * @brief 模型记录视图，对外暴露的模型元信息
     */
    struct ModelRecordView
    {
        int64_t model_id{-1};       ///< 模型 ID
        QString uuid;               ///< 模型 UUID
        QString name;               ///< 模型名称
        QString framework_name;     ///< 框架名称
        QString model_architecture; ///< 模型架构
        qint64  ctime{0};           ///< 创建时间
        qint64  mtime{0};           ///< 修改时间

        /**
         * @brief 检查记录是否有效
         * @return 有效返回 true
         */
        bool isValid() const
        {
            return !uuid.trimmed().isEmpty();
        }
    };

    /**
     * @brief 获取行数
     * @param parent 父索引
     * @return 行数
     */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    /**
     * @brief 获取指定索引的数据
     * @param index 模型索引
     * @param role 数据角色
     * @return 数据值
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    /**
     * @brief 获取角色名称映射
     * @return 角色名称哈希表
     */
    QHash<int, QByteArray> roleNames() const override;

    /**
     * @brief 添加模型
     * @param name 模型名称
     * @param framework_name 框架名称
     * @param model_architecture 模型架构名称
     * @return 添加成功返回 true
     */
    Q_INVOKABLE bool addModel(const QString &name, const QString &framework_name, const QString &model_architecture);

    /**
     * @brief 重命名模型
     * @param model_id 模型 ID
     * @param name 新名称
     * @return 操作成功返回 true
     */
    Q_INVOKABLE bool renameModel(const qint64 model_id, const QString &name);

    /**
     * @brief 校验模型名称合法性
     * @param name 模型名称
     * @return 合法返回空字符串，否则返回错误信息
     */
    Q_INVOKABLE QString validateModelName(const QString &name) const;

    /**
     * @brief 删除模型
     * @param model_id 模型 ID
     * @return 操作成功返回 true
     */
    Q_INVOKABLE bool deleteModel(const qint64 model_id);

    /**
     * @brief 复制模型
     * @param model_id 源模型 ID
     * @return 操作成功返回 true
     */
    Q_INVOKABLE bool copyModel(const qint64 model_id, bool copy_train_weights = false);

    /**
     * @brief 获取支持的框架名称列表
     * @return 框架名称列表
     */
    Q_INVOKABLE QStringList supportedFrameworks() const;

    /**
     * @brief 获取支持的模型架构列表
     * @param framework_name 框架名称
     * @return 模型架构名称列表
     */
    Q_INVOKABLE QStringList supportedModelArchitectures(const QString &framework_name) const;

    /**
     * @brief 获取可用模型名称列表
     * @return 模型名称列表
     */
    Q_INVOKABLE QStringList availableModelNames() const;

    /**
     * @brief 获取指定行的模型数据
     * @param row 行号
     * @return 模型数据键值对
     */
    Q_INVOKABLE QVariantMap modelAt(int row) const;

    /**
     * @brief 获取用户可见模型的指定行数据
     * @param row 行号
     * @return 模型数据键值对
     */
    Q_INVOKABLE QVariantMap userVisibleModelAt(int row) const;

    /**
     * @brief 根据 UUID 获取模型数据
     * @param uuid 模型 UUID
     * @return 模型数据键值对
     */
    Q_INVOKABLE QVariantMap modelRecordForUuid(const QString &uuid) const;

    /**
     * @brief 根据 UUID 获取模型记录视图
     * @param uuid 模型 UUID
     * @return 模型记录视图
     */
    ModelRecordView modelRecordViewForUuid(const QString &uuid) const;

    /**
     * @brief 根据 UUID 获取模型实例
     * @param uuid 模型 UUID
     * @return 模型实例指针
     */
    Q_INVOKABLE dltool::model::IModel *modelForUuid(const QString &uuid) const;

    /**
     * @brief 启动 TensorBoard
     * @param model_uuid 模型 UUID
     * @return TensorBoard URL
     */
    Q_INVOKABLE QString startTensorBoard(const QString &model_uuid);

    /**
     * @brief 获取深度学习方法
     * @return 方法枚举值
     */
    int method() const
    {
        return method_;
    }

    /**
     * @brief 获取用户可见模型代理
     * @return 代理模型指针
     */
    QAbstractItemModel *userVisibleModel() const;

    /**
     * @brief 获取项目目录
     * @return 项目目录路径
     */
    QString projectDirectory() const
    {
        return project_dir_;
    }

    /**
     * @brief 创建已注册模型实例
     * @param framework_name 框架名称
     * @param model_architecture 模型架构名称
     * @return 模型实例
     */
    std::unique_ptr<IModel> createRegisteredModelInstance(const QString &framework_name,
                                                          const QString &model_architecture) const;

    /**
     * @brief 获取所有已注册模型实例
     * @return 模型实例列表
     */
    std::vector<std::unique_ptr<IModel>> registeredModelInstances() const;

    /**
     * @brief 添加模型记录并持久化
     * @param name 模型名称
     * @param framework_name 框架名称
     * @param model_architecture 模型架构名称
     * @param err_msg 错误信息输出
     * @return 模型记录视图
     */
    ModelRecordView addModelRecord(const QString &name, const QString &framework_name,
                                   const QString &model_architecture, QString *err_msg = nullptr);

    /**
     * @brief 合并并持久化模型扩展数据
     * @param model_uuid 模型 UUID
     * @param updates 要合并到 extra_data 顶层的数据
     * @param err_msg 错误信息输出
     * @return 更新成功返回 true
     */
    bool updateModelExtraData(const QString &model_uuid, const QVariantMap &updates, QString *err_msg = nullptr);

    /**
     * @brief 更新模型修改时间
     * @param model_uuid 模型 UUID
     * @param err_msg 错误信息输出
     * @return 更新成功返回 true
     */
    bool touchModelModifiedTime(const QString &model_uuid, QString *err_msg = nullptr);

signals:
    /**
     * @brief 模型扩展数据发生变化
     * @param model_uuid 发生变化的模型 UUID
     */
    void modelExtraDataChanged(const QString &model_uuid);

private:
    struct ModelRecord
    {
        int64_t model_id{-1};
        QString uuid;
        QString name;
        QString framework_name;
        QString model_architecture;
        qint64  ctime{0};
        qint64  mtime{0};
        QVariantMap extra_data;
    };

    /**
     * @brief 从数据库初始化模型列表
     */
    void init();

    /**
     * @brief 根据模型 ID 查找索引
     * @param model_id 模型 ID
     * @return 索引，未找到返回 -1
     */
    int indexOfModel(const int64_t model_id) const;

    /**
     * @brief 根据 UUID 查找索引
     * @param uuid 模型 UUID
     * @return 索引，未找到返回 -1
     */
    int indexOfUuid(const QString &uuid) const;

    /**
     * @brief 生成唯一的复制名称
     * @param name 原始名称
     * @return 唯一复制名称
     */
    QString uniqueCopyName(const QString &name) const;

    /**
     * @brief 获取或创建模型缓存实例
     * @param record 模型记录
     * @return 模型实例指针
     */
    IModel *cachedModelForRecord(const ModelRecord &record) const;

    /**
     * @brief 将记录转为视图
     * @param record 模型记录
     * @return 模型记录视图
     */
    static ModelRecordView toRecordView(const ModelRecord &record);

    /**
     * @brief 初始化模型的数据集视图模型
     * @param model 模型实例
     */
    void initializeDatasetViewModels(IModel *model) const;

    /**
     * @brief 请求加载模型任务配置
     * @param model_uuid 模型 UUID
     */
    void requestModelTaskConfigLoad(const QString &model_uuid) const;

    /**
     * @brief 应用已加载的模型任务配置
     * @param model_uuid 模型 UUID
     * @param model_name 模型名称
     * @param train_params 训练参数
     * @param test_params 测试参数
     */
    void applyLoadedModelTaskConfigs(const QString &model_uuid, const QString &model_name,
                                     const QVariantMap &train_params, const QVariantMap &test_params);

    /**
     * @brief 生成实例缓存键
     * @param uuid 模型 UUID
     * @return 缓存键
     */
    static std::string instanceKey(const QString &uuid);

    /**
     * @brief 获取模型 ID 数据
     * @param index 模型索引
     * @return 模型 ID
     */
    QVariant getModelId(const QModelIndex &index) const;

    /**
     * @brief 获取 UUID 数据
     * @param index 模型索引
     * @return UUID
     */
    QVariant getUuid(const QModelIndex &index) const;

    /**
     * @brief 获取名称数据
     * @param index 模型索引
     * @return 模型名称
     */
    QVariant getName(const QModelIndex &index) const;

    /**
     * @brief 获取框架名称数据
     * @param index 模型索引
     * @return 框架名称
     */
    QVariant getFrameworkName(const QModelIndex &index) const;

    /**
     * @brief 获取模型架构数据
     * @param index 模型索引
     * @return 模型架构名称
     */
    QVariant getModelArchitecture(const QModelIndex &index) const;

    /**
     * @brief 获取创建时间数据
     * @param index 模型索引
     * @return 创建时间文本
     */
    QVariant getCtime(const QModelIndex &index) const;

    /**
     * @brief 获取修改时间数据
     * @param index 模型索引
     * @return 修改时间文本
     */
    QVariant getMtime(const QModelIndex &index) const;

    /**
     * @brief 获取扩展数据
     * @param index 模型索引
     * @return 扩展数据
     */
    QVariant getExtraData(const QModelIndex &index) const;

    /**
     * @brief 格式化时间戳为字符串
     * @param timestamp 时间戳
     * @return 时间字符串
     */
    static QString formatTimestamp(qint64 timestamp);

    dltool::database::ProjectDataBase *database_{nullptr};           ///< 数据库
    dltool::data::DataManager         *data_manager_{nullptr};       ///< 数据管理器
    QSortFilterProxyModel             *user_visible_model_{nullptr}; ///< 用户可见模型代理

    QProcess *tensorboard_process_{nullptr}; ///< TensorBoard 进程
    QString   tensorboard_model_uuid_;       ///< 当前 TensorBoard 模型 UUID

    int method_{-1}; ///< 深度学习方法

    QString project_dir_; ///< 项目目录

    std::vector<ModelRecord> models_; ///< 模型记录列表

    mutable std::unordered_map<std::string, std::unique_ptr<IModel>> model_instances_; ///< 模型实例缓存

    mutable std::unordered_set<std::string> config_load_started_; ///< 已触发配置加载的模型
};

} // namespace dltool::model
