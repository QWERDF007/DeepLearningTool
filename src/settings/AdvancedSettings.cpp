#include "settings/AdvancedSettings.h"

#include "database/DataBase.h"

#include <spdlog/spdlog.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include <algorithm>

namespace dltool::settings {

namespace {

constexpr const char *kDefaultImageSearchModel       = "resnet18";
constexpr const char *kDefaultImageSearchModelPath   = "F:/models/resnet18.wts";
constexpr const char *kDefaultImageSearchFeatureName = "layer4";
constexpr const char *kDefaultSmartAnnotationModel   = "edge_sam";
constexpr const char *kDefaultSmartAnnotationPath    = "F:/models/edge_sam.wts";

QString normalizedImageSearchModel(QString value)
{
    value = value.trimmed();
    return value.isEmpty() ? QString::fromLatin1(kDefaultImageSearchModel) : value;
}

QString normalizedFeatureName(QString value)
{
    value = value.trimmed();
    return value.isEmpty() ? QString::fromLatin1(kDefaultImageSearchFeatureName) : value;
}

QString normalizedSmartAnnotationModel(QString value)
{
    value = value.trimmed();
    return value.isEmpty() ? QString::fromLatin1(kDefaultSmartAnnotationModel) : value;
}

QString normalizedOption(QString value, const QStringList &allowed_values, const QString &default_value)
{
    value = value.trimmed().toLower();
    return allowed_values.contains(value) ? value : default_value;
}

void appendUnique(QStringList &values, QString value)
{
    value = value.trimmed();
    if (!value.isEmpty() && !values.contains(value))
    {
        values.append(value);
    }
}

QVariant valueWithFallback(const QVariantMap &row, const QString &key, const QString &legacy_key,
                           const QVariant &default_value)
{
    if (row.contains(key))
    {
        return row.value(key);
    }
    return row.value(legacy_key, default_value);
}

} // namespace

ImageSearchSettings::ImageSearchSettings(QObject *parent)
    : QObject(parent)
{
}

ImageSearchSettings::~ImageSearchSettings() = default;

void ImageSearchSettings::setEnabled(bool value)
{
    if (options_.enabled != value)
    {
        options_.enabled = value;
        emit enabledChanged();
    }
}

void ImageSearchSettings::setModel(const QString &value)
{
    const QString model = normalizedImageSearchModel(value);
    if (model_.name != model)
    {
        model_.name = model;
        emit modelChanged();
    }
}

void ImageSearchSettings::setModelPath(const QString &value)
{
    const QString path = value.trimmed();
    if (model_.path != path)
    {
        model_.path = path;
        emit modelPathChanged();
    }
}

void ImageSearchSettings::setFeatureName(const QString &value)
{
    const QString feature_name = normalizedFeatureName(value);
    if (model_.feature_name != feature_name)
    {
        model_.feature_name = feature_name;
        emit featureNameChanged();
    }
}

void ImageSearchSettings::setRebuildIndex(bool value)
{
    if (options_.rebuild_index != value)
    {
        options_.rebuild_index = value;
        emit rebuildIndexChanged();
    }
}

void ImageSearchSettings::setTopK(int value)
{
    value = std::clamp(value, 1, 1000);
    if (options_.top_k != value)
    {
        options_.top_k = value;
        emit topKChanged();
    }
}

void ImageSearchSettings::setNorm(const QString &value)
{
    const QString norm = normalizedOption(value, {QStringLiteral("l2"), QStringLiteral("l1"), QStringLiteral("none")},
                                          QStringLiteral("l2"));
    if (index_.norm != norm)
    {
        index_.norm = norm;
        emit normChanged();
    }
}

void ImageSearchSettings::setPreprocessBackend(const QString &value)
{
    const QString backend
        = normalizedOption(value, {QStringLiteral("cpu"), QStringLiteral("gpu")}, QStringLiteral("cpu"));
    if (runtime_.preprocess_backend != backend)
    {
        runtime_.preprocess_backend = backend;
        emit preprocessBackendChanged();
    }
}

void ImageSearchSettings::setFaissBackend(const QString &value)
{
    const QString backend
        = normalizedOption(value, {QStringLiteral("cpu"), QStringLiteral("gpu")}, QStringLiteral("cpu"));
    if (runtime_.faiss_backend != backend)
    {
        runtime_.faiss_backend = backend;
        emit faissBackendChanged();
    }
    if (backend == QStringLiteral("gpu"))
    {
        setIndexStorage(QStringLiteral("ram"));
    }
}

void ImageSearchSettings::setIndexStorage(const QString &value)
{
    QString storage = normalizedOption(value, {QStringLiteral("ram"), QStringLiteral("disk")}, QStringLiteral("ram"));
    if (runtime_.faiss_backend == QStringLiteral("gpu"))
    {
        storage = QStringLiteral("ram");
    }

    if (index_.storage != storage)
    {
        index_.storage = storage;
        emit indexStorageChanged();
    }
}

void ImageSearchSettings::setDiskBuildBatchSize(int value)
{
    value = std::clamp(value, 1, 8192);
    if (index_.disk_build_batch_size != value)
    {
        index_.disk_build_batch_size = value;
        emit diskBuildBatchSizeChanged();
    }
}

void ImageSearchSettings::setModelBatchSize(int value)
{
    value = std::clamp(value, 1, 8192);
    if (model_.batch_size != value)
    {
        model_.batch_size = value;
        emit modelBatchSizeChanged();
    }
}

void ImageSearchSettings::setModelBackend(const QString &value)
{
    const QString backend = normalizedOption(value,
                                             {QStringLiteral("tensorrt"), QStringLiteral("openvino"),
                                              QStringLiteral("onnxruntime")},
                                             QStringLiteral("tensorrt"));
    if (runtime_.model_backend != backend)
    {
        runtime_.model_backend = backend;
        emit modelBackendChanged();
    }
}

void ImageSearchSettings::setModelDevice(const QString &value)
{
    const QString device
        = normalizedOption(value, {QStringLiteral("cpu"), QStringLiteral("gpu")}, QStringLiteral("gpu"));
    if (runtime_.model_device != device)
    {
        runtime_.model_device = device;
        emit modelDeviceChanged();
    }
}

void ImageSearchSettings::setIndexDirectory(const QString &value)
{
    const QString path = value.trimmed();
    if (index_.directory != path)
    {
        index_.directory = path;
        emit indexDirectoryChanged();
    }
}

QStringList ImageSearchSettings::customFeatureNames(const QString &model_name) const
{
    return custom_feature_names_.value(normalizedImageSearchModel(model_name));
}

void ImageSearchSettings::addCustomFeatureName(const QString &model_name, const QString &feature_name)
{
    const QString model   = normalizedImageSearchModel(model_name);
    const QString feature = feature_name.trimmed();
    if (feature.isEmpty())
    {
        return;
    }

    QStringList values = custom_feature_names_.value(model);
    if (values.contains(feature))
    {
        return;
    }

    values.append(feature);
    custom_feature_names_.insert(model, values);
    emit customFeatureNamesChanged();
}

QString ImageSearchSettings::customFeatureNamesJson() const
{
    QJsonObject root;
    const auto  keys = custom_feature_names_.keys();
    for (const QString &model : keys)
    {
        QJsonArray names;
        for (const QString &feature_name : custom_feature_names_.value(model))
        {
            names.append(feature_name);
        }
        root.insert(model, names);
    }
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void ImageSearchSettings::setCustomFeatureNamesJson(const QString &value)
{
    QHash<QString, QStringList> parsed_names;
    const QJsonDocument         document = QJsonDocument::fromJson(value.toUtf8());
    if (document.isObject())
    {
        const QJsonObject root = document.object();
        for (auto it = root.constBegin(); it != root.constEnd(); ++it)
        {
            QStringList names;
            if (it.value().isArray())
            {
                for (const QJsonValue &entry : it.value().toArray())
                {
                    appendUnique(names, entry.toString());
                }
            }
            else if (it.value().isString())
            {
                appendUnique(names, it.value().toString());
            }

            if (!names.isEmpty())
            {
                parsed_names.insert(normalizedImageSearchModel(it.key()), names);
            }
        }
    }

    if (custom_feature_names_ != parsed_names)
    {
        custom_feature_names_ = parsed_names;
        emit customFeatureNamesChanged();
    }
}

void ImageSearchSettings::load(const QVariantMap &row)
{
    setEnabled(row.value(QStringLiteral("enabled"), true).toBool());
    setModel(row.value(QStringLiteral("model"), QString::fromLatin1(kDefaultImageSearchModel)).toString());
    setModelPath(row.value(QStringLiteral("model_path"), QString::fromLatin1(kDefaultImageSearchModelPath)).toString());
    setFeatureName(
        row.value(QStringLiteral("feature_name"), QString::fromLatin1(kDefaultImageSearchFeatureName)).toString());
    setRebuildIndex(row.value(QStringLiteral("rebuild_index"), false).toBool());
    setTopK(row.value(QStringLiteral("top_k"), 5).toInt());
    setNorm(row.value(QStringLiteral("norm"), QStringLiteral("l2")).toString());
    setPreprocessBackend(row.value(QStringLiteral("preprocess_backend"), QStringLiteral("cpu")).toString());
    setFaissBackend(row.value(QStringLiteral("faiss_backend"), QStringLiteral("cpu")).toString());
    setIndexStorage(row.value(QStringLiteral("index_storage"), QStringLiteral("ram")).toString());
    setDiskBuildBatchSize(row.value(QStringLiteral("disk_build_batch_size"), 256).toInt());
    setModelBatchSize(row.value(QStringLiteral("model_batch_size"), 1).toInt());
    setModelBackend(row.value(QStringLiteral("model_backend"), QStringLiteral("tensorrt")).toString());
    setModelDevice(row.value(QStringLiteral("model_device"), QStringLiteral("gpu")).toString());
    setIndexDirectory(row.value(QStringLiteral("index_directory"), QString()).toString());
    setCustomFeatureNamesJson(row.value(QStringLiteral("custom_feature_names"), QStringLiteral("{}")).toString());
}

QVariantMap ImageSearchSettings::saveMap() const
{
    return QVariantMap{
        {             QStringLiteral("enabled"), options_.enabled},
        {               QStringLiteral("model"), model_.name},
        {          QStringLiteral("model_path"), model_.path},
        {        QStringLiteral("feature_name"), model_.feature_name},
        {       QStringLiteral("rebuild_index"), options_.rebuild_index},
        {               QStringLiteral("top_k"), options_.top_k},
        {                QStringLiteral("norm"), index_.norm},
        {  QStringLiteral("preprocess_backend"), runtime_.preprocess_backend},
        {       QStringLiteral("faiss_backend"), runtime_.faiss_backend},
        {       QStringLiteral("index_storage"), index_.storage},
        {QStringLiteral("disk_build_batch_size"), index_.disk_build_batch_size},
        {     QStringLiteral("model_batch_size"), model_.batch_size},
        {       QStringLiteral("model_backend"), runtime_.model_backend},
        {        QStringLiteral("model_device"), runtime_.model_device},
        {      QStringLiteral("index_directory"), index_.directory},
        {   QStringLiteral("custom_feature_names"), customFeatureNamesJson()},
    };
}

void ImageSearchSettings::reset()
{
    setEnabled(true);
    setModel(QString::fromLatin1(kDefaultImageSearchModel));
    setModelPath(QString::fromLatin1(kDefaultImageSearchModelPath));
    setFeatureName(QString::fromLatin1(kDefaultImageSearchFeatureName));
    setRebuildIndex(false);
    setTopK(5);
    setNorm(QStringLiteral("l2"));
    setPreprocessBackend(QStringLiteral("cpu"));
    setFaissBackend(QStringLiteral("cpu"));
    setIndexStorage(QStringLiteral("ram"));
    setDiskBuildBatchSize(256);
    setModelBatchSize(1);
    setModelBackend(QStringLiteral("tensorrt"));
    setModelDevice(QStringLiteral("gpu"));
    setIndexDirectory(QString());
    setCustomFeatureNamesJson(QStringLiteral("{}"));
}

RoiSearchSettings::RoiSearchSettings(QObject *parent)
    : ImageSearchSettings(parent)
{
}

RoiSearchSettings::~RoiSearchSettings() = default;

void RoiSearchSettings::setPooledHeight(int value)
{
    value = std::clamp(value, 1, 64);
    if (roi_.pooled_height != value)
    {
        roi_.pooled_height = value;
        emit pooledHeightChanged();
    }
}

void RoiSearchSettings::setPooledWidth(int value)
{
    value = std::clamp(value, 1, 64);
    if (roi_.pooled_width != value)
    {
        roi_.pooled_width = value;
        emit pooledWidthChanged();
    }
}

void RoiSearchSettings::setSamplingRatio(int value)
{
    value = std::clamp(value, -1, 32);
    if (roi_.sampling_ratio != value)
    {
        roi_.sampling_ratio = value;
        emit samplingRatioChanged();
    }
}

void RoiSearchSettings::setAligned(bool value)
{
    if (roi_.aligned != value)
    {
        roi_.aligned = value;
        emit alignedChanged();
    }
}

void RoiSearchSettings::setUsePca(bool value)
{
    if (roi_.use_pca != value)
    {
        roi_.use_pca = value;
        emit usePcaChanged();
    }
}

void RoiSearchSettings::setPcaDim(int value)
{
    value = std::clamp(value, 0, 8192);
    if (roi_.pca_dim != value)
    {
        roi_.pca_dim = value;
        emit pcaDimChanged();
    }
}

void RoiSearchSettings::load(const QVariantMap &row)
{
    ImageSearchSettings::load(row);
    setPooledHeight(row.value(QStringLiteral("pooled_height"), 7).toInt());
    setPooledWidth(row.value(QStringLiteral("pooled_width"), 7).toInt());
    setSamplingRatio(row.value(QStringLiteral("sampling_ratio"), -1).toInt());
    setAligned(row.value(QStringLiteral("aligned"), false).toBool());
    setUsePca(row.value(QStringLiteral("use_pca"), false).toBool());
    setPcaDim(row.value(QStringLiteral("pca_dim"), 0).toInt());
}

QVariantMap RoiSearchSettings::saveMap() const
{
    QVariantMap row = ImageSearchSettings::saveMap();
    row.insert(QStringLiteral("pooled_height"), roi_.pooled_height);
    row.insert(QStringLiteral("pooled_width"), roi_.pooled_width);
    row.insert(QStringLiteral("sampling_ratio"), roi_.sampling_ratio);
    row.insert(QStringLiteral("aligned"), roi_.aligned);
    row.insert(QStringLiteral("use_pca"), roi_.use_pca);
    row.insert(QStringLiteral("pca_dim"), roi_.pca_dim);
    return row;
}

void RoiSearchSettings::reset()
{
    ImageSearchSettings::reset();
    setPooledHeight(7);
    setPooledWidth(7);
    setSamplingRatio(-1);
    setAligned(false);
    setUsePca(false);
    setPcaDim(0);
}

SmartAnnotationSettings::SmartAnnotationSettings(QObject *parent)
    : QObject(parent)
{
}

SmartAnnotationSettings::~SmartAnnotationSettings() = default;

void SmartAnnotationSettings::setEnabled(bool value)
{
    if (model_.enabled != value)
    {
        model_.enabled = value;
        emit enabledChanged();
    }
}

void SmartAnnotationSettings::setModel(const QString &value)
{
    const QString model = normalizedSmartAnnotationModel(value);
    if (model_.name != model)
    {
        model_.name = model;
        emit modelChanged();
    }
}

void SmartAnnotationSettings::setModelPath(const QString &value)
{
    const QString path = value.trimmed();
    if (model_.path != path)
    {
        model_.path = path;
        emit modelPathChanged();
    }
}

void SmartAnnotationSettings::setModelBackend(const QString &value)
{
    const QString backend = normalizedOption(value,
                                             {QStringLiteral("tensorrt"), QStringLiteral("openvino"),
                                              QStringLiteral("onnxruntime")},
                                             QStringLiteral("tensorrt"));
    if (model_.backend != backend)
    {
        model_.backend = backend;
        emit modelBackendChanged();
    }
}

void SmartAnnotationSettings::setModelDevice(const QString &value)
{
    const QString device
        = normalizedOption(value, {QStringLiteral("cpu"), QStringLiteral("gpu")}, QStringLiteral("gpu"));
    if (model_.device != device)
    {
        model_.device = device;
        emit modelDeviceChanged();
    }
}

void SmartAnnotationSettings::setMaskThreshold(double value)
{
    value = std::clamp(value, -20.0, 20.0);
    if (mask_.threshold != value)
    {
        mask_.threshold = value;
        emit maskThresholdChanged();
    }
}

void SmartAnnotationSettings::setPolygonSimplifyEpsilon(double value)
{
    value = std::clamp(value, 0.0, 50.0);
    if (polygon_.simplify_epsilon != value)
    {
        polygon_.simplify_epsilon = value;
        emit polygonSimplifyEpsilonChanged();
    }
}

void SmartAnnotationSettings::setMaskAlpha(double value)
{
    value = std::clamp(value, 0.0, 1.0);
    if (mask_.alpha != value)
    {
        mask_.alpha = value;
        emit maskAlphaChanged();
    }
}

void SmartAnnotationSettings::setRefreshInterval(int value)
{
    value = std::clamp(value, 20, 5000);
    if (preview_.refresh_interval != value)
    {
        preview_.refresh_interval = value;
        emit refreshIntervalChanged();
    }
}

void SmartAnnotationSettings::load(const QVariantMap &row)
{
    setEnabled(valueWithFallback(row, QStringLiteral("enabled"), QStringLiteral("smart_annotation_enabled"), false)
                   .toBool());
    setModel(valueWithFallback(row, QStringLiteral("model"), QStringLiteral("smart_annotation_model"),
                               QString::fromLatin1(kDefaultSmartAnnotationModel))
                 .toString());
    setModelPath(valueWithFallback(row, QStringLiteral("model_path"), QStringLiteral("smart_annotation_model_path"),
                                   QString::fromLatin1(kDefaultSmartAnnotationPath))
                     .toString());
    setModelBackend(valueWithFallback(row, QStringLiteral("model_backend"),
                                      QStringLiteral("smart_annotation_model_backend"), QStringLiteral("tensorrt"))
                        .toString());
    setModelDevice(valueWithFallback(row, QStringLiteral("model_device"),
                                     QStringLiteral("smart_annotation_model_device"), QStringLiteral("gpu"))
                       .toString());
    setMaskThreshold(valueWithFallback(row, QStringLiteral("mask_threshold"),
                                       QStringLiteral("smart_annotation_mask_threshold"), 0.0)
                         .toDouble());
    setPolygonSimplifyEpsilon(
        valueWithFallback(row, QStringLiteral("polygon_simplify_epsilon"),
                          QStringLiteral("smart_annotation_polygon_simplify_epsilon"), 2.0)
            .toDouble());
    setMaskAlpha(
        valueWithFallback(row, QStringLiteral("mask_alpha"), QStringLiteral("smart_annotation_mask_alpha"), 0.35)
            .toDouble());
    setRefreshInterval(valueWithFallback(row, QStringLiteral("refresh_interval"),
                                         QStringLiteral("smart_annotation_refresh_interval"), 80)
                           .toInt());
}

QVariantMap SmartAnnotationSettings::saveMap() const
{
    return QVariantMap{
        {             QStringLiteral("enabled"), model_.enabled},
        {               QStringLiteral("model"), model_.name},
        {          QStringLiteral("model_path"), model_.path},
        {       QStringLiteral("model_backend"), model_.backend},
        {        QStringLiteral("model_device"), model_.device},
        {      QStringLiteral("mask_threshold"), mask_.threshold},
        {QStringLiteral("polygon_simplify_epsilon"), polygon_.simplify_epsilon},
        {          QStringLiteral("mask_alpha"), mask_.alpha},
        {    QStringLiteral("refresh_interval"), preview_.refresh_interval},
    };
}

void SmartAnnotationSettings::reset()
{
    setEnabled(false);
    setModel(QString::fromLatin1(kDefaultSmartAnnotationModel));
    setModelPath(QString::fromLatin1(kDefaultSmartAnnotationPath));
    setModelBackend(QStringLiteral("tensorrt"));
    setModelDevice(QStringLiteral("gpu"));
    setMaskThreshold(0.0);
    setPolygonSimplifyEpsilon(2.0);
    setMaskAlpha(0.35);
    setRefreshInterval(80);
}

AdvancedSettings::AdvancedSettings(QObject *parent)
    : QObject(parent)
    , image_search_(new ImageSearchSettings(this))
    , roi_search_(new RoiSearchSettings(this))
    , smart_annotation_(new SmartAnnotationSettings(this))
{
}

AdvancedSettings::~AdvancedSettings() = default;

void AdvancedSettings::load(database::SettingsDataBase *database)
{
    if (!database)
    {
        return;
    }

    {
        QString err_msg;
        const auto row = database->loadFeatureSearchSettings(err_msg);
        if (!err_msg.isEmpty())
        {
            spdlog::warn("Load feature search settings failed: {}", err_msg.toUtf8().constData());
        }
        image_search_->load(row);
    }

    {
        QString err_msg;
        const auto row = database->loadRoiSearchSettings(err_msg);
        if (!err_msg.isEmpty())
        {
            spdlog::warn("Load ROI search settings failed: {}", err_msg.toUtf8().constData());
        }
        roi_search_->load(row);
    }

    {
        QString err_msg;
        auto    row = database->loadSmartAnnotationSettings(err_msg);
        if (!err_msg.isEmpty())
        {
            spdlog::warn("Load smart annotation settings failed: {}", err_msg.toUtf8().constData());
        }
        if (row.isEmpty())
        {
            QString legacy_err_msg;
            row = database->loadFeatureSearchSettings(legacy_err_msg);
            if (!legacy_err_msg.isEmpty())
            {
                spdlog::warn("Load legacy smart annotation settings failed: {}",
                             legacy_err_msg.toUtf8().constData());
            }
        }
        smart_annotation_->load(row);
    }
}

void AdvancedSettings::save(database::SettingsDataBase *database)
{
    if (!database)
    {
        return;
    }

    {
        QString err_msg;
        database->saveFeatureSearchSettings(image_search_->saveMap(), err_msg);
        if (!err_msg.isEmpty())
        {
            spdlog::error("Save feature search settings failed: {}", err_msg.toUtf8().constData());
        }
    }

    {
        QString err_msg;
        database->saveRoiSearchSettings(roi_search_->saveMap(), err_msg);
        if (!err_msg.isEmpty())
        {
            spdlog::error("Save ROI search settings failed: {}", err_msg.toUtf8().constData());
        }
    }

    {
        QString err_msg;
        database->saveSmartAnnotationSettings(smart_annotation_->saveMap(), err_msg);
        if (!err_msg.isEmpty())
        {
            spdlog::error("Save smart annotation settings failed: {}", err_msg.toUtf8().constData());
        }
    }
}

void AdvancedSettings::reset()
{
    image_search_->reset();
    roi_search_->reset();
    smart_annotation_->reset();
}

} // namespace dltool::settings
