#pragma once

#include "dltool/settings/Export.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml>

namespace dltool::database {
class SettingsDataBase;
}

namespace dltool::settings {

class SETTINGS_API ImageSearchSettings : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ImageSearchSettings)
    QML_UNCREATABLE("ImageSearchSettings is managed by AdvancedSettings")

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QString modelPath READ modelPath WRITE setModelPath NOTIFY modelPathChanged)
    Q_PROPERTY(QString featureName READ featureName WRITE setFeatureName NOTIFY featureNameChanged)
    Q_PROPERTY(bool rebuildIndex READ rebuildIndex WRITE setRebuildIndex NOTIFY rebuildIndexChanged)
    Q_PROPERTY(int topK READ topK WRITE setTopK NOTIFY topKChanged)
    Q_PROPERTY(QString norm READ norm WRITE setNorm NOTIFY normChanged)
    Q_PROPERTY(QString preprocessBackend READ preprocessBackend WRITE setPreprocessBackend NOTIFY
                   preprocessBackendChanged)
    Q_PROPERTY(QString faissBackend READ faissBackend WRITE setFaissBackend NOTIFY faissBackendChanged)
    Q_PROPERTY(QString indexStorage READ indexStorage WRITE setIndexStorage NOTIFY indexStorageChanged)
    Q_PROPERTY(int diskBuildBatchSize READ diskBuildBatchSize WRITE setDiskBuildBatchSize NOTIFY
                   diskBuildBatchSizeChanged)
    Q_PROPERTY(int modelBatchSize READ modelBatchSize WRITE setModelBatchSize NOTIFY modelBatchSizeChanged)
    Q_PROPERTY(QString modelBackend READ modelBackend WRITE setModelBackend NOTIFY modelBackendChanged)
    Q_PROPERTY(QString modelDevice READ modelDevice WRITE setModelDevice NOTIFY modelDeviceChanged)
    Q_PROPERTY(QString indexDirectory READ indexDirectory WRITE setIndexDirectory NOTIFY indexDirectoryChanged)

public:
    explicit ImageSearchSettings(QObject *parent = nullptr);
    ~ImageSearchSettings() override;

    bool enabled() const
    {
        return options_.enabled;
    }
    void setEnabled(bool value);

    QString model() const
    {
        return model_.name;
    }
    void setModel(const QString &value);

    QString modelPath() const
    {
        return model_.path;
    }
    void setModelPath(const QString &value);

    QString featureName() const
    {
        return model_.feature_name;
    }
    void setFeatureName(const QString &value);

    bool rebuildIndex() const
    {
        return options_.rebuild_index;
    }
    void setRebuildIndex(bool value);

    int topK() const
    {
        return options_.top_k;
    }
    void setTopK(int value);

    QString norm() const
    {
        return index_.norm;
    }
    void setNorm(const QString &value);

    QString preprocessBackend() const
    {
        return runtime_.preprocess_backend;
    }
    void setPreprocessBackend(const QString &value);

    QString faissBackend() const
    {
        return runtime_.faiss_backend;
    }
    void setFaissBackend(const QString &value);

    QString indexStorage() const
    {
        return index_.storage;
    }
    void setIndexStorage(const QString &value);

    int diskBuildBatchSize() const
    {
        return index_.disk_build_batch_size;
    }
    void setDiskBuildBatchSize(int value);

    int modelBatchSize() const
    {
        return model_.batch_size;
    }
    void setModelBatchSize(int value);

    QString modelBackend() const
    {
        return runtime_.model_backend;
    }
    void setModelBackend(const QString &value);

    QString modelDevice() const
    {
        return runtime_.model_device;
    }
    void setModelDevice(const QString &value);

    QString indexDirectory() const
    {
        return index_.directory;
    }
    void setIndexDirectory(const QString &value);

    Q_INVOKABLE QStringList customFeatureNames(const QString &model_name) const;
    Q_INVOKABLE void        addCustomFeatureName(const QString &model_name, const QString &feature_name);

    void        load(const QVariantMap &row);
    QVariantMap saveMap() const;
    void        reset();

signals:
    void enabledChanged();
    void modelChanged();
    void modelPathChanged();
    void featureNameChanged();
    void rebuildIndexChanged();
    void topKChanged();
    void normChanged();
    void preprocessBackendChanged();
    void faissBackendChanged();
    void indexStorageChanged();
    void diskBuildBatchSizeChanged();
    void modelBatchSizeChanged();
    void modelBackendChanged();
    void modelDeviceChanged();
    void indexDirectoryChanged();
    void customFeatureNamesChanged();

private:
    QString customFeatureNamesJson() const;
    void    setCustomFeatureNamesJson(const QString &value);

    struct ModelSettings
    {
        QString name{QStringLiteral("resnet18")};
        QString path{QStringLiteral("F:/models/resnet18.wts")};
        QString feature_name{QStringLiteral("layer4")};
        int     batch_size{1};
    };

    struct IndexSettings
    {
        QString norm{QStringLiteral("l2")};
        QString storage{QStringLiteral("ram")};
        QString directory;
        int     disk_build_batch_size{256};
    };

    struct RuntimeSettings
    {
        QString preprocess_backend{QStringLiteral("cpu")};
        QString faiss_backend{QStringLiteral("cpu")};
        QString model_backend{QStringLiteral("tensorrt")};
        QString model_device{QStringLiteral("gpu")};
    };

    struct SearchOptions
    {
        bool enabled{true};
        bool rebuild_index{false};
        int  top_k{5};
    };

    ModelSettings            model_;
    IndexSettings            index_;
    RuntimeSettings          runtime_;
    SearchOptions            options_;
    QHash<QString, QStringList> custom_feature_names_;
};

class SETTINGS_API RoiSearchSettings : public ImageSearchSettings
{
    Q_OBJECT
    QML_NAMED_ELEMENT(RoiSearchSettings)
    QML_UNCREATABLE("RoiSearchSettings is managed by AdvancedSettings")

    Q_PROPERTY(int pooledHeight READ pooledHeight WRITE setPooledHeight NOTIFY pooledHeightChanged)
    Q_PROPERTY(int pooledWidth READ pooledWidth WRITE setPooledWidth NOTIFY pooledWidthChanged)
    Q_PROPERTY(int samplingRatio READ samplingRatio WRITE setSamplingRatio NOTIFY samplingRatioChanged)
    Q_PROPERTY(bool aligned READ aligned WRITE setAligned NOTIFY alignedChanged)
    Q_PROPERTY(bool usePca READ usePca WRITE setUsePca NOTIFY usePcaChanged)
    Q_PROPERTY(int pcaDim READ pcaDim WRITE setPcaDim NOTIFY pcaDimChanged)

public:
    explicit RoiSearchSettings(QObject *parent = nullptr);
    ~RoiSearchSettings() override;

    int pooledHeight() const
    {
        return roi_.pooled_height;
    }
    void setPooledHeight(int value);

    int pooledWidth() const
    {
        return roi_.pooled_width;
    }
    void setPooledWidth(int value);

    int samplingRatio() const
    {
        return roi_.sampling_ratio;
    }
    void setSamplingRatio(int value);

    bool aligned() const
    {
        return roi_.aligned;
    }
    void setAligned(bool value);

    bool usePca() const
    {
        return roi_.use_pca;
    }
    void setUsePca(bool value);

    int pcaDim() const
    {
        return roi_.pca_dim;
    }
    void setPcaDim(int value);

    void        load(const QVariantMap &row);
    QVariantMap saveMap() const;
    void        reset();

signals:
    void pooledHeightChanged();
    void pooledWidthChanged();
    void samplingRatioChanged();
    void alignedChanged();
    void usePcaChanged();
    void pcaDimChanged();

private:
    struct RoiAlignSettings
    {
        int  pooled_height{7};
        int  pooled_width{7};
        int  sampling_ratio{-1};
        bool aligned{false};
        bool use_pca{false};
        int  pca_dim{0};
    };

    RoiAlignSettings roi_;
};

class SETTINGS_API SmartAnnotationSettings : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SmartAnnotationSettings)
    QML_UNCREATABLE("SmartAnnotationSettings is managed by AdvancedSettings")

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QString modelPath READ modelPath WRITE setModelPath NOTIFY modelPathChanged)
    Q_PROPERTY(QString modelBackend READ modelBackend WRITE setModelBackend NOTIFY modelBackendChanged)
    Q_PROPERTY(QString modelDevice READ modelDevice WRITE setModelDevice NOTIFY modelDeviceChanged)
    Q_PROPERTY(double maskThreshold READ maskThreshold WRITE setMaskThreshold NOTIFY maskThresholdChanged)
    Q_PROPERTY(double polygonSimplifyEpsilon READ polygonSimplifyEpsilon WRITE setPolygonSimplifyEpsilon NOTIFY
                   polygonSimplifyEpsilonChanged)
    Q_PROPERTY(double maskAlpha READ maskAlpha WRITE setMaskAlpha NOTIFY maskAlphaChanged)
    Q_PROPERTY(int refreshInterval READ refreshInterval WRITE setRefreshInterval NOTIFY refreshIntervalChanged)

public:
    explicit SmartAnnotationSettings(QObject *parent = nullptr);
    ~SmartAnnotationSettings() override;

    bool enabled() const
    {
        return model_.enabled;
    }
    void setEnabled(bool value);

    QString model() const
    {
        return model_.name;
    }
    void setModel(const QString &value);

    QString modelPath() const
    {
        return model_.path;
    }
    void setModelPath(const QString &value);

    QString modelBackend() const
    {
        return model_.backend;
    }
    void setModelBackend(const QString &value);

    QString modelDevice() const
    {
        return model_.device;
    }
    void setModelDevice(const QString &value);

    double maskThreshold() const
    {
        return mask_.threshold;
    }
    void setMaskThreshold(double value);

    double polygonSimplifyEpsilon() const
    {
        return polygon_.simplify_epsilon;
    }
    void setPolygonSimplifyEpsilon(double value);

    double maskAlpha() const
    {
        return mask_.alpha;
    }
    void setMaskAlpha(double value);

    int refreshInterval() const
    {
        return preview_.refresh_interval;
    }
    void setRefreshInterval(int value);

    void        load(const QVariantMap &row);
    QVariantMap saveMap() const;
    void        reset();

signals:
    void enabledChanged();
    void modelChanged();
    void modelPathChanged();
    void modelBackendChanged();
    void modelDeviceChanged();
    void maskThresholdChanged();
    void polygonSimplifyEpsilonChanged();
    void maskAlphaChanged();
    void refreshIntervalChanged();

private:
    struct ModelSettings
    {
        bool    enabled{false};
        QString name{QStringLiteral("edge_sam")};
        QString path{QStringLiteral("F:/models/edge_sam.wts")};
        QString backend{QStringLiteral("tensorrt")};
        QString device{QStringLiteral("gpu")};
    };

    struct MaskSettings
    {
        double threshold{0.0};
        double alpha{0.35};
    };

    struct PolygonSettings
    {
        double simplify_epsilon{2.0};
    };

    struct PreviewSettings
    {
        int refresh_interval{80};
    };

    ModelSettings   model_;
    MaskSettings    mask_;
    PolygonSettings polygon_;
    PreviewSettings preview_;
};

class SETTINGS_API AdvancedSettings : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(AdvancedSettings)
    QML_UNCREATABLE("AdvancedSettings is managed by GlobalSettings")

    Q_PROPERTY(ImageSearchSettings *imageSearch READ imageSearch CONSTANT)
    Q_PROPERTY(RoiSearchSettings *roiSearch READ roiSearch CONSTANT)
    Q_PROPERTY(SmartAnnotationSettings *smartAnnotation READ smartAnnotation CONSTANT)

public:
    explicit AdvancedSettings(QObject *parent = nullptr);
    ~AdvancedSettings() override;

    ImageSearchSettings *imageSearch() const
    {
        return image_search_;
    }

    RoiSearchSettings *roiSearch() const
    {
        return roi_search_;
    }

    SmartAnnotationSettings *smartAnnotation() const
    {
        return smart_annotation_;
    }

    void load(database::SettingsDataBase *database);
    void save(database::SettingsDataBase *database);
    void reset();

private:
    ImageSearchSettings     *image_search_{nullptr};
    RoiSearchSettings       *roi_search_{nullptr};
    SmartAnnotationSettings *smart_annotation_{nullptr};
};

} // namespace dltool::settings
