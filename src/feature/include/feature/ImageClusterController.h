#pragma once

#include "dltool/feature/Export.h"
#include "feature/ImageSearchDataProvider.h"

#include <inferrt/features/ImageCluster.hpp>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QtQml>
#include <chrono>
#include <cstdint>
#include <set>
#include <vector>

namespace dltool::feature {

class FEATURE_API ImageClusterController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ImageClusterController)
    QML_UNCREATABLE("Can not create ImageClusterController directly!")

    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged FINAL)
    Q_PROPERTY(bool hasResults READ hasResults NOTIFY resultsChanged FINAL)
    Q_PROPERTY(int resultCount READ resultCount NOTIFY resultsChanged FINAL)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged FINAL)
    Q_PROPERTY(QString lastSummary READ lastSummary NOTIFY resultsChanged FINAL)

public:
    explicit ImageClusterController(ImageSearchDataProvider *data_provider, QObject *parent = nullptr);
    ~ImageClusterController() override = default;

    bool enabled() const;
    bool isRunning() const;
    bool hasResults() const;
    int  resultCount() const;

    QString lastError() const;
    QString lastSummary() const;

    Q_INVOKABLE bool clusterSelectedImages();
    Q_INVOKABLE bool cluster(const QVariantList &image_ids, const QVariantList &dataset_ids);

signals:
    void enabledChanged();
    void runningChanged();
    void resultsChanged();
    void lastErrorChanged();
    void buildProgressChanged(int processedCount, int totalCount);

private:
    struct ClusterRequest
    {
        QString weights_file;
        bool    include_noise{false};
        ImageSearchDataProvider::ImageClusterApplyMode apply_mode{
            ImageSearchDataProvider::ImageClusterApplyMode::Move};

        irt::features::ImageClusterConfig config;
        std::vector<irt::features::ImageClusterItem> items;

        std::chrono::steady_clock::time_point started_at;
        QPointer<ImageClusterController>      controller;
    };

    struct ClusterResponse
    {
        bool    success{false};
        QString error;
        QString summary;
        qint64  elapsed_ms{0};

        bool    include_noise{false};
        ImageSearchDataProvider::ImageClusterApplyMode apply_mode{
            ImageSearchDataProvider::ImageClusterApplyMode::Move};
        int     feature_dim{0};
        int64_t cluster_count{0};
        int64_t noise_count{0};

        std::vector<ImageSearchDataProvider::ImageClusterAssignment> assignments;
    };

    static std::set<int64_t> parseDatasetIds(const QVariantList &dataset_ids);

    void buildClusterRequest(ClusterRequest &request);
    bool validateClusterRequest(const ClusterRequest &request);
    void collectClusterItems(ClusterRequest &request, const std::vector<int64_t> &image_ids,
                             const std::set<int64_t> &dataset_ids);
    void executeCluster(const ClusterRequest &request, ClusterResponse &response);

    void resetForNewCluster();
    void startProgress(const ClusterRequest &request);
    void finishProgress(bool success, const QString &message);
    void finishCluster(const ClusterResponse &response);

    static irt::features::ImageClusterProgressCallback createProgressReporter(
        QPointer<ImageClusterController> controller, size_t total_count);

    void setRunning(bool running);
    void setLastError(const QString &last_error);

    ImageSearchDataProvider *data_provider_{nullptr};

    bool    enabled_{true};
    bool    running_{false};
    QString last_error_;
    QString last_summary_;
    int     result_count_{0};
};

} // namespace dltool::feature
