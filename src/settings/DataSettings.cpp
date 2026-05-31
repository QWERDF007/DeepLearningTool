#include "settings/DataSettings.h"

#include "database/DataBase.h"

#include <spdlog/spdlog.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace dltool::settings {

namespace {

constexpr const char *kDefaultFeatureExtractionModel       = "resnet18";
constexpr const char *kDefaultFeatureExtractionModelPath   = "F:/models/resnet18.wts";
constexpr const char *kDefaultFeatureExtractionFeatureName = "layer4";

QString normalizedFeatureModel(QString value)
{
    value = value.trimmed();
    return value.isEmpty() ? QString::fromLatin1(kDefaultFeatureExtractionModel) : value;
}

QString normalizedFeatureName(QString value)
{
    value = value.trimmed();
    return value.isEmpty() ? QString::fromLatin1(kDefaultFeatureExtractionFeatureName) : value;
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

} // namespace

DataSettings::DataSettings(QObject *parent)
    : QObject(parent)
{
}

DataSettings::~DataSettings() {}

void DataSettings::setThumbnailMargin(int value)
{
    // 验证：边距必须大于等于 0
    value = std::max(0, value);

    if (thumbnail_margin_ != value)
    {
        thumbnail_margin_ = value;
        emit thumbnailMarginChanged();
    }
}

void DataSettings::setThumbnailCacheSize(int value)
{
    // 验证：缓存大小必须大于 0
    value = std::max(1, value);

    if (thumbnail_cache_size_ != value)
    {
        thumbnail_cache_size_ = value;
        emit thumbnailCacheSizeChanged();
    }
}

void DataSettings::setImageLoadThreads(int value)
{
    // 验证：线程数必须在 1-16 之间
    value = std::clamp(value, 1, 16);

    if (image_load_threads_ != value)
    {
        image_load_threads_ = value;
        emit imageLoadThreadsChanged();
    }
}

void DataSettings::setLabelBorderWidth(int value)
{
    // 验证：边框宽度必须大于 0
    value = std::max(1, value);

    if (label_border_width_ != value)
    {
        label_border_width_ = value;
        emit labelBorderWidthChanged();
    }
}

void DataSettings::setLabelFillOpacity(int value)
{
    // 验证：透明度必须在 0-100 之间
    value = std::clamp(value, 0, 100);

    if (label_fill_opacity_ != value)
    {
        label_fill_opacity_ = value;
        emit labelFillOpacityChanged();
    }
}

void DataSettings::setImageCellScale(double value)
{
    // 验证：缩放值必须在合理范围内
    value = std::clamp(value, 0.1, 10.0);

    if (image_cell_scale_ != value)
    {
        image_cell_scale_ = value;
        emit imageCellScaleChanged();
    }
}

void DataSettings::setImageCellScaleFrom(double value)
{
    // 验证：最小缩放值必须大于 0
    value = std::max(0.1, value);

    if (image_cell_scale_from_ != value)
    {
        image_cell_scale_from_ = value;
        emit imageCellScaleFromChanged();
    }
}

void DataSettings::setImageCellScaleTo(double value)
{
    // 验证：最大缩放值必须大于最小值
    value = std::max(image_cell_scale_from_ + 0.1, value);

    if (image_cell_scale_to_ != value)
    {
        image_cell_scale_to_ = value;
        emit imageCellScaleToChanged();
    }
}

void DataSettings::setImageCellScaleStepSize(double value)
{
    // 验证：步长必须大于 0
    value = std::max(0.01, value);

    if (image_cell_scale_step_size_ != value)
    {
        image_cell_scale_step_size_ = value;
        emit imageCellScaleStepSizeChanged();
    }
}

void DataSettings::setLabelThumbnailScale(double value)
{
    // 验证：缩放值必须在 0.5-4.0 范围内
    value = std::clamp(value, 0.5, 4.0);

    if (label_thumbnail_scale_ != value)
    {
        label_thumbnail_scale_ = value;
        emit labelThumbnailScaleChanged();
    }
}

void DataSettings::setLabelThumbnailAspectRatio(double value)
{
    // 验证：长宽比必须在 0.5-2.0 范围内
    value = std::clamp(value, 0.5, 2.0);

    if (label_thumbnail_aspect_ratio_ != value)
    {
        label_thumbnail_aspect_ratio_ = value;
        emit labelThumbnailAspectRatioChanged();
    }
}

void DataSettings::setLabelThumbnailBorderPadding(double value)
{
    // 验证：边界必须在 0.0-1.0 范围内
    value = std::clamp(value, 0.0, 1.0);

    if (label_thumbnail_border_padding_ != value)
    {
        label_thumbnail_border_padding_ = value;
        emit labelThumbnailBorderPaddingChanged();
    }
}

void DataSettings::setFeatureExtractionEnabled(bool value)
{
    if (feature_extraction_enabled_ != value)
    {
        feature_extraction_enabled_ = value;
        emit featureExtractionEnabledChanged();
    }
}

void DataSettings::setFeatureExtractionModel(const QString &value)
{
    const QString model = normalizedFeatureModel(value);
    if (feature_extraction_model_ != model)
    {
        feature_extraction_model_ = model;
        emit featureExtractionModelChanged();
    }
}

void DataSettings::setFeatureExtractionModelPath(const QString &value)
{
    const QString path = value.trimmed();
    if (feature_extraction_model_path_ != path)
    {
        feature_extraction_model_path_ = path;
        emit featureExtractionModelPathChanged();
    }
}

void DataSettings::setFeatureExtractionFeatureName(const QString &value)
{
    const QString feature_name = normalizedFeatureName(value);
    if (feature_extraction_feature_name_ != feature_name)
    {
        feature_extraction_feature_name_ = feature_name;
        emit featureExtractionFeatureNameChanged();
    }
}

void DataSettings::setFeatureExtractionRebuildIndex(bool value)
{
    if (feature_extraction_rebuild_index_ != value)
    {
        feature_extraction_rebuild_index_ = value;
        emit featureExtractionRebuildIndexChanged();
    }
}

void DataSettings::setFeatureExtractionTopK(int value)
{
    value = std::clamp(value, 1, 1000);
    if (feature_extraction_top_k_ != value)
    {
        feature_extraction_top_k_ = value;
        emit featureExtractionTopKChanged();
    }
}

void DataSettings::setFeatureExtractionNorm(const QString &value)
{
    const QString norm = normalizedOption(value, {QStringLiteral("l2"), QStringLiteral("l1"), QStringLiteral("none")},
                                          QStringLiteral("l2"));
    if (feature_extraction_norm_ != norm)
    {
        feature_extraction_norm_ = norm;
        emit featureExtractionNormChanged();
    }
}

void DataSettings::setFeatureExtractionPreprocessBackend(const QString &value)
{
    const QString backend
        = normalizedOption(value, {QStringLiteral("cpu"), QStringLiteral("gpu")}, QStringLiteral("cpu"));
    if (feature_extraction_preprocess_backend_ != backend)
    {
        feature_extraction_preprocess_backend_ = backend;
        emit featureExtractionPreprocessBackendChanged();
    }
}

void DataSettings::setFeatureExtractionFaissBackend(const QString &value)
{
    const QString backend
        = normalizedOption(value, {QStringLiteral("cpu"), QStringLiteral("gpu")}, QStringLiteral("cpu"));
    if (feature_extraction_faiss_backend_ != backend)
    {
        feature_extraction_faiss_backend_ = backend;
        emit featureExtractionFaissBackendChanged();
    }
    if (backend == QStringLiteral("gpu"))
    {
        setFeatureExtractionIndexStorage(QStringLiteral("ram"));
    }
}

void DataSettings::setFeatureExtractionIndexStorage(const QString &value)
{
    QString storage = normalizedOption(value, {QStringLiteral("ram"), QStringLiteral("disk")}, QStringLiteral("ram"));
    if (feature_extraction_faiss_backend_ == QStringLiteral("gpu"))
    {
        storage = QStringLiteral("ram");
    }

    if (feature_extraction_index_storage_ != storage)
    {
        feature_extraction_index_storage_ = storage;
        emit featureExtractionIndexStorageChanged();
    }
}

void DataSettings::setFeatureExtractionDiskBuildBatchSize(int value)
{
    value = std::clamp(value, 1, 8192);
    if (feature_extraction_disk_build_batch_size_ != value)
    {
        feature_extraction_disk_build_batch_size_ = value;
        emit featureExtractionDiskBuildBatchSizeChanged();
    }
}

void DataSettings::setFeatureExtractionModelBackend(const QString &value)
{
    const QString backend = normalizedOption(value,
                                             {QStringLiteral("tensorrt"), QStringLiteral("openvino"),
                                              QStringLiteral("onnxruntime")},
                                             QStringLiteral("tensorrt"));
    if (feature_extraction_model_backend_ != backend)
    {
        feature_extraction_model_backend_ = backend;
        emit featureExtractionModelBackendChanged();
    }
}

void DataSettings::setFeatureExtractionModelDevice(const QString &value)
{
    const QString device
        = normalizedOption(value, {QStringLiteral("cpu"), QStringLiteral("gpu")}, QStringLiteral("gpu"));
    if (feature_extraction_model_device_ != device)
    {
        feature_extraction_model_device_ = device;
        emit featureExtractionModelDeviceChanged();
    }
}

void DataSettings::setFeatureExtractionIndexDirectory(const QString &value)
{
    const QString path = value.trimmed();
    if (feature_extraction_index_directory_ != path)
    {
        feature_extraction_index_directory_ = path;
        emit featureExtractionIndexDirectoryChanged();
    }
}

QStringList DataSettings::featureExtractionCustomFeatureNames(const QString &model_name) const
{
    return feature_extraction_custom_feature_names_.value(normalizedFeatureModel(model_name));
}

void DataSettings::addFeatureExtractionCustomFeatureName(const QString &model_name, const QString &feature_name)
{
    const QString model   = normalizedFeatureModel(model_name);
    const QString feature = feature_name.trimmed();
    if (feature.isEmpty())
    {
        return;
    }

    QStringList values = feature_extraction_custom_feature_names_.value(model);
    if (values.contains(feature))
    {
        return;
    }

    values.append(feature);
    feature_extraction_custom_feature_names_.insert(model, values);
    emit featureExtractionCustomFeatureNamesChanged();
}

QString DataSettings::featureExtractionCustomFeatureNamesJson() const
{
    QJsonObject root;
    const auto  keys = feature_extraction_custom_feature_names_.keys();
    for (const QString &model : keys)
    {
        QJsonArray names;
        for (const QString &feature_name : feature_extraction_custom_feature_names_.value(model))
        {
            names.append(feature_name);
        }
        root.insert(model, names);
    }
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void DataSettings::setFeatureExtractionCustomFeatureNamesJson(const QString &value)
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
                parsed_names.insert(normalizedFeatureModel(it.key()), names);
            }
        }
    }

    if (feature_extraction_custom_feature_names_ != parsed_names)
    {
        feature_extraction_custom_feature_names_ = parsed_names;
        emit featureExtractionCustomFeatureNamesChanged();
    }
}

void DataSettings::load(database::SettingsDataBase *database)
{
    if (!database)
        return;

    // 缩略图设置
    {
        QString err_msg;
        const auto row = database->loadThumbnailSettings(err_msg);
        if (!err_msg.isEmpty())
        {
            spdlog::warn("Load thumbnail settings failed: {}", err_msg.toUtf8().constData());
        }
        setThumbnailMargin(row.value(QStringLiteral("margin"), 10).toInt());
        setThumbnailCacheSize(row.value(QStringLiteral("cache_size"), 100).toInt());
        setImageLoadThreads(row.value(QStringLiteral("image_load_threads"), 4).toInt());
        setImageCellScale(row.value(QStringLiteral("cell_scale"), 1.0).toDouble());
        setImageCellScaleFrom(row.value(QStringLiteral("cell_scale_from"), 0.5).toDouble());
        setImageCellScaleTo(row.value(QStringLiteral("cell_scale_to"), 4.0).toDouble());
        setImageCellScaleStepSize(row.value(QStringLiteral("cell_scale_step"), 0.25).toDouble());
        setLabelThumbnailScale(row.value(QStringLiteral("label_scale"), 1.0).toDouble());
        setLabelThumbnailAspectRatio(row.value(QStringLiteral("label_aspect_ratio"), 1.0).toDouble());
        setLabelThumbnailBorderPadding(row.value(QStringLiteral("label_border_padding"), 0.1).toDouble());
    }

    // 标注显示设置
    {
        QString err_msg;
        const auto row = database->loadLabelDisplaySettings(err_msg);
        if (!err_msg.isEmpty())
        {
            spdlog::warn("Load label display settings failed: {}", err_msg.toUtf8().constData());
        }
        setLabelBorderWidth(row.value(QStringLiteral("border_width"), 2).toInt());
        setLabelFillOpacity(row.value(QStringLiteral("fill_opacity"), 30).toInt());
    }

    // 特征搜索设置
    {
        QString err_msg;
        const auto row = database->loadFeatureSearchSettings(err_msg);
        if (!err_msg.isEmpty())
        {
            spdlog::warn("Load feature search settings failed: {}", err_msg.toUtf8().constData());
        }
        setFeatureExtractionEnabled(row.value(QStringLiteral("enabled"), true).toBool());
        setFeatureExtractionModel(
            row.value(QStringLiteral("model"), QString::fromLatin1(kDefaultFeatureExtractionModel)).toString());
        setFeatureExtractionModelPath(
            row.value(QStringLiteral("model_path"), QString::fromLatin1(kDefaultFeatureExtractionModelPath)).toString());
        setFeatureExtractionFeatureName(row.value(QStringLiteral("feature_name"),
                                                  QString::fromLatin1(kDefaultFeatureExtractionFeatureName))
                                            .toString());
        setFeatureExtractionRebuildIndex(row.value(QStringLiteral("rebuild_index"), false).toBool());
        setFeatureExtractionTopK(row.value(QStringLiteral("top_k"), 5).toInt());
        setFeatureExtractionNorm(row.value(QStringLiteral("norm"), QStringLiteral("l2")).toString());
        setFeatureExtractionPreprocessBackend(
            row.value(QStringLiteral("preprocess_backend"), QStringLiteral("cpu")).toString());
        setFeatureExtractionFaissBackend(row.value(QStringLiteral("faiss_backend"), QStringLiteral("cpu")).toString());
        setFeatureExtractionIndexStorage(row.value(QStringLiteral("index_storage"), QStringLiteral("ram")).toString());
        setFeatureExtractionDiskBuildBatchSize(row.value(QStringLiteral("disk_build_batch_size"), 256).toInt());
        setFeatureExtractionModelBackend(
            row.value(QStringLiteral("model_backend"), QStringLiteral("tensorrt")).toString());
        setFeatureExtractionModelDevice(row.value(QStringLiteral("model_device"), QStringLiteral("gpu")).toString());
        setFeatureExtractionIndexDirectory(row.value(QStringLiteral("index_directory"), QString()).toString());
        setFeatureExtractionCustomFeatureNamesJson(
            row.value(QStringLiteral("custom_feature_names"), QStringLiteral("{}")).toString());
    }
}

void DataSettings::save(database::SettingsDataBase *database)
{
    if (!database)
        return;

    // 缩略图设置
    {
        QString err_msg;
        database->saveThumbnailSettings(
            QVariantMap{
                {QStringLiteral("margin"), thumbnail_margin_},
                {QStringLiteral("cache_size"), thumbnail_cache_size_},
                {QStringLiteral("image_load_threads"), image_load_threads_},
                {QStringLiteral("cell_scale"), image_cell_scale_},
                {QStringLiteral("cell_scale_from"), image_cell_scale_from_},
                {QStringLiteral("cell_scale_to"), image_cell_scale_to_},
                {QStringLiteral("cell_scale_step"), image_cell_scale_step_size_},
                {QStringLiteral("label_scale"), label_thumbnail_scale_},
                {QStringLiteral("label_aspect_ratio"), label_thumbnail_aspect_ratio_},
                {QStringLiteral("label_border_padding"), label_thumbnail_border_padding_},
            },
            err_msg);
        if (!err_msg.isEmpty())
            spdlog::error("Save thumbnail settings failed: {}", err_msg.toUtf8().constData());
    }

    // 标注显示设置
    {
        QString err_msg;
        database->saveLabelDisplaySettings(
            QVariantMap{
                {QStringLiteral("border_width"), label_border_width_},
                {QStringLiteral("fill_opacity"), label_fill_opacity_},
            },
            err_msg);
        if (!err_msg.isEmpty())
            spdlog::error("Save label display settings failed: {}", err_msg.toUtf8().constData());
    }

    // 特征搜索设置
    {
        QString err_msg;
        database->saveFeatureSearchSettings(
            QVariantMap{
                {QStringLiteral("enabled"), feature_extraction_enabled_},
                {QStringLiteral("model"), feature_extraction_model_},
                {QStringLiteral("model_path"), feature_extraction_model_path_},
                {QStringLiteral("feature_name"), feature_extraction_feature_name_},
                {QStringLiteral("rebuild_index"), feature_extraction_rebuild_index_},
                {QStringLiteral("top_k"), feature_extraction_top_k_},
                {QStringLiteral("norm"), feature_extraction_norm_},
                {QStringLiteral("preprocess_backend"), feature_extraction_preprocess_backend_},
                {QStringLiteral("faiss_backend"), feature_extraction_faiss_backend_},
                {QStringLiteral("index_storage"), feature_extraction_index_storage_},
                {QStringLiteral("disk_build_batch_size"), feature_extraction_disk_build_batch_size_},
                {QStringLiteral("model_backend"), feature_extraction_model_backend_},
                {QStringLiteral("model_device"), feature_extraction_model_device_},
                {QStringLiteral("index_directory"), feature_extraction_index_directory_},
                {QStringLiteral("custom_feature_names"), featureExtractionCustomFeatureNamesJson()},
            },
            err_msg);
        if (!err_msg.isEmpty())
            spdlog::error("Save feature search settings failed: {}", err_msg.toUtf8().constData());
    }
}

void DataSettings::reset()
{
    setThumbnailMargin(10);
    setThumbnailCacheSize(100);
    setImageLoadThreads(4);
    setLabelBorderWidth(2);
    setLabelFillOpacity(30);

    setImageCellScale(1.0);
    setImageCellScaleFrom(0.5);
    setImageCellScaleTo(4.0);
    setImageCellScaleStepSize(0.25);

    setLabelThumbnailScale(1.0);
    setLabelThumbnailAspectRatio(1.0);
    setLabelThumbnailBorderPadding(0.1);

    setFeatureExtractionEnabled(true);
    setFeatureExtractionModel(QString::fromLatin1(kDefaultFeatureExtractionModel));
    setFeatureExtractionModelPath(QString::fromLatin1(kDefaultFeatureExtractionModelPath));
    setFeatureExtractionFeatureName(QString::fromLatin1(kDefaultFeatureExtractionFeatureName));
    setFeatureExtractionRebuildIndex(false);
    setFeatureExtractionTopK(5);
    setFeatureExtractionNorm(QStringLiteral("l2"));
    setFeatureExtractionPreprocessBackend(QStringLiteral("cpu"));
    setFeatureExtractionFaissBackend(QStringLiteral("cpu"));
    setFeatureExtractionIndexStorage(QStringLiteral("ram"));
    setFeatureExtractionDiskBuildBatchSize(256);
    setFeatureExtractionModelBackend(QStringLiteral("tensorrt"));
    setFeatureExtractionModelDevice(QStringLiteral("gpu"));
    setFeatureExtractionIndexDirectory(QString());
    setFeatureExtractionCustomFeatureNamesJson(QStringLiteral("{}"));
}

} // namespace dltool::settings
