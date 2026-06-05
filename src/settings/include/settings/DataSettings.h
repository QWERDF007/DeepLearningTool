#pragma once

#include "settings/SettingsExport.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml>

namespace dltool::database {
class SettingsDataBase;
}

namespace dltool::settings {

/**
 * @brief 数据相关设置
 * 
 * 包含图像处理、标注显示等相关的配置
 */
class SETTINGS_API DataSettings : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DataSettings)
    QML_UNCREATABLE("DataSettings is managed by GlobalSettings")

    // 缩略图边距
    Q_PROPERTY(int thumbnailMargin READ thumbnailMargin WRITE setThumbnailMargin NOTIFY thumbnailMarginChanged)

    // 缩略图缓存大小（MB）
    Q_PROPERTY(
        int thumbnailCacheSize READ thumbnailCacheSize WRITE setThumbnailCacheSize NOTIFY thumbnailCacheSizeChanged)

    // 图像加载线程数
    Q_PROPERTY(int imageLoadThreads READ imageLoadThreads WRITE setImageLoadThreads NOTIFY imageLoadThreadsChanged)

    // 标注边框宽度
    Q_PROPERTY(int labelBorderWidth READ labelBorderWidth WRITE setLabelBorderWidth NOTIFY labelBorderWidthChanged)

    // 标注填充透明度 (0-100)
    Q_PROPERTY(int labelFillOpacity READ labelFillOpacity WRITE setLabelFillOpacity NOTIFY labelFillOpacityChanged)

    // 图像单元格缩放
    Q_PROPERTY(double imageCellScale READ imageCellScale WRITE setImageCellScale NOTIFY imageCellScaleChanged)
    Q_PROPERTY(
        double imageCellScaleFrom READ imageCellScaleFrom WRITE setImageCellScaleFrom NOTIFY imageCellScaleFromChanged)
    Q_PROPERTY(double imageCellScaleTo READ imageCellScaleTo WRITE setImageCellScaleTo NOTIFY imageCellScaleToChanged)
    Q_PROPERTY(double imageCellScaleStepSize READ imageCellScaleStepSize WRITE setImageCellScaleStepSize NOTIFY
                   imageCellScaleStepSizeChanged)

    // 标注缩略图缩放
    Q_PROPERTY(double labelThumbnailScale READ labelThumbnailScale WRITE setLabelThumbnailScale NOTIFY
                   labelThumbnailScaleChanged)
    Q_PROPERTY(double labelThumbnailScaleFrom READ labelThumbnailScaleFrom CONSTANT)
    Q_PROPERTY(double labelThumbnailScaleTo READ labelThumbnailScaleTo CONSTANT)
    Q_PROPERTY(double labelThumbnailScaleStepSize READ labelThumbnailScaleStepSize CONSTANT)

    // 标注缩略图长宽比
    Q_PROPERTY(double labelThumbnailAspectRatio READ labelThumbnailAspectRatio WRITE setLabelThumbnailAspectRatio NOTIFY
                   labelThumbnailAspectRatioChanged)
    Q_PROPERTY(double labelThumbnailAspectRatioFrom READ labelThumbnailAspectRatioFrom CONSTANT)
    Q_PROPERTY(double labelThumbnailAspectRatioTo READ labelThumbnailAspectRatioTo CONSTANT)
    Q_PROPERTY(double labelThumbnailAspectRatioStepSize READ labelThumbnailAspectRatioStepSize CONSTANT)

    // 标注缩略图边界扩展
    Q_PROPERTY(double labelThumbnailBorderPadding READ labelThumbnailBorderPadding WRITE setLabelThumbnailBorderPadding
                   NOTIFY labelThumbnailBorderPaddingChanged)
    Q_PROPERTY(double labelThumbnailBorderPaddingFrom READ labelThumbnailBorderPaddingFrom CONSTANT)
    Q_PROPERTY(double labelThumbnailBorderPaddingTo READ labelThumbnailBorderPaddingTo CONSTANT)
    Q_PROPERTY(double labelThumbnailBorderPaddingStepSize READ labelThumbnailBorderPaddingStepSize CONSTANT)

    Q_PROPERTY(bool featureExtractionEnabled READ featureExtractionEnabled WRITE setFeatureExtractionEnabled NOTIFY
                   featureExtractionEnabledChanged)
    Q_PROPERTY(QString featureExtractionModel READ featureExtractionModel WRITE setFeatureExtractionModel NOTIFY
                   featureExtractionModelChanged)
    Q_PROPERTY(QString featureExtractionModelPath READ featureExtractionModelPath WRITE setFeatureExtractionModelPath
                   NOTIFY featureExtractionModelPathChanged)
    Q_PROPERTY(QString featureExtractionFeatureName READ featureExtractionFeatureName WRITE
                   setFeatureExtractionFeatureName NOTIFY featureExtractionFeatureNameChanged)
    Q_PROPERTY(bool featureExtractionRebuildIndex READ featureExtractionRebuildIndex WRITE
                   setFeatureExtractionRebuildIndex NOTIFY featureExtractionRebuildIndexChanged)
    Q_PROPERTY(int featureExtractionTopK READ featureExtractionTopK WRITE setFeatureExtractionTopK NOTIFY
                   featureExtractionTopKChanged)
    Q_PROPERTY(QString featureExtractionNorm READ featureExtractionNorm WRITE setFeatureExtractionNorm NOTIFY
                   featureExtractionNormChanged)
    Q_PROPERTY(QString featureExtractionPreprocessBackend READ featureExtractionPreprocessBackend WRITE
                   setFeatureExtractionPreprocessBackend NOTIFY featureExtractionPreprocessBackendChanged)
    Q_PROPERTY(QString featureExtractionFaissBackend READ featureExtractionFaissBackend WRITE
                   setFeatureExtractionFaissBackend NOTIFY featureExtractionFaissBackendChanged)
    Q_PROPERTY(QString featureExtractionIndexStorage READ featureExtractionIndexStorage WRITE
                   setFeatureExtractionIndexStorage NOTIFY featureExtractionIndexStorageChanged)
    Q_PROPERTY(int featureExtractionDiskBuildBatchSize READ featureExtractionDiskBuildBatchSize WRITE
                   setFeatureExtractionDiskBuildBatchSize NOTIFY featureExtractionDiskBuildBatchSizeChanged)
    Q_PROPERTY(int featureExtractionModelBatchSize READ featureExtractionModelBatchSize WRITE
                   setFeatureExtractionModelBatchSize NOTIFY featureExtractionModelBatchSizeChanged)
    Q_PROPERTY(QString featureExtractionModelBackend READ featureExtractionModelBackend WRITE
                   setFeatureExtractionModelBackend NOTIFY featureExtractionModelBackendChanged)
    Q_PROPERTY(QString featureExtractionModelDevice READ featureExtractionModelDevice WRITE
                   setFeatureExtractionModelDevice NOTIFY featureExtractionModelDeviceChanged)
    Q_PROPERTY(QString featureExtractionIndexDirectory READ featureExtractionIndexDirectory WRITE
                   setFeatureExtractionIndexDirectory NOTIFY featureExtractionIndexDirectoryChanged)

    // 智能标注设置
    Q_PROPERTY(bool smartAnnotationEnabled READ smartAnnotationEnabled WRITE setSmartAnnotationEnabled NOTIFY
                   smartAnnotationEnabledChanged)
    Q_PROPERTY(QString smartAnnotationModel READ smartAnnotationModel WRITE setSmartAnnotationModel NOTIFY
                   smartAnnotationModelChanged)
    Q_PROPERTY(QString smartAnnotationModelPath READ smartAnnotationModelPath WRITE setSmartAnnotationModelPath NOTIFY
                   smartAnnotationModelPathChanged)
    Q_PROPERTY(QString smartAnnotationModelBackend READ smartAnnotationModelBackend WRITE setSmartAnnotationModelBackend
                   NOTIFY smartAnnotationModelBackendChanged)
    Q_PROPERTY(QString smartAnnotationModelDevice READ smartAnnotationModelDevice WRITE setSmartAnnotationModelDevice
                   NOTIFY smartAnnotationModelDeviceChanged)
    Q_PROPERTY(double smartAnnotationMaskThreshold READ smartAnnotationMaskThreshold WRITE
                   setSmartAnnotationMaskThreshold NOTIFY smartAnnotationMaskThresholdChanged)
    Q_PROPERTY(double smartAnnotationPolygonSimplifyEpsilon READ smartAnnotationPolygonSimplifyEpsilon WRITE
                   setSmartAnnotationPolygonSimplifyEpsilon NOTIFY smartAnnotationPolygonSimplifyEpsilonChanged)
    Q_PROPERTY(double smartAnnotationMaskAlpha READ smartAnnotationMaskAlpha WRITE setSmartAnnotationMaskAlpha NOTIFY
                   smartAnnotationMaskAlphaChanged)
    Q_PROPERTY(int smartAnnotationRefreshInterval READ smartAnnotationRefreshInterval WRITE
                   setSmartAnnotationRefreshInterval NOTIFY smartAnnotationRefreshIntervalChanged)

public:
    explicit DataSettings(QObject *parent = nullptr);
    ~DataSettings();

    int thumbnailMargin() const
    {
        return thumbnail_margin_;
    }

    void setThumbnailMargin(int value);

    int thumbnailCacheSize() const
    {
        return thumbnail_cache_size_;
    }

    void setThumbnailCacheSize(int value);

    int imageLoadThreads() const
    {
        return image_load_threads_;
    }

    void setImageLoadThreads(int value);

    int labelBorderWidth() const
    {
        return label_border_width_;
    }

    void setLabelBorderWidth(int value);

    int labelFillOpacity() const
    {
        return label_fill_opacity_;
    }

    void setLabelFillOpacity(int value);

    double imageCellScale() const
    {
        return image_cell_scale_;
    }

    void setImageCellScale(double value);

    double imageCellScaleFrom() const
    {
        return image_cell_scale_from_;
    }

    void setImageCellScaleFrom(double value);

    double imageCellScaleTo() const
    {
        return image_cell_scale_to_;
    }

    void setImageCellScaleTo(double value);

    double imageCellScaleStepSize() const
    {
        return image_cell_scale_step_size_;
    }

    void setImageCellScaleStepSize(double value);

    double labelThumbnailScale() const
    {
        return label_thumbnail_scale_;
    }

    void setLabelThumbnailScale(double value);

    double labelThumbnailScaleFrom() const
    {
        return label_thumbnail_scale_from_;
    }

    double labelThumbnailScaleTo() const
    {
        return label_thumbnail_scale_to_;
    }

    double labelThumbnailScaleStepSize() const
    {
        return label_thumbnail_scale_step_size_;
    }

    double labelThumbnailAspectRatio() const
    {
        return label_thumbnail_aspect_ratio_;
    }

    void setLabelThumbnailAspectRatio(double value);

    double labelThumbnailAspectRatioFrom() const
    {
        return label_thumbnail_aspect_ratio_from_;
    }

    double labelThumbnailAspectRatioTo() const
    {
        return label_thumbnail_aspect_ratio_to_;
    }

    double labelThumbnailAspectRatioStepSize() const
    {
        return label_thumbnail_aspect_ratio_step_size_;
    }

    double labelThumbnailBorderPadding() const
    {
        return label_thumbnail_border_padding_;
    }

    void setLabelThumbnailBorderPadding(double value);

    double labelThumbnailBorderPaddingFrom() const
    {
        return label_thumbnail_border_padding_from_;
    }

    double labelThumbnailBorderPaddingTo() const
    {
        return label_thumbnail_border_padding_to_;
    }

    double labelThumbnailBorderPaddingStepSize() const
    {
        return label_thumbnail_border_padding_step_size_;
    }

    bool featureExtractionEnabled() const
    {
        return feature_extraction_enabled_;
    }

    void setFeatureExtractionEnabled(bool value);

    QString featureExtractionModel() const
    {
        return feature_extraction_model_;
    }

    void setFeatureExtractionModel(const QString &value);

    QString featureExtractionModelPath() const
    {
        return feature_extraction_model_path_;
    }

    void setFeatureExtractionModelPath(const QString &value);

    QString featureExtractionFeatureName() const
    {
        return feature_extraction_feature_name_;
    }

    void setFeatureExtractionFeatureName(const QString &value);

    bool featureExtractionRebuildIndex() const
    {
        return feature_extraction_rebuild_index_;
    }

    void setFeatureExtractionRebuildIndex(bool value);

    int featureExtractionTopK() const
    {
        return feature_extraction_top_k_;
    }

    void setFeatureExtractionTopK(int value);

    QString featureExtractionNorm() const
    {
        return feature_extraction_norm_;
    }

    void setFeatureExtractionNorm(const QString &value);

    QString featureExtractionPreprocessBackend() const
    {
        return feature_extraction_preprocess_backend_;
    }

    void setFeatureExtractionPreprocessBackend(const QString &value);

    QString featureExtractionFaissBackend() const
    {
        return feature_extraction_faiss_backend_;
    }

    void setFeatureExtractionFaissBackend(const QString &value);

    QString featureExtractionIndexStorage() const
    {
        return feature_extraction_index_storage_;
    }

    void setFeatureExtractionIndexStorage(const QString &value);

    int featureExtractionDiskBuildBatchSize() const
    {
        return feature_extraction_disk_build_batch_size_;
    }

    void setFeatureExtractionDiskBuildBatchSize(int value);

    int featureExtractionModelBatchSize() const
    {
        return feature_extraction_model_batch_size_;
    }

    void setFeatureExtractionModelBatchSize(int value);

    QString featureExtractionModelBackend() const
    {
        return feature_extraction_model_backend_;
    }

    void setFeatureExtractionModelBackend(const QString &value);

    QString featureExtractionModelDevice() const
    {
        return feature_extraction_model_device_;
    }

    void setFeatureExtractionModelDevice(const QString &value);

    QString featureExtractionIndexDirectory() const
    {
        return feature_extraction_index_directory_;
    }

    void setFeatureExtractionIndexDirectory(const QString &value);

    bool smartAnnotationEnabled() const
    {
        return smart_annotation_enabled_;
    }

    void setSmartAnnotationEnabled(bool value);

    QString smartAnnotationModel() const
    {
        return smart_annotation_model_;
    }

    void setSmartAnnotationModel(const QString &value);

    QString smartAnnotationModelPath() const
    {
        return smart_annotation_model_path_;
    }

    void setSmartAnnotationModelPath(const QString &value);

    QString smartAnnotationModelBackend() const
    {
        return smart_annotation_model_backend_;
    }

    void setSmartAnnotationModelBackend(const QString &value);

    QString smartAnnotationModelDevice() const
    {
        return smart_annotation_model_device_;
    }

    void setSmartAnnotationModelDevice(const QString &value);

    double smartAnnotationMaskThreshold() const
    {
        return smart_annotation_mask_threshold_;
    }

    void setSmartAnnotationMaskThreshold(double value);

    double smartAnnotationPolygonSimplifyEpsilon() const
    {
        return smart_annotation_polygon_simplify_epsilon_;
    }

    void setSmartAnnotationPolygonSimplifyEpsilon(double value);

    double smartAnnotationMaskAlpha() const
    {
        return smart_annotation_mask_alpha_;
    }

    void setSmartAnnotationMaskAlpha(double value);

    int smartAnnotationRefreshInterval() const
    {
        return smart_annotation_refresh_interval_;
    }

    void setSmartAnnotationRefreshInterval(int value);

    Q_INVOKABLE QStringList featureExtractionCustomFeatureNames(const QString &model_name) const;

    Q_INVOKABLE void addFeatureExtractionCustomFeatureName(const QString &model_name, const QString &feature_name);

    /**
     * @brief 从数据库加载设置
     * @param database 设置数据库
     */
    void load(database::SettingsDataBase *database);

    /**
     * @brief 保存设置到数据库
     * @param database 设置数据库
     */
    void save(database::SettingsDataBase *database);

    /**
     * @brief 重置所有设置为默认值
     */
    void reset();

signals:
    void thumbnailMarginChanged();
    void thumbnailCacheSizeChanged();
    void imageLoadThreadsChanged();
    void labelBorderWidthChanged();
    void labelFillOpacityChanged();
    void imageCellScaleChanged();
    void imageCellScaleFromChanged();
    void imageCellScaleToChanged();
    void imageCellScaleStepSizeChanged();
    void labelThumbnailScaleChanged();
    void labelThumbnailAspectRatioChanged();
    void labelThumbnailBorderPaddingChanged();
    void featureExtractionEnabledChanged();
    void featureExtractionModelChanged();
    void featureExtractionModelPathChanged();
    void featureExtractionFeatureNameChanged();
    void featureExtractionRebuildIndexChanged();
    void featureExtractionTopKChanged();
    void featureExtractionNormChanged();
    void featureExtractionPreprocessBackendChanged();
    void featureExtractionFaissBackendChanged();
    void featureExtractionIndexStorageChanged();
    void featureExtractionDiskBuildBatchSizeChanged();
    void featureExtractionModelBatchSizeChanged();
    void featureExtractionModelBackendChanged();
    void featureExtractionModelDeviceChanged();
    void featureExtractionIndexDirectoryChanged();
    void featureExtractionCustomFeatureNamesChanged();
    void smartAnnotationEnabledChanged();
    void smartAnnotationModelChanged();
    void smartAnnotationModelPathChanged();
    void smartAnnotationModelBackendChanged();
    void smartAnnotationModelDeviceChanged();
    void smartAnnotationMaskThresholdChanged();
    void smartAnnotationPolygonSimplifyEpsilonChanged();
    void smartAnnotationMaskAlphaChanged();
    void smartAnnotationRefreshIntervalChanged();

private:
    QString featureExtractionCustomFeatureNamesJson() const;
    void    setFeatureExtractionCustomFeatureNamesJson(const QString &value);

    int thumbnail_margin_{10};
    int thumbnail_cache_size_{100}; // 100MB
    int image_load_threads_{4};
    int label_border_width_{2};
    int label_fill_opacity_{30}; // 30%

    double image_cell_scale_{1.0};
    double image_cell_scale_from_{0.5};
    double image_cell_scale_to_{4.0};
    double image_cell_scale_step_size_{0.25};

    double label_thumbnail_scale_{1.0};
    double label_thumbnail_scale_from_{0.5};
    double label_thumbnail_scale_to_{4.0};
    double label_thumbnail_scale_step_size_{0.25};

    double label_thumbnail_aspect_ratio_{1.0};
    double label_thumbnail_aspect_ratio_from_{0.5};
    double label_thumbnail_aspect_ratio_to_{2.0};
    double label_thumbnail_aspect_ratio_step_size_{0.1};

    double label_thumbnail_border_padding_{0.1};
    double label_thumbnail_border_padding_from_{0.0};
    double label_thumbnail_border_padding_to_{1.0};
    double label_thumbnail_border_padding_step_size_{0.1};

    bool    feature_extraction_enabled_{true};
    QString feature_extraction_model_{QStringLiteral("resnet18")};
    QString feature_extraction_model_path_{QStringLiteral("F:/models/resnet18.wts")};
    QString feature_extraction_feature_name_{QStringLiteral("layer4")};
    bool    feature_extraction_rebuild_index_{false};
    int     feature_extraction_top_k_{5};
    QString feature_extraction_norm_{QStringLiteral("l2")};
    QString feature_extraction_preprocess_backend_{QStringLiteral("cpu")};
    QString feature_extraction_faiss_backend_{QStringLiteral("cpu")};
    QString feature_extraction_index_storage_{QStringLiteral("ram")};
    int     feature_extraction_disk_build_batch_size_{256};
    int     feature_extraction_model_batch_size_{1};
    QString feature_extraction_model_backend_{QStringLiteral("tensorrt")};
    QString feature_extraction_model_device_{QStringLiteral("gpu")};
    QString feature_extraction_index_directory_;

    QHash<QString, QStringList> feature_extraction_custom_feature_names_;

    bool    smart_annotation_enabled_{false};
    QString smart_annotation_model_{QStringLiteral("edge_sam")};
    QString smart_annotation_model_path_{QStringLiteral("F:/models/edge_sam.wts")};
    QString smart_annotation_model_backend_{QStringLiteral("tensorrt")};
    QString smart_annotation_model_device_{QStringLiteral("gpu")};
    double  smart_annotation_mask_threshold_{0.0};
    double  smart_annotation_polygon_simplify_epsilon_{2.0};
    double  smart_annotation_mask_alpha_{0.35};
    int     smart_annotation_refresh_interval_{80};
};

} // namespace dltool::settings
