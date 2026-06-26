#pragma once

#include "dltool/feature/Export.h"
#include "settings/SettingsKeys.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml>
#include <set>
#include <vector>

namespace dltool::feature {

class ImageSearchDataProvider;

/**
 * @brief 图像相似度搜索控制器
 *
 * 基于 InferRT 特征提取与 FAISS 索引，对当前选中的查询图像在指定数据集图库中执行相似检索。
 * 搜索在后台线程执行，完成后将结果写入 GlobalFilter 的图像搜索过滤模块。
 */
class FEATURE_API ImageSearchController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ImageSearchController)
    QML_UNCREATABLE("Can not create ImageSearchController directly!")

    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged FINAL)
    Q_PROPERTY(bool hasResults READ hasResults NOTIFY resultsChanged FINAL)
    Q_PROPERTY(int resultCount READ resultCount NOTIFY resultsChanged FINAL)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged FINAL)
    Q_PROPERTY(QString lastSummary READ lastSummary NOTIFY resultsChanged FINAL)

public:
    explicit ImageSearchController(ImageSearchDataProvider *data_provider, QObject *parent = nullptr);
    ~ImageSearchController() override = default;

    /**
     * @brief 搜索任务是否正在运行
     * @return 后台线程执行搜索时返回 true
     */
    bool isRunning() const;

    /**
     * @brief 是否存在最近一次搜索的命中结果
     * @return 结果数量大于 0 时返回 true
     */
    bool hasResults() const;

    /**
     * @brief 获取最近一次搜索命中的图像数量
     * @return 命中图像数量
     */
    int resultCount() const;

    /**
     * @brief 获取最近一次错误或校验失败信息
     * @return 错误描述；无错误时为空字符串
     */
    QString lastError() const;

    /**
     * @brief 获取最近一次成功搜索的摘要信息
     * @return 例如命中数量的说明文本；未成功或无结果时为空
     */
    QString lastSummary() const;

    /**
     * @brief 对当前选中的图像执行相似度搜索
     *
     * 以图像列表中已勾选的图像为查询，在指定数据集（或全部）构成的图库中检索相似图像。
     * 成功后在后台完成时自动更新 GlobalFilter 的搜索结果；若已有任务在运行则拒绝新请求。
     *
     * @param dataset_ids 限定图库的数据集 ID 列表；为空表示使用全部图像
     * @details 图像搜索模型、索引、后端和运行时参数从 GlobalSettings 的图像搜索分组读取。
     *
     * @return 任务成功提交到后台线程时返回 true
     */
    Q_INVOKABLE bool searchSelectedImages(const QVariantList &dataset_ids);

    Q_INVOKABLE bool searchImages(const QVariantList &image_ids, const QVariantList &dataset_ids);

    Q_INVOKABLE bool searchLabelRois(const QVariantList &label_ids, const QVariantList &dataset_ids);

signals:
    /** @brief 运行状态变化（running 属性） */
    void runningChanged();

    /** @brief 搜索结果或命中数量变化 */
    void resultsChanged();

    /** @brief 最近一次错误信息变化 */
    void lastErrorChanged();

    /** @brief 特征库构建进度更新 */
    void buildProgressChanged(int processedCount, int totalCount);

private:
    struct SearchRequest;
    struct SearchResponse;

    // ── 参数解析与校验 ──

    /// 从 QVariantList 提取生效的 dataset ID 集合
    static std::set<int64_t> parseDatasetIds(const QVariantList &dataset_ids);

    /// 校验权重文件是否存在；失败时自动调用 setLastError
    bool validateWeightsFile(const QString &path);

    /// 从设置分组读取参数快照并组装为 SearchRequest
    SearchRequest buildSearchRequest(dltool::settings::generated::AccessorKey accessor_key);

    /// 确认设置分组已加载且对应搜索功能已启用
    bool ensureSearchSettingsEnabled(dltool::settings::generated::AccessorKey accessor_key,
                                     const QString                           &display_name);

    /// 使用指定查询图像启动图像搜索
    bool startImageSearch(const std::vector<int64_t> &query_ids, const QVariantList &dataset_ids,
                          const QString &empty_query_message, const QString &missing_query_file_message);

    // ── 图像收集 ──

    /// 按数据集过滤并收集图库图像路径及 ID 映射
    void collectGalleryImages(SearchRequest &request, const std::set<int64_t> &dataset_ids);

    /// 收集当前选中图像的查询路径
    void collectQueryImages(SearchRequest &request, const std::vector<int64_t> &query_ids) const;
    void collectGalleryRois(SearchRequest &request, const std::set<int64_t> &dataset_ids);
    void collectQueryRois(SearchRequest &request, const std::vector<int64_t> &query_label_ids) const;

    // ── 索引路径 ──

    /// 计算当前搜索请求对应的 FAISS 索引文件路径
    QString computeIndexPath(const SearchRequest &request) const;

    // ── 后台搜索 ──

    static void executeSearchWorker(SearchRequest request, QPointer<ImageSearchController> controller);

    // ── UI 反馈 ──

    void resetForNewSearch();
    void startProgress(const SearchRequest &request);
    void finishProgress(bool success, const QString &message);
    void setRunning(bool running);
    void setLastError(const QString &last_error);
    void finishSearch(const SearchResponse &response);

    // ── 数据成员 ──

    ImageSearchDataProvider *data_provider_{nullptr};
    bool                     running_{false};
    QString                  last_error_;
    QString                  last_summary_;
    int                      result_count_{0};
};

} // namespace dltool::feature
