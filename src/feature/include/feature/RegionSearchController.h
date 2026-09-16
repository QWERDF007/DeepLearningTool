#pragma once

#include "dltool/feature/Export.h"

#include <QObject>
#include <QRectF>
#include <QString>
#include <QVariantMap>
#include <QtQml>

#include <cstdint>
#include <memory>

namespace dltool::data {
class DataManager;
}

namespace irt::features {
struct DinoSearchResponse;
}

namespace dltool::feature {

class RegionTask;

/**
 * @brief 基于 DINO 骨干的区域检索与标注生成控制器。
 *
 * 在所选数据集中搜索与指定标注在外观和结构上相似的区域，并批量生成新标注。
 */
class FEATURE_API RegionSearchController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(RegionSearchController)
    QML_UNCREATABLE("Can not create RegionSearchController directly!")

    Q_PROPERTY(bool enabled READ isEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(bool running READ isRunning NOTIFY busyChanged)
    Q_PROPERTY(bool computing READ isComputing NOTIFY computingChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(double progressValue READ progressValue NOTIFY progressValueChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressTextChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorTextChanged)

    // 查询快照信息
    Q_PROPERTY(int64_t queryLabelId READ queryLabelId NOTIFY queryChanged)
    Q_PROPERTY(int64_t queryImageId READ queryImageId NOTIFY queryChanged)
    Q_PROPERTY(int64_t queryDatasetId READ queryDatasetId NOTIFY queryChanged)
    Q_PROPERTY(int64_t queryClassId READ queryClassId NOTIFY queryChanged)
    Q_PROPERTY(QString queryClassName READ queryClassName NOTIFY queryChanged)
    Q_PROPERTY(QString queryImagePath READ queryImagePath NOTIFY queryChanged)
    Q_PROPERTY(QRectF queryRect READ queryRect NOTIFY queryChanged)
    Q_PROPERTY(bool queryValid READ queryValid NOTIFY queryChanged)
    Q_PROPERTY(bool canRepresentResultRect READ canRepresentResultRect CONSTANT)

    // 配置与索引范围
    Q_PROPERTY(int profileFinalK READ profileFinalK NOTIFY profileChanged)
    Q_PROPERTY(bool needsBuild READ needsBuild NOTIFY scopeChanged)
    Q_PROPERTY(int indexedImageCount READ indexedImageCount NOTIFY scopeChanged)

    // 结果摘要
    Q_PROPERTY(int returnedCount READ returnedCount NOTIFY summaryChanged)
    Q_PROPERTY(int createdCount READ createdCount NOTIFY summaryChanged)
    Q_PROPERTY(int reusedCount READ reusedCount NOTIFY summaryChanged)
    Q_PROPERTY(int skippedCount READ skippedCount NOTIFY summaryChanged)
    Q_PROPERTY(bool hasPartialResults READ hasPartialResults NOTIFY partialChanged)
    Q_PROPERTY(int partialCount READ partialCount NOTIFY partialChanged)

public:
    enum class State
    {
        Idle,
        Preparing,
        Building,
        Searching,
        Cancelling,
        AwaitingPartial,
        Committing,
        Done,
        Failed,
    };
    Q_ENUM(State)

    explicit RegionSearchController(dltool::data::DataManager *data_manager, QObject *parent = nullptr);
    ~RegionSearchController() override;

    bool isEnabled() const;
    bool isBusy() const;
    bool isRunning() const;
    bool isComputing() const;
    State state() const;
    QString statusText() const;
    double progressValue() const;
    QString progressText() const;
    Q_INVOKABLE QString errorText() const;
    Q_INVOKABLE QString lastError() const;
    Q_INVOKABLE QString validationError() const;

    int64_t queryLabelId() const;
    int64_t queryImageId() const;
    int64_t queryDatasetId() const;
    int64_t queryClassId() const;
    QString queryClassName() const;
    QString queryImagePath() const;
    QRectF queryRect() const;
    bool queryValid() const;
    bool canRepresentResultRect() const;

    int profileFinalK() const;
    bool needsBuild() const;
    int indexedImageCount() const;

    int returnedCount() const;
    int createdCount() const;
    int reusedCount() const;
    int skippedCount() const;
    bool hasPartialResults() const;
    int partialCount() const;

    Q_INVOKABLE bool captureQuery(qint64 label_id);
    Q_INVOKABLE bool checkNeedsBuild(const QList<qint64> &dataset_ids);
    Q_INVOKABLE bool start(const QVariantMap &options);
    Q_INVOKABLE bool search(const QVariantList &ids, const QVariantList &search_scope);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void generateReturnedPartial();
    Q_INVOKABLE void showLatestResults();
    Q_INVOKABLE void rebuildIndex(const QList<qint64> &dataset_ids);

    void commitResults(const std::shared_ptr<irt::features::DinoSearchResponse> &response);
    void prepareTestJob(int64_t target_class_id);
    void shutdown();
    void requestShutdown();

signals:
    void enabledChanged();
    void busyChanged();
    void computingChanged();
    void statusTextChanged();
    void progressValueChanged();
    void progressTextChanged();
    void errorTextChanged();
    void queryChanged();
    void profileChanged();
    void scopeChanged();
    void summaryChanged();
    void partialChanged();
    void requestNavigateToReview();

private slots:
    void onTaskProgress(double value, const QString &text);
    void onTaskFinished();

private:
    friend class RegionSearchTest;
    void setLastError(const QString &last_error);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace dltool::feature
