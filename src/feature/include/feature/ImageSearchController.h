#pragma once

#include "dltool/feature/Export.h"
#include "feature/SearchControllerBase.h"

#include <QtQml>

namespace dltool::feature {

class FeatureDataProvider;
class ImageSearchDataProvider;

class FEATURE_API ImageSearchController : public SearchControllerBase
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ImageSearchController)
    QML_UNCREATABLE("Can not create ImageSearchController directly!")

public:
    explicit ImageSearchController(ImageSearchDataProvider *data_provider, QObject *parent = nullptr);
    ~ImageSearchController() override = default;

    Q_INVOKABLE bool searchSelectedImages(const QVariantList &dataset_ids);

protected:
    FeatureDataProvider *dataProvider() const override;
    void clearProviderResults() override;
    void applyResults(const SearchResponse &response) override;

private:
    ImageSearchDataProvider *data_provider_{nullptr};
};

} // namespace dltool::feature
