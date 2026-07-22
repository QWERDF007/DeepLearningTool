#pragma once

#include "dltool/feature/Export.h"
#include "feature/RoiClusterDataProvider.h"

#include <inferrt/features/RoiCluster.hpp>

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

class FEATURE_API RoiClusterController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(RoiClusterController)
    QML_UNCREATABLE("Can not create RoiClusterController directly!")

    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged FINAL)
    Q_PROPERTY(bool hasResults READ hasResults NOTIFY resultsChanged FINAL)
    Q_PROPERTY(int resultCount READ resultCount NOTIFY resultsChanged FINAL)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged FINAL)
    Q_PROPERTY(QString lastSummary READ lastSummary NOTIFY resultsChanged FINAL)

public:
    explicit RoiClusterController(RoiClusterDataProvider *data_provider,
                                  dltool::data::DataManager *data_manager,
                                  QObject *parent = nullptr);
    ~RoiClusterController() override = default;

    bool enabled() const;
    bool isRunning() const;
    bool hasResults() const;
    int  resultCount() const;

    QString lastError() const;
    QString lastSummary() const;

    Q_INVOKABLE QString validationError() const;
    Q_INVOKABLE bool    cluster(const QVariantList &dataset_class_scope);

signals:
    void enabledChanged();
    void runningChanged();
    void resultsChanged();
    void lastErrorChanged();
    void buildProgressChanged(int processedCount, int totalCount);

private:
    struct Request
    {
        QString weights_file;
        bool    include_noise{false};

        irt::features::RoiClusterConfig config;
        std::vector<irt::features::RoiClusterItem> items;

        std::chrono::steady_clock::time_point started_at;
        QPointer<RoiClusterController>        controller;
    };

    struct Response
    {
        bool    success{false};
        QString error;
        QString summary;
        qint64  elapsed_ms{0};

        bool include_noise{false};
        int  feature_dim{0};
        int64_t cluster_count{0};
        int64_t noise_count{0};

        std::vector<irt::features::RoiClusterAssignment> assignments;
    };

    void buildRequest(Request &request) const;
    bool validateRequest(const Request &request);
    QString requestValidationError(const Request &request) const;
    void collectClusterItems(Request &request, const std::map<int64_t, std::set<int64_t>> &scope);
    void executeCluster(const Request &request, Response &response);
    bool applyClusterResult(const Response &response, size_t &assigned_count, size_t &tag_count,
                            size_t &skipped_noise_count, QString &err_msg);

    void resetForNewCluster();
    void startProgress(const Request &request);
    void finishProgress(bool success, const QString &message);
    void finishCluster(const Response &response);

    static irt::features::RoiClusterProgressCallback createProgressReporter(
        QPointer<RoiClusterController> controller, size_t total_count);

    void setRunning(bool running);
    void setLastError(const QString &last_error);

    RoiClusterDataProvider *data_provider_{nullptr};
    QPointer<dltool::data::DataManager> data_manager_;

    bool    enabled_{true};
    bool    running_{false};
    QString last_error_;
    QString last_summary_;
    int     result_count_{0};
};

} // namespace dltool::feature
