#include "feature/ImageSearchController.h"

#include "feature/ImageSearchDataProvider.h"

namespace dltool::feature {

ImageSearchController::ImageSearchController(ImageSearchDataProvider *data_provider, QObject *parent)
    : SearchControllerBase(dltool::settings::generated::AccessorKey::ImageSearch, parent)
    , data_provider_(data_provider)
{
}

bool ImageSearchController::searchSelectedImages(const QVariantList &dataset_ids)
{
    if (!data_provider_)
    {
        setLastError(QString("图像模型未初始化"));
        return false;
    }

    const auto query_ids = data_provider_->selectedImageIds();
    if (query_ids.empty())
    {
        setLastError(QString("请先选择要检索的图片"));
        return false;
    }

    QVariantList ids;
    ids.reserve(static_cast<int>(query_ids.size()));
    for (const int64_t id : query_ids) ids.append(static_cast<qlonglong>(id));
    return search(ids, dataset_ids);
}

FeatureDataProvider *ImageSearchController::dataProvider() const
{
    return data_provider_;
}

void ImageSearchController::clearProviderResults()
{
    if (data_provider_)
        data_provider_->clearImageSearchResults();
}

void ImageSearchController::applyResults(const SearchResponse &response)
{
    if (data_provider_)
        data_provider_->setImageSearchResults(response.result_ids, !response.result_ids.empty());
}

} // namespace dltool::feature
