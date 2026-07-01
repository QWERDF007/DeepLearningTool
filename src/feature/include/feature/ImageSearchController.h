#pragma once

#include "dltool/feature/Export.h"
#include "settings/SettingsKeys.h"

#include <inferrt/features/ImageSearch.hpp>
#include <inferrt/features/RoiSearch.hpp>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <set>
#include <vector>

namespace dltool::feature {

class ImageSearchDataProvider;

class FEATURE_API ImageSearchController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ImageSearchController)
    QML_UNCREATABLE("Can not create ImageSearchController directly!")

    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged FINAL)
    Q_PROPERTY(bool hasResults READ hasResults NOTIFY resultsChanged FINAL)
    Q_PROPERTY(int resultCount READ resultCount NOTIFY resultsChanged FINAL)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged FINAL)
    Q_PROPERTY(QString lastSummary READ lastSummary NOTIFY resultsChanged FINAL)

public:
    /**
     * @brief 构造函数（默认使用图像搜索设置）
     * @param data_provider 图像搜索数据提供者
     * @param parent 父对象
     */
    explicit ImageSearchController(ImageSearchDataProvider *data_provider, QObject *parent = nullptr);
    ~ImageSearchController() override = default;

    /**
     * @brief 功能是否启用
     * @return 启用返回 true
     */
    bool enabled() const;

    /**
     * @brief 是否正在运行搜索
     * @return 运行中返回 true
     */
    bool isRunning() const;

    /**
     * @brief 是否有搜索结果
     * @return 有结果返回 true
     */
    bool hasResults() const;

    /**
     * @brief 获取搜索结果数量
     * @return 结果数量
     */
    int resultCount() const;

    /**
     * @brief 获取最后一次错误信息
     * @return 错误信息文本
     */
    QString lastError() const;

    /**
     * @brief 获取最后一次搜索摘要
     * @return 搜索摘要文本
     */
    QString lastSummary() const;

    /**
     * @brief 以当前选中的图像作为查询进行搜索
     * @param dataset_ids 数据集 ID 列表
     * @return 启动成功返回 true
     */
    Q_INVOKABLE bool searchSelectedImages(const QVariantList &dataset_ids);

    /**
     * @brief 以指定 ID 列表作为查询进行搜索
     * @param ids 图像/标注 ID 列表
     * @param dataset_ids 数据集 ID 列表
     * @return 启动成功返回 true
     */
    Q_INVOKABLE virtual bool search(const QVariantList &ids, const QVariantList &dataset_ids);

signals:
    void enabledChanged();
    void runningChanged();
    void resultsChanged();
    void lastErrorChanged();

    /**
     * @brief 构建进度变化信号
     * @param processedCount 已处理数量
     * @param totalCount 总数量
     */
    void buildProgressChanged(int processedCount, int totalCount);

protected:
    /**
     * @brief 构造函数（子类指定设置访问键）
     * @param data_provider 图像搜索数据提供者
     * @param settings_accessor 设置访问键
     * @param parent 父对象
     */
    explicit ImageSearchController(ImageSearchDataProvider                 *data_provider,
                                   dltool::settings::generated::AccessorKey settings_accessor,
                                   QObject                                 *parent = nullptr);

    /// 搜索请求结构体
    struct SearchRequest
    {
        QString weights_file; ///< 模型权重文件路径
        QString index_file;   ///< 特征索引文件路径

        bool rebuild_index{false}; ///< 是否重建索引
        int  top_k{5};             ///< 返回结果数量

        irt::features::ImageSearchConfig image_config; ///< 图像搜索配置
        irt::features::RoiSearchConfig   roi_config;   ///< ROI 搜索配置

        std::chrono::steady_clock::time_point started_at; ///< 搜索开始时间

        std::vector<std::filesystem::path>          query_images;   ///< 查询图像路径列表
        std::vector<irt::features::ImageSearchItem> gallery_images; ///< 搜索库图像条目列表

        std::vector<irt::features::RoiSearchItem> query_rois;   ///< 查询 ROI 列表
        std::vector<irt::features::RoiSearchItem> gallery_rois; ///< 搜索库 ROI 列表

        QPointer<ImageSearchController> controller; ///< 控制器指针（线程安全）
    };

    /// 搜索响应结构体
    struct SearchResponse
    {
        bool                 success{false}; ///< 是否成功
        QString              error;          ///< 错误信息
        QString              summary;        ///< 搜索摘要
        qint64               elapsed_ms{0};  ///< 耗时（毫秒）
        std::vector<int64_t> result_ids;     ///< 结果 ID 列表
    };

    using BuildProgressCallback = std::function<void(const irt::features::ImageSearchBuildProgress &)>;

    ImageSearchDataProvider *data_provider_{nullptr}; ///< 数据提供者

    /**
     * @brief 解析数据集 ID 列表
     * @param dataset_ids QVariantList 格式的数据集 ID
     * @return 去重后的数据集 ID 集合
     */
    static std::set<int64_t> parseDatasetIds(const QVariantList &dataset_ids);

    /**
     * @brief 验证权重文件是否存在
     * @param path 权重文件路径
     * @return 文件存在返回 true
     */
    bool validateWeightsFile(const QString &path);

    /**
     * @brief 获取当前控制器对应的设置访问键
     * @return 设置访问键
     */
    virtual dltool::settings::generated::AccessorKey settingsAccessor() const;
    /**
     * @brief 获取请求对应的模型名称
     * @param req 搜索请求
     * @return 模型名称
     */
    virtual QString                                  modelNameForRequest(const SearchRequest &req) const;

    /**
     * @brief 获取请求对应的特征层名称
     * @param req 搜索请求
     * @return 特征层名称
     */
    virtual QString featureNameForRequest(const SearchRequest &req) const;

    /**
     * @brief 验证搜索请求参数
     * @param req 搜索请求
     * @return 验证通过返回 true
     */
    virtual bool validateSearchRequest(SearchRequest &req);

    /**
     * @brief 构建搜索请求配置参数
     * @param req 搜索请求
     * 
     */
    virtual void buildSearchRequest(SearchRequest &req);

    /**
     * @brief 计算特征索引文件路径
     * @param request 搜索请求
     * @return 索引文件路径
     */
    virtual QString computeIndexPath(const SearchRequest &request) const;

    /**
     * @brief 从数据集中收集图像/标注作为搜索库
     * @param request 搜索请求
     * @param dataset_ids 数据集 ID 集合
     */
    virtual void collectGallery(SearchRequest &request, const std::set<int64_t> &dataset_ids);

    /**
     * @brief 收集查询项的路径信息
     * @param request 搜索请求
     * @param ids 图像/标注 ID 列表
     */
    virtual void collectQuery(SearchRequest &request, const std::vector<int64_t> &ids);

    /**
     * @brief 执行搜索
     * @param request 搜索请求
     * @param response 搜索响应
     */
    virtual void executeSearch(const SearchRequest &request, SearchResponse &response);

    /**
     * @brief 获取搜索功能的显示名称
     * @return 显示名称
     */
    virtual QString searchDisplayName() const;

    /**
     * @brief 获取查询为空时的错误提示
     * @return 错误提示文本
     */
    virtual QString emptyQuerySelectionErrorMessage() const;

    /**
     * @brief 获取搜索库为空时的错误提示
     * @return 错误提示文本
     */
    virtual QString emptyGalleryErrorMessage() const;

    /**
     * @brief 获取查询准备为空时的错误提示
     * @return 错误提示文本
     */
    virtual QString emptyPreparedQueryErrorMessage() const;

    /**
     * @brief 获取查询项数量
     * @param request 搜索请求
     * @return 查询项数量
     */
    virtual size_t queryItemCount(const SearchRequest &request) const;

    /**
     * @brief 获取搜索库项数量
     * @param request 搜索请求
     * @return 搜索库项数量
     */
    virtual size_t galleryItemCount(const SearchRequest &request) const;

    /**
     * @brief 获取模型可用的特征层选项
     * @param model_name 模型名称
     * @return 特征层名称列表
     */
    virtual QStringList featureOptionsForModel(const QString &model_name) const;

    /**
     * @brief 重置状态以开始新搜索
     */
    void resetForNewSearch();

    /**
     * @brief 启动进度显示
     * @param request 搜索请求
     */
    void startProgress(const SearchRequest &request);

    /**
     * @brief 结束进度显示
     * @param success 是否成功
     * @param message 日志消息
     */
    void finishProgress(bool success, const QString &message);

    /**
     * @brief 完成搜索并处理结果
     * @param response 搜索响应
     */
    void finishSearch(const SearchResponse &response);

    /**
     * @brief 创建构建进度报告回调
     * @param controller 控制器指针
     * @param gallery_count 搜索库项数量
     * @return 进度回调函数
     */
    static BuildProgressCallback createBuildProgressReporter(QPointer<ImageSearchController> controller,
                                                             size_t                          gallery_count);

    /**
     * @brief 将搜索结果应用到数据提供者
     * @param response 搜索响应
     */
    virtual void applyResults(const SearchResponse &response);

    void setRunning(bool running);

    void setLastError(const QString &last_error);

    // ponytail: base class resets both image+label results; subclasses override if needed
    /// 清除数据提供者的搜索结果
    virtual void clearProviderResults();

    /**
     * @brief 确认搜索设置已启用
     * @param display_name 搜索功能显示名称
     * @return 已启用返回 true
     */
    bool ensureSearchSettingsEnabled(const QString &display_name);

    dltool::settings::generated::AccessorKey settings_accessor_{
        dltool::settings::generated::AccessorKey::ImageSearch}; ///< 设置访问键

    bool    enabled_{true};   ///< 功能是否启用
    bool    running_{false};  ///< 是否正在运行
    QString last_error_;      ///< 最后一次错误信息
    QString last_summary_;    ///< 最后一次搜索摘要
    int     result_count_{0}; ///< 搜索结果数量
};

} // namespace dltool::feature
