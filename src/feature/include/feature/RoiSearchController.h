#pragma once

#include "dltool/feature/Export.h"
#include "feature/SearchControllerBase.h"

#include <QtQml>

namespace dltool::feature {

class RoiSearchDataProvider;
class FeatureDataProvider;

/**
 * @brief ROI 标注搜索控制器
 */
class FEATURE_API RoiSearchController : public SearchControllerBase
{
    Q_OBJECT
    QML_NAMED_ELEMENT(RoiSearchController)
    QML_UNCREATABLE("Can not create RoiSearchController directly!")

public:
     /**
     * @brief 构造函数
     * @param data_provider ROI 搜索数据提供者
     * @param parent 父对象
     */
    explicit RoiSearchController(RoiSearchDataProvider *data_provider, QObject *parent = nullptr);

protected:
    FeatureDataProvider *dataProvider() const override;

    /**
     * @brief 获取请求对应的模型名称
     * @param request 搜索请求
     * @return 模型名称
     */
    QString modelNameForRequest(const SearchRequest &request) const override;

    /**
     * @brief 获取请求对应的特征层名称
     * @param request 搜索请求
     * @return 特征层名称
     */
    QString featureNameForRequest(const SearchRequest &request) const override;

    /**
     * @brief 验证搜索请求参数
     * @param request 搜索请求
     * @return 验证通过返回 true
     */
    bool validateSearchRequest(SearchRequest &request) override;

    /// 构建搜索请求配置参数
    void buildSearchRequest(SearchRequest &req) override;

    /**
     * @brief 计算特征索引文件路径
     * @param request 搜索请求
     * @return 索引文件路径
     */
    QString computeIndexPath(const SearchRequest &request) const override;

    /**
     * @brief 从数据集中收集标注 ROI 作为搜索库
     * @param request 搜索请求
     * @param search_scope 数据集和类别搜索范围
     */
    void collectGallery(SearchRequest &request, const SearchScope &search_scope) override;

    /**
     * @brief 收集查询标注的 ROI
     * @param request 搜索请求
     * @param ids 标注 ID 列表
     */
    void collectQuery(SearchRequest &request, const std::vector<int64_t> &ids) override;

    /// 执行标注搜索
    void executeSearch(const SearchRequest &request, SearchResponse &response) override;

    /**
     * @brief 获取搜索功能的显示名称
     * @return 显示名称
     */
    QString searchDisplayName() const override;

    /**
     * @brief 获取查询为空时的错误提示
     * @return 错误提示文本
     */
    QString emptyQuerySelectionErrorMessage() const override;

    /**
     * @brief 获取搜索库为空时的错误提示
     * @return 错误提示文本
     */
    QString emptyGalleryErrorMessage() const override;

    /**
     * @brief 获取查询准备为空时的错误提示
     * @return 错误提示文本
     */
    QString emptyPreparedQueryErrorMessage() const override;

    /**
     * @brief 获取查询项数量
     * @param request 搜索请求
     * @return 查询项数量
     */
    size_t queryItemCount(const SearchRequest &request) const override;

    /**
     * @brief 获取搜索库项数量
     * @param request 搜索请求
     * @return 搜索库项数量
     */
    size_t galleryItemCount(const SearchRequest &request) const override;

    /**
     * @brief 获取模型可用的特征层选项
     * @param model_name 模型名称
     * @return 特征层名称列表
     */
    QStringList featureOptionsForModel(const QString &model_name) const override;

    /// 清除数据提供者的搜索结果
    void clearProviderResults() override;

    /**
     * @brief 将搜索结果应用到数据提供者
     * @param response 搜索响应
     */
    void applyResults(const SearchResponse &response) override;

private:
    RoiSearchDataProvider *data_provider_{nullptr};
};

} // namespace dltool::feature
