#pragma once

#include "dltool/feature/Export.h"
#include "feature/ImageClusterDataProvider.h"

#include <inferrt/features/ImageCluster.hpp>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QtQml>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace dltool::data {
class DataManager;
} // namespace dltool::data

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
    explicit ImageClusterController(ImageClusterDataProvider *data_provider,
                                    dltool::data::DataManager *data_manager,
                                    QObject *parent = nullptr);
    ~ImageClusterController() override = default;

    bool enabled() const;
    bool isRunning() const;
    bool hasResults() const;
    int  resultCount() const;

    QString lastError() const;
    QString lastSummary() const;

    /**
     * @brief 验证当前聚类配置是否可用于启动，不修改运行状态或错误状态。
     */
    Q_INVOKABLE QString validationError() const;

    Q_INVOKABLE bool cluster(const QVariantList &dataset_ids);

signals:
    void enabledChanged();
    void runningChanged();
    void resultsChanged();
    void lastErrorChanged();
    void buildProgressChanged(int processedCount, int totalCount);

private:
    enum class ImageClusterApplyMode
    {
        Move = 0,
        Copy = 1,
    };

    struct ImageClusterAssignment
    {
        int64_t image_id{0};
        int64_t cluster_id{-1};
        double  probability{0.0};
    };

    struct ImageClusterApplyResult
    {
        size_t moved_image_count{0};
        size_t copied_image_count{0};
        size_t target_dataset_count{0};
        size_t skipped_noise_count{0};
    };

    struct ClusterApplyPlan
    {
        std::map<int64_t, std::vector<int64_t>> image_ids_by_target_dataset;
        size_t                                  skipped_noise_count{0};
    };

    struct ClusterRequest
    {
        QString weights_file;
        bool    include_noise{false};
        ImageClusterApplyMode apply_mode{ImageClusterApplyMode::Move};

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
        ImageClusterApplyMode apply_mode{ImageClusterApplyMode::Move};
        int     feature_dim{0};
        int64_t cluster_count{0};
        int64_t noise_count{0};

        std::vector<ImageClusterAssignment> assignments;
    };

    void buildClusterRequest(ClusterRequest &request) const;
    bool validateClusterRequest(const ClusterRequest &request);
    QString clusterRequestValidationError(const ClusterRequest &request) const;
    void collectClusterItems(ClusterRequest &request, const std::map<int64_t, std::set<int64_t>> &scope);
    void executeCluster(const ClusterRequest &request, ClusterResponse &response);
    bool buildClusterApplyPlan(const std::vector<ImageClusterAssignment> &assignments,
                               bool include_noise,
                               ClusterApplyPlan &plan,
                               QString &err_msg);
    bool ensureClusterTargetDataset(const QString &target_dataset_name, int64_t &dataset_id, QString &err_msg);
    void applyClusterPlan(const ClusterResponse &response, ClusterApplyPlan plan);
    void completeClusterApply(const ClusterResponse &response, const ClusterApplyPlan &plan,
                              size_t applied_image_count, const QString &error);

    void resetForNewCluster();
    void startProgress(const ClusterRequest &request);
    void finishProgress(bool success, const QString &message);
    void finishCluster(const ClusterResponse &response);

    static irt::features::ImageClusterProgressCallback createProgressReporter(
        QPointer<ImageClusterController> controller, size_t total_count);

    void setRunning(bool running);
    void setLastError(const QString &last_error);

    ImageClusterDataProvider *data_provider_{nullptr};
    QPointer<dltool::data::DataManager> data_manager_;

    bool    enabled_{true};
    bool    running_{false};
    QString last_error_;
    QString last_summary_;
    int     result_count_{0};
};

} // namespace dltool::feature
