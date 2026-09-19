#include "feature/RoiSearchController.h"

#include "SearchControllerUtils.h"
#include "feature/RoiSearchDataProvider.h"
#include "settings/GlobalSettings.h"
#include "settings/SettingsValue.h"

#include <inferrt/features/RoiSearch.hpp>
#include <spdlog/spdlog.h>

#include <QFileInfo>
#include <algorithm>
#include <map>
#include <set>
#include <unordered_set>

using dltool::settings::settingBool;
using dltool::settings::settingInt;
using dltool::settings::settingString;

namespace dltool::feature {

RoiSearchController::RoiSearchController(RoiSearchDataProvider *data_provider, QObject *parent)
    : SearchControllerBase(dltool::settings::generated::AccessorKey::RoiSearch, parent)
    , data_provider_(data_provider)
{
}

FeatureDataProvider *RoiSearchController::dataProvider() const
{
    return data_provider_;
}

QString RoiSearchController::searchDisplayName() const
{
    return QString("标注搜索");
}

QString RoiSearchController::modelNameForRequest(const SearchRequest &request) const
{
    return QString::fromStdString(request.roi_config.model_name);
}

QString RoiSearchController::featureNameForRequest(const SearchRequest &request) const
{
    return QString::fromStdString(request.roi_config.feature_name);
}

QString RoiSearchController::validationErrorForRequest(const SearchRequest &request) const
{
    const QString base_error = SearchControllerBase::validationErrorForRequest(request);
    if (!base_error.isEmpty())
    {
        return base_error;
    }

    const QString     model_name       = modelNameForRequest(request);
    const QStringList spatial_features = featureOptionsForModel(model_name);
    if (spatial_features.isEmpty())
    {
        return QString("标注搜索未在配置中找到模型 %1 的空间特征层").arg(model_name);
    }
    return {};
}

bool RoiSearchController::validateSearchRequest(SearchRequest &request)
{
    if (!SearchControllerBase::validateSearchRequest(request))
        return false;

    namespace generated_field    = dltool::settings::generated::field;
    const QString     model_name = modelNameForRequest(request);
    const QStringList spatial_features = featureOptionsForModel(model_name);
    if (spatial_features.isEmpty())
    {
        setLastError(QString("标注搜索未在配置中找到模型 %1 的空间特征层").arg(model_name));
        return false;
    }

    const QString feature_name = featureNameForRequest(request);
    if (!spatial_features.contains(feature_name))
    {
        const QString effective_feature_name = spatial_features.last();
        request.image_config.feature_name    = effective_feature_name.toStdString();
        request.roi_config.feature_name      = effective_feature_name.toStdString();
        dltool::settings::GlobalSettings::getInstance()->setFieldValue(generated_field::RoiSearch::FeatureName,
                                                                       effective_feature_name);
    }
    return true;
}

QString RoiSearchController::emptyQuerySelectionErrorMessage() const
{
    return QString("请先选择要搜索的标注");
}

QString RoiSearchController::emptyGalleryErrorMessage() const
{
    return QString("选定数据集中没有可搜索的标注 ROI");
}

QString RoiSearchController::emptyPreparedQueryErrorMessage() const
{
    return QString("选中的标注没有有效 ROI 或图像文件不存在");
}

size_t RoiSearchController::queryItemCount(const SearchRequest &request) const
{
    return request.query_rois.size();
}

size_t RoiSearchController::galleryItemCount(const SearchRequest &request) const
{
    return request.gallery_rois.size();
}

void RoiSearchController::buildSearchRequest(SearchRequest &req) const
{
    const auto *settings      = dltool::settings::GlobalSettings::getInstance();
    namespace generated_field = dltool::settings::generated::field;

    const auto base_settings = readImageSearchBaseSettings(settings, settingsAccessor());
    req.weights_file         = base_settings.weights_file.isEmpty()
                                   ? QString()
                                   : QFileInfo(base_settings.weights_file).absoluteFilePath();
    req.rebuild_index        = base_settings.rebuild_index;
    req.top_k                = base_settings.top_k;
    applyImageSearchBaseConfig(req.image_config, base_settings);
    applyImageSearchBaseConfig(req.roi_config, base_settings);

    req.roi_config.exact_search = settingBool(settings, generated_field::RoiSearch::ExactSearch, true);

    const QString mode_str = settingString(settings, generated_field::RoiSearch::Mode, QStringLiteral("crop_masked_mean"));
    req.roi_config.mode    = parseRoiFeatureMode(mode_str);

    req.roi_config.crop_margin = static_cast<float>(
        settings != nullptr ? settings->valueForField(generated_field::RoiSearch::CropMargin, 0.05).toDouble() : 0.05);
    req.roi_config.patch_size = std::max(1, settingInt(settings, generated_field::RoiSearch::PatchSize, 16));
}

QString RoiSearchController::computeIndexPath(const SearchRequest &request) const
{
    const QString index_dir
        = indexDirectoryForProject(data_provider_->projectDir(), QStringLiteral("roi_search"));
    return indexPathForRequest(index_dir, modelNameForRequest(request), featureNameForRequest(request),
                               QStringLiteral(".roi.faiss"));
}

void RoiSearchController::collectGallery(SearchRequest &request, const SearchScope &search_scope)
{
    const auto all_label_ids = data_provider_->allLabelIds();
    for (const int64_t label_id : all_label_ids)
    {
        const int64_t image_id = data_provider_->labelImageId(label_id);
        if (image_id < 0)
            continue;
        const int64_t image_dataset_id = data_provider_->imageDatasetId(image_id);
        const auto    scope_it        = search_scope.find(image_dataset_id);
        if (!search_scope.empty() && scope_it == search_scope.end())
            continue;
        if (scope_it != search_scope.end() && !scope_it->second.empty()
            && scope_it->second.find(data_provider_->labelClassId(label_id)) == scope_it->second.end())
            continue;

        const QString path = data_provider_->imagePath(image_id);
        if (!QFileInfo::exists(path))
            continue;

        irt::features::RoiSearchItem item;
        if (!roiItemFromLabelData(label_id, toFsPath(QFileInfo(path).absoluteFilePath()),
                                  data_provider_->labelData(label_id), item))
            continue;

        request.gallery_rois.push_back(std::move(item));
    }
}

void RoiSearchController::collectQuery(SearchRequest &request, const std::vector<int64_t> &ids)
{
    for (const int64_t label_id : ids)
    {
        const int64_t image_id = data_provider_->labelImageId(label_id);
        if (image_id < 0)
            continue;

        const QString path = data_provider_->imagePath(image_id);
        if (!QFileInfo::exists(path))
            continue;

        irt::features::RoiSearchItem item;
        if (!roiItemFromLabelData(label_id, toFsPath(QFileInfo(path).absoluteFilePath()),
                                  data_provider_->labelData(label_id), item))
            continue;

        request.query_rois.push_back(std::move(item));
    }
}

SearchControllerBase::SearchExecutor RoiSearchController::searchExecutor() const
{
    return &RoiSearchController::executeRoiSearch;
}

void RoiSearchController::executeRoiSearch(const SearchRequest &request, SearchResponse &response,
                                           const BuildProgressCallback &progress)
{
    const size_t gallery_count = request.gallery_rois.size();

    try
    {
        irt::features::RoiSearch search(request.roi_config);
        const auto               weights_path = toFsPath(request.weights_file);
        const auto               index_path   = toFsPath(request.index_file);

        if (request.cancellationRequested())
            return;

        addProgressMessage(spdlog::level::info, QString("正在准备标注搜索特征库: %1 个标注").arg(gallery_count));
        search.buildOrLoad(weights_path, request.gallery_rois, index_path, request.rebuild_index,
                           progress);

        if (request.cancellationRequested())
        {
            response.error = QStringLiteral("标注搜索已取消");
            return;
        }

        const auto stats = search.featureWorkStats();
        if (stats.available)
        {
            addProgressMessage(spdlog::level::info,
                               QString("特征工作统计: 解码图像 %1 张, 提取视图 %2 个, 前向批次 %3 次")
                                   .arg(stats.decoded_images)
                                   .arg(stats.encoded_views)
                                   .arg(stats.forward_batches));
        }

        const auto gallery_ids_vec = search.galleryIds();
        const std::unordered_set<int64_t> gallery_id_set(gallery_ids_vec.begin(), gallery_ids_vec.end());

        std::map<int64_t, float> result_scores;
        for (const auto &query_item : request.query_rois)
        {
            std::vector<irt::features::RoiSearchResult> search_results;
            if (gallery_id_set.find(query_item.roi_id) != gallery_id_set.end())
            {
                search_results = search.searchByRoiId(query_item.roi_id, request.top_k);
            }
            else
            {
                search_results = search.search(query_item, request.top_k);
            }

            for (const auto &result : search_results)
            {
                const int64_t label_id = result.roi_id;
                auto          it       = result_scores.find(label_id);
                if (it == result_scores.end() || result.score > it->second)
                    result_scores[label_id] = result.score;
            }
        }

        response.result_ids = sortedSearchResultIds(result_scores);
        response.success    = true;
        response.summary    = QString("标注搜索完成: 命中 %1 个标注").arg(response.result_ids.size());
    }
    catch (const std::exception &e)
    {
        response.success = false;
        response.error   = QString::fromUtf8(e.what());
    }
    catch (...)
    {
        response.success = false;
        response.error   = QString("未知标注搜索错误");
    }
}

QStringList RoiSearchController::featureOptionsForModel(const QString &model_name) const
{
    auto *settings = dltool::settings::GlobalSettings::getInstance();
    if (!settings || !settings->catalog())
        return {};

    const QVariantList options = settings->catalog()->optionsForAccessor(
        dltool::settings::toQString(dltool::settings::generated::accessorPath(settingsAccessor())),
        dltool::settings::toQString(
            dltool::settings::generated::fieldName(dltool::settings::generated::field::RoiSearch::FeatureName)),
        model_name);

    QStringList result;
    for (const QVariant &value : options)
    {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty() && !result.contains(text))
            result.append(text);
    }
    return result;
}

void RoiSearchController::applyResults(const SearchResponse &response)
{
    if (data_provider_)
        data_provider_->setLabelSearchResults(response.result_ids, !response.result_ids.empty());
}

void RoiSearchController::clearProviderResults()
{
    if (data_provider_)
        data_provider_->clearLabelSearchResults();
}

} // namespace dltool::feature
