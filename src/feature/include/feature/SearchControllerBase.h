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
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace dltool::feature {

class FeatureDataProvider;

class FEATURE_API SearchControllerBase : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged FINAL)
    Q_PROPERTY(bool hasResults READ hasResults NOTIFY resultsChanged FINAL)
    Q_PROPERTY(int resultCount READ resultCount NOTIFY resultsChanged FINAL)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged FINAL)
    Q_PROPERTY(QString lastSummary READ lastSummary NOTIFY resultsChanged FINAL)

public:
    explicit SearchControllerBase(dltool::settings::generated::AccessorKey settings_accessor,
                                  QObject                                *parent = nullptr);
    ~SearchControllerBase() override = default;

    bool enabled() const;
    bool isRunning() const;
    bool hasResults() const;
    int resultCount() const;
    QString lastError() const;
    QString lastSummary() const;

    Q_INVOKABLE virtual bool search(const QVariantList &ids, const QVariantList &search_scope);

signals:
    void enabledChanged();
    void runningChanged();
    void resultsChanged();
    void lastErrorChanged();
    void buildProgressChanged(int processedCount, int totalCount);

protected:
    using SearchScope = std::map<int64_t, std::set<int64_t>>;

    struct SearchRequest
    {
        QString weights_file;
        QString index_file;

        bool rebuild_index{false};
        int  top_k{5};

        irt::features::ImageSearchConfig image_config;
        irt::features::RoiSearchConfig   roi_config;

        std::chrono::steady_clock::time_point started_at;

        std::vector<std::filesystem::path>          query_images;
        std::vector<irt::features::ImageSearchItem> gallery_images;

        std::vector<irt::features::RoiSearchItem> query_rois;
        std::vector<irt::features::RoiSearchItem> gallery_rois;

        QPointer<SearchControllerBase> controller;
    };

    struct SearchResponse
    {
        bool                 success{false};
        QString              error;
        QString              summary;
        qint64               elapsed_ms{0};
        std::vector<int64_t> result_ids;
    };

    using BuildProgressCallback = std::function<void(const irt::features::ImageSearchBuildProgress &)>;

    static SearchScope parseSearchScope(const QVariantList &search_scope);

    virtual FeatureDataProvider *dataProvider() const = 0;

    bool validateWeightsFile(const QString &path);

    virtual dltool::settings::generated::AccessorKey settingsAccessor() const;
    virtual QString modelNameForRequest(const SearchRequest &req) const;
    virtual QString featureNameForRequest(const SearchRequest &req) const;
    virtual bool validateSearchRequest(SearchRequest &req);
    virtual void buildSearchRequest(SearchRequest &req);
    virtual QString computeIndexPath(const SearchRequest &request) const;
    virtual void collectGallery(SearchRequest &request, const SearchScope &search_scope);
    virtual void collectQuery(SearchRequest &request, const std::vector<int64_t> &ids);
    virtual void executeSearch(const SearchRequest &request, SearchResponse &response);

    virtual QString searchDisplayName() const;
    virtual QString emptyQuerySelectionErrorMessage() const;
    virtual QString emptyGalleryErrorMessage() const;
    virtual QString emptyPreparedQueryErrorMessage() const;
    virtual size_t queryItemCount(const SearchRequest &request) const;
    virtual size_t galleryItemCount(const SearchRequest &request) const;
    virtual QStringList featureOptionsForModel(const QString &model_name) const;

    void resetForNewSearch();
    void startProgress(const SearchRequest &request);
    void finishProgress(bool success, const QString &message);
    void finishSearch(const SearchResponse &response);

    static BuildProgressCallback createBuildProgressReporter(QPointer<SearchControllerBase> controller,
                                                             size_t                         gallery_count);

    virtual void applyResults(const SearchResponse &response) = 0;
    virtual void clearProviderResults() = 0;

    void setRunning(bool running);
    void setLastError(const QString &last_error);

    bool ensureSearchSettingsEnabled(const QString &display_name);

private:
    dltool::settings::generated::AccessorKey settings_accessor_{
        dltool::settings::generated::AccessorKey::ImageSearch};

    bool    enabled_{true};
    bool    running_{false};
    QString last_error_;
    QString last_summary_;
    int     result_count_{0};
};

} // namespace dltool::feature
