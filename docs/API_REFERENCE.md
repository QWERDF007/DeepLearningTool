# DeepLearningTool API 参考

本文档记录当前源码中面向模块间调用和 QML 使用的主要接口。更细的函数签名以对应头文件为准。

## 1. Common

头文件：`src/common/include/common/`

### Logger

```cpp
namespace dltool::common {

COMMON_API std::vector<spdlog::sink_ptr> defaultSinks();
COMMON_API std::shared_ptr<spdlog::logger> setupLogger(
    const std::vector<spdlog::sink_ptr> &sinks);

}
```

`src/tool/main.cpp` 会创建默认 sinks，追加 `UILogger` 对应的 Qt sink，并注册到 project/data 模块。

### Singleton

```cpp
namespace dltool::common {

template<typename T>
class Singleton {
public:
    static T *getInstance();
    static T *getInstanceCPP();
};

}
```

宏：

```cpp
SINGLETON(ClassName)
QT_QML_SINGLETON(ClassName)
```

### CrashHandler 与 Utils

```cpp
namespace dltool::common {

class CrashHandler {
public:
    void setup(std::function<void()> crash_callback = nullptr);
};

QString uuid();
QString toQString(const QStringList &, const QString &sep = ", ",
                  Qt::SplitBehavior behavior = Qt::KeepEmptyParts);
QString getDirectory(const QString &path);
std::vector<QString> getDirectories(const QString &path, bool recursive = false);
std::vector<QString> getFiles(const QString &path, const QStringList &name_filters,
                              bool recursive = false);

class FileReader : QObject {
public:
    inline static const QStringList ImageFilters{
        "*.jpg", "*.jpeg", "*.png", "*.bmp", "*.webp"
    };
    const QString read(const QString &root, bool recursive = false, bool circular = false);
};

}
```

## 2. Core (`dltool.core`)

头文件：`src/core/include/core/CoreDef.h`

### DeepLearningMethod

QML 单例：`DeepLearningMethod`

```cpp
enum Method {
    Classification = 0,
    Detection,
    Segmentation,
    Pose,
    OCR,
};

Q_INVOKABLE QList<QVariantMap> getMethods() const;
QList<int> getMethodTypes() const;
Q_INVOKABLE QString getMethodName(const int method) const;

static const QList<QVariantMap> &methodItems();
static const QList<int> &supportedMethodTypes();
static bool isSupportedMethod(const int method);
static QString methodName(const int method);
```

当前 `supportedMethodTypes()` 返回 `Classification`、`Detection` 和 `Segmentation`，项目有效性校验接受这三类。`Pose` 和 `OCR` 已在枚举和展示列表中保留，但当前不是已支持任务类型。

## 3. Database

头文件：`src/database/include/database/DataBase.h`

命名空间：`dltool::database`

### DataBase

```cpp
class DataBase : public QObject {
public:
    DataBase(const QString &path, QObject *parent = nullptr);
    virtual ~DataBase();

    sqlpp::sqlite3::connection_pool *connectionPool();
    QString path() const;

    static sqlpp::sqlite3::connection connect(const QString &path, const int flags);
    static QString applicationDatabasePath(const QString &fileName);
    bool checkIntegrity(QString &err_msg) const;
};
```

### ProjectDataBase

负责 `.dlpro` 项目数据库。主要能力：

- 项目元信息：`initProject()`、`openProject()`、`updateProject()`、`getProjectInfo()`、`getLabelInfo()`。
- 数据集：`getAllDatasets()`、`addDataset()`、`updateDataset()`、`deleteDataset()`。
- 图像：`addImages()`、`getImage()`、`getImages()`、`getAllImages()`、`deleteImages()`。
- 标签类别：`getAllLabelClasses()`、`addLabelClass()`、`updateLabelClass()`、`deleteLabelClass()`。
- 图像标签：`getAllTagClasses()`、`addTagClass()`、`updateTagClass()`、`deleteTagClass()`、`getAllTags()`、`addImagesTag()`、`deleteImagesTag()`。
- 模型记录：`getAllModels()`、`addModel()`、`updateModelName()`、`deleteModel()`。
- 标注实例：`getAllLabels()`、`addLabels()`、`updateLabelsData()`、`updateLabelsClass()`、`deleteLabels()`。

### RecentProjectsDataBase

保存最近项目列表，默认数据库位置为软件目录下的 `db/history.db`。

```cpp
class RecentProjectsDataBase : public DataBase {
public:
    bool addProject(const QString &path, QString &err_msg) const;
    bool removeProject(const QString &path, QString &err_msg) const;
    int getProjects(std::vector<QString> &paths, QString &err_msg) const;
};
```

### SettingsDataBase

保存全局设置，默认数据库位置为软件目录下的 `db/settings.db`。当前每个设置分类使用独立表，通过 `QVariantMap` 按分类整行加载和保存。

```cpp
class SettingsDataBase : public DataBase {
public:
    QVariantMap loadFeatureSearchSettings(QString &err_msg) const;
    bool saveFeatureSearchSettings(const QVariantMap &row, QString &err_msg) const;

    QVariantMap loadSmartAnnotationSettings(QString &err_msg) const;
    bool saveSmartAnnotationSettings(const QVariantMap &row, QString &err_msg) const;

    QVariantMap loadThumbnailSettings(QString &err_msg) const;
    bool saveThumbnailSettings(const QVariantMap &row, QString &err_msg) const;

    QVariantMap loadLabelDisplaySettings(QString &err_msg) const;
    bool saveLabelDisplaySettings(const QVariantMap &row, QString &err_msg) const;

    QVariantMap loadImageEnhanceSettings(QString &err_msg) const;
    bool saveImageEnhanceSettings(const QVariantMap &row, QString &err_msg) const;

    QVariantMap loadUiSettings(QString &err_msg) const;
    bool saveUiSettings(const QVariantMap &row, QString &err_msg) const;

    QVariantMap loadSettings(const QString &table_name, QString &err_msg) const;
    bool saveSettings(const QString &table_name, const QVariantMap &row, QString &err_msg) const;
};
```

## 4. Settings (`dltool.settings`)

头文件：`src/settings/include/settings/`

### GlobalSettings

QML 单例：`GlobalSettings`

```cpp
class GlobalSettings : public QObject {
public:
    Q_INVOKABLE void load();
    Q_INVOKABLE void save();
    Q_INVOKABLE QObject *settingsObject(const QString &accessor_path) const;
    Q_INVOKABLE QVariant value(const QString &accessor_path, const QString &property_name,
                               const QVariant &fallback = {}) const;
    Q_INVOKABLE bool setValue(const QString &accessor_path, const QString &property_name, const QVariant &value);
};
```

QML 示例：

```qml
import dltool.settings

Item {
    Component.onCompleted: {
        GlobalSettings.setValue("data", "thumbnailMargin", 12)
        GlobalSettings.setValue("advanced.imageSearch", "topK", 10)
    }
}
```

### SoftwareSetting

| 属性 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `maxRecentProjects` | `int` | `10` | 最近项目数量限制 |
| `autoSaveInterval` | `int` | `300` | 自动保存间隔，单位秒 |
| `autoSaveEnabled` | `bool` | `true` | 是否启用自动保存 |
| `pythonEnvPath` | `string` | `""` | Python 环境目录 |

### DataSettings

| 属性 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `thumbnailMargin` | `int` | `10` | 缩略图边距 |
| `thumbnailCacheSize` | `int` | `100` | 缩略图缓存大小 |
| `imageLoadThreads` | `int` | `4` | 图像加载线程数 |
| `labelBorderWidth` | `int` | `2` | 标注边框宽度 |
| `labelFillOpacity` | `int` | `30` | 标注填充透明度，0-100 |
| `imageCellScale` | `double` | `1.0` | 图像网格缩放 |
| `imageCellScaleFrom` | `double` | `0.5` | 图像网格缩放下限 |
| `imageCellScaleTo` | `double` | `4.0` | 图像网格缩放上限 |
| `imageCellScaleStepSize` | `double` | `0.25` | 图像网格缩放步长 |
| `labelThumbnailScale` | `double` | `1.0` | 标注缩略图缩放 |
| `labelThumbnailAspectRatio` | `double` | `1.0` | 标注缩略图宽高比 |
| `labelThumbnailBorderPadding` | `double` | `0.1` | 标注缩略图边界扩展 |

`imageCellScale` 已迁移到 `DataSettings`，不要再从 `UISettings` 读取。

### AdvancedSettings

`AdvancedSettings` 是 `GlobalSettings.advanced` 下的聚合对象。

```cpp
Q_PROPERTY(ImageSearchSettings *imageSearch READ imageSearch CONSTANT)
Q_PROPERTY(SmartAnnotationSettings *smartAnnotation READ smartAnnotation CONSTANT)
```

#### ImageSearchSettings

| 属性 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `enabled` | `bool` | `true` | 是否启用图像搜索相关默认配置 |
| `model` | `QString` | `"resnet18"` | 默认特征提取模型名称 |
| `modelPath` | `QString` | `"F:/models/resnet18.wts"` | 模型权重文件路径 |
| `featureName` | `QString` | `"layer4"` | 默认特征层名称 |
| `rebuildIndex` | `bool` | `false` | 是否强制重建 FAISS 索引 |
| `topK` | `int` | `5` | 每张查询图返回的最近邻数量 |
| `norm` | `QString` | `"l2"` | 特征归一化方式（none / l1 / l2） |
| `preprocessBackend` | `QString` | `"cpu"` | 预处理后端（cpu / gpu） |
| `faissBackend` | `QString` | `"cpu"` | FAISS 计算后端（cpu / gpu） |
| `indexStorage` | `QString` | `"ram"` | 索引存储方式（ram / disk） |
| `modelBatchSize` | `int` | `1` | 特征提取推理批大小 |
| `modelBackend` | `QString` | `"tensorrt"` | 模型推理后端 |
| `modelDevice` | `QString` | `"gpu"` | 模型运行设备 |
| `indexDirectory` | `QString` | `""` | 特征库索引目录 |

方法：

```cpp
Q_INVOKABLE QStringList customFeatureNames(const QString &model_name) const;
Q_INVOKABLE void addCustomFeatureName(const QString &model_name,
                                      const QString &feature_name);
```

#### SmartAnnotationSettings

| 属性 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `enabled` | `bool` | `false` | 是否启用智能标注默认配置 |
| `model` | `QString` | `"edge_sam"` | 默认智能标注模型 |
| `modelPath` | `QString` | `"F:/models/edge_sam.wts"` | 模型权重文件路径 |
| `modelBackend` | `QString` | `"tensorrt"` | 模型后端 |
| `modelDevice` | `QString` | `"gpu"` | 模型运行设备 |
| `maskThreshold` | `double` | `0.0` | mask 阈值 |
| `polygonSimplifyEpsilon` | `double` | `2.0` | 多边形简化参数 |
| `maskAlpha` | `double` | `0.35` | 预览 mask 透明度 |
| `refreshInterval` | `int` | `80` | 预览刷新间隔，毫秒 |

### UISettings

| 属性 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `imageBrightness` | `double` | `0.0` | 图像亮度 |
| `imageBrightnessFrom` | `double` | `-1.0` | 亮度下限 |
| `imageBrightnessTo` | `double` | `1.0` | 亮度上限 |
| `imageBrightnessStepSize` | `double` | `0.1` | 亮度步长 |
| `imageContrast` | `double` | `0.0` | 图像对比度 |
| `imageContrastFrom` | `double` | `-1.0` | 对比度下限 |
| `imageContrastTo` | `double` | `1.0` | 对比度上限 |
| `imageContrastStepSize` | `double` | `0.1` | 对比度步长 |
| `theme` | `QString` | `"dark"` | 主题 |
| `language` | `QString` | `"zh_CN"` | 语言 |

## 5. Data (`dltool.data`)

头文件：`src/data/include/data/`

### DataFormat

QML 单例：`DataFormat`

```cpp
static Q_INVOKABLE QList<QString> getSupportedDataFormat();       // ["LabelMe", "COCO"]
static Q_INVOKABLE QList<QString> getSupportedExportDataFormat(); // ["LabelMe", "COCO"]
static Q_INVOKABLE QList<QString> getSupportedImageFormat();      // jpg/jpeg/png/bmp/webp
static Q_INVOKABLE int getDataFormat(const QString &name);
static bool isDataFormatSupported(const int data_format);
```

当前导入支持 LabelMe、COCO；导出支持 LabelMe、COCO。目标检测使用 bbox 标注；语义分割使用多边形点集 `points`，并保留 bbox 作为显示、筛选和缩略图裁剪的外接框。

### DataManager

QML 类型：`DataManager`，不可直接创建；通过 `Project.dataManager` 获取。

```cpp
Q_PROPERTY(DatasetsListModel *datasets READ datasets CONSTANT)
Q_PROPERTY(ImageInstancesListModel *imageInstances READ imageInstances CONSTANT)
Q_PROPERTY(LabelClassesListModel *labelClasses READ labelClasses CONSTANT)
Q_PROPERTY(ImageTagsListModel *imageTags READ imageTags CONSTANT)
Q_PROPERTY(LabelInstancesListModel *labelInstances READ labelInstances CONSTANT)
Q_PROPERTY(ImageLabelsListModel *imageLabelsList READ imageLabelsList CONSTANT)
Q_PROPERTY(ImageLabelsTableModel *imageLabelsTable READ imageLabelsTable CONSTANT)
Q_PROPERTY(ImageInfoListModel *imageInfo READ imageInfo CONSTANT)
Q_PROPERTY(GlobalFilter *globalFilter READ globalFilter CONSTANT)
Q_PROPERTY(ImageSearchController *imageSearch READ imageSearch CONSTANT)
Q_PROPERTY(SmartAnnotationController *smartAnnotation READ smartAnnotation CONSTANT)
Q_PROPERTY(DatasetFilterItemsModel *datasetFilterItems READ datasetFilterItems CONSTANT)
Q_PROPERTY(TagFilterItemsModel *tagFilterItems READ tagFilterItems CONSTANT)
Q_PROPERTY(LabelClassFilterItemsModel *labelClassFilterItems READ labelClassFilterItems CONSTANT)
Q_PROPERTY(CategoryStatisticsModel *categoryStatisticsModel READ categoryStatisticsModel CONSTANT)
Q_PROPERTY(int method READ method CONSTANT)
```

主要方法：

```cpp
Q_INVOKABLE QList<QString> getAllDatasetsName() const;
Q_INVOKABLE int getDatasetId(const QString &dataset_name) const;
Q_INVOKABLE QString getDatasetName(const int dataset_id) const;

Q_INVOKABLE void addDataset(const QString &name);
Q_INVOKABLE void updateDataset(const int64_t dataset_id, const QString &name);
Q_INVOKABLE void deleteDataset(const int64_t dataset_id);
Q_INVOKABLE void importData(const int64_t dataset_id, const int data_format,
                            const QString &image_dir, const QString &data_dir);
Q_INVOKABLE void exportDataset(const int64_t dataset_id, const int data_format,
                               const QString &output_dir);
Q_INVOKABLE void deleteSelectedImages();

Q_INVOKABLE void addLabelClass(const QString &name, const QString &color,
                               const QString &shortcut);
Q_INVOKABLE void updateLabelClass(const int64_t label_class_id,
                                  const QString &name, const QString &color,
                                  const QString &shortcut,
                                  const int64_t ordinal_index);
Q_INVOKABLE void deleteLabelClass(const int64_t label_class_id);

Q_INVOKABLE void addLabels(const std::vector<int64_t> &image_ids,
                           const std::vector<int64_t> &label_class_ids,
                           const std::vector<QVariantMap> &data);
Q_INVOKABLE bool addLabel(const int64_t image_id, const int64_t label_class_id,
                          const QVariantMap &data);
Q_INVOKABLE void updateLabels(const std::vector<int64_t> &label_ids,
                              const std::vector<QVariantMap> &data);
Q_INVOKABLE void updateLabelsClass(const std::vector<int64_t> &label_ids,
                                   const std::vector<int64_t> &label_class_ids);
Q_INVOKABLE void deleteLabels(const std::vector<int64_t> &label_ids);
Q_INVOKABLE void duplicateSelectedLabels();

Q_INVOKABLE void addTagClass(const QString &name);
Q_INVOKABLE QString getImageName(const int64_t image_id) const;
Q_INVOKABLE QString getImagePath(const int64_t image_id) const;
Q_INVOKABLE QString getImageDatasetName(const int64_t image_id) const;
Q_INVOKABLE QString getImageTagName(const int64_t image_id) const;
```

### 常用模型 Role

| 模型 | QML 名称 | 主要 role |
|------|----------|-----------|
| `DatasetsListModel` | `DatasetsModel` | `dataset_id`、`name`、`stats`、`progress` |
| `ImageInstancesListModel` | `ImageInstancesModel` | `image_id`、`name`、`path`、`selected`、`isCurrent`、`hasLabels` |
| `ImageInfoListModel` | `ImageInfoModel` | `title`、`value` |
| `LabelClassesListModel` | `LabelClassesModel` | `label_class_id`、`name`、`color`、`shortcut`、`ordinal_index`、`selected` |
| `ImageTagsListModel` | `ImageTagsModel` | `tag_id`、`name`、`selected_images_stats`、`current_image_stats` |
| `LabelInstancesListModel` | `LabelInstancesModel` | `label_id`、`image_id`、`label_class_id`、`label_class_name`、`label_class_color`、`data`、`selected` |
| `ImageLabelsListModel` | `ImageLabelsListModel` | `label_id`、`image_id`、`label_class_id`、`data`、`color`、`selected`、`hovered` |
| `ImageLabelsTableModel` | `ImageLabelsTableModel` | `title`、`data`、`class_data`、`selected` |
| `FilterItemsModel` | `FilterItemsModel` | `id`、`text`、`checked` |
| `CategoryStatisticsModel` | `CategoryStatisticsModel` | `categoryId`、`categoryName`、`categoryColor`、`instanceCount`、`imageCount`、`instancePercentage`、`imagePercentage` |

标注 `data` 的公共字段为 `x`、`y`、`width`、`height`。语义分割标注额外包含 `point_count` 和 `points`，其中 `points` 是 `{x, y}` 点列表。

### GlobalFilter

QML 类型：`GlobalFilter`，通过 `DataManager.globalFilter` 获取。

```cpp
enum class FilterType {
    Dataset,
    Tag,
    LabelClass,
    ImageLabelClass,
    ImageSearch
};

Q_PROPERTY(bool isActive READ isActive NOTIFY filterStateChanged)
Q_PROPERTY(int activeFilterCount READ activeFilterCount NOTIFY filterStateChanged)
Q_PROPERTY(QString filterSummary READ filterSummary NOTIFY filterStateChanged)
Q_PROPERTY(bool hasImageSearchResults READ hasImageSearchResults NOTIFY filterStateChanged)
Q_PROPERTY(bool imageSearchFilterEnabled READ imageSearchFilterEnabled NOTIFY filterStateChanged)
Q_PROPERTY(int imageSearchResultCount READ imageSearchResultCount NOTIFY filterStateChanged)

Q_INVOKABLE void setFilter(FilterType type, const std::vector<int64_t> &ids);
Q_INVOKABLE void setFilterEnabled(FilterType type, bool enabled);
Q_INVOKABLE void clearFilter(FilterType type);
Q_INVOKABLE void selectAll(FilterType type);
Q_INVOKABLE void deselectAll(FilterType type);
Q_INVOKABLE std::vector<int64_t> getActiveIds(FilterType type) const;
Q_INVOKABLE void clearAllFilters();
Q_INVOKABLE void setImageSearchFilterEnabled(bool enabled);
Q_INVOKABLE void clearImageSearchResults();
```

### ImageSearchController

QML 类型：`ImageSearchController`，通过 `DataManager.imageSearch` 获取。

```cpp
Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
Q_PROPERTY(bool hasResults READ hasResults NOTIFY resultsChanged)
Q_PROPERTY(int resultCount READ resultCount NOTIFY resultsChanged)
Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
Q_PROPERTY(QString lastSummary READ lastSummary NOTIFY resultsChanged)
Q_PROPERTY(QString defaultInferRtRoot READ defaultInferRtRoot CONSTANT)
Q_PROPERTY(QString defaultModelName READ defaultModelName CONSTANT)
Q_PROPERTY(QString defaultFeatureName READ defaultFeatureName CONSTANT)

Q_INVOKABLE QStringList supportedModelPresets() const;
Q_INVOKABLE QStringList modelFeatureNames(const QString &model_name) const;
Q_INVOKABLE QString suggestedWeightsPath(const QString &model_name) const;
Q_INVOKABLE bool searchSelectedImages(const QVariantList &dataset_ids,
                                      const QString &model_name,
                                      const QString &weights_file,
                                      const QString &feature_name,
                                      bool rebuild_index,
                                      int top_k,
                                      const QString &norm,
                                      const QString &preprocess_backend,
                                      const QString &faiss_backend,
                                      const QString &index_storage,
                                      int model_batch_size,
                                      const QString &model_backend,
                                      const QString &model_device);
```

信号：

- `runningChanged()`：搜索运行状态变化。
- `resultsChanged()`：搜索结果或命中数量变化。
- `lastErrorChanged()`：错误信息变化。
- `buildProgressChanged(int processedCount, int totalCount)`：特征库构建进度。

### SmartAnnotationController

QML 类型：`SmartAnnotationController`，通过 `DataManager.smartAnnotation` 获取。

```cpp
Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
Q_PROPERTY(bool loadingModel READ isLoadingModel NOTIFY loadingModelChanged)
Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

Q_INVOKABLE QStringList supportedModelPresets() const;
Q_INVOKABLE QString suggestedModelPath(const QString &model_name,
                                       const QString &backend) const;
Q_INVOKABLE QVariantMap infer(const QString &image_path,
                              const QVariantList &prompt_points);
Q_INVOKABLE void clearCache();
```

信号：

- `runningChanged()`：推理运行状态变化。
- `loadingModelChanged()`：模型加载状态变化。
- `lastErrorChanged()`：错误信息变化。
- `modelLoadFinished(bool success)`：模型异步加载完成。

## 6. Model (`dltool.model`)

头文件：`src/model/include/model/`

### ModelManager

QML 类型：`ModelManager`，不可直接创建；通过 `Project.modelManager` 获取。

```cpp
Q_PROPERTY(int method READ method CONSTANT)

Q_INVOKABLE bool addModel(const QString &name, const QString &network_structure);
Q_INVOKABLE bool renameModel(const qint64 model_id, const QString &name);
Q_INVOKABLE bool deleteModel(const qint64 model_id);
Q_INVOKABLE bool copyModel(const qint64 model_id);

Q_INVOKABLE QStringList supportedNetworkStructures() const;
Q_INVOKABLE QStringList availableModelNames() const;
Q_INVOKABLE QVariantMap modelAt(int row) const;
Q_INVOKABLE dltool::model::IModel *modelForId(const qint64 model_id,
                                              const QString &network_structure) const;
```

Role：

| role | 说明 |
|------|------|
| `model_id` | 模型记录 ID |
| `name` | 模型名称 |
| `network_structure` | 网络结构名称，例如 YOLOv5 |
| `training_result` | 训练结果记录 |
| `test_result` | 测试结果记录 |
| `ctime` | 创建时间 |
| `mtime` | 修改时间 |

### IModel 与 IModelConfig

```cpp
class IModel : public QObject {
    Q_PROPERTY(int method READ method CONSTANT)
    Q_PROPERTY(QString typeName READ typeName CONSTANT)
    Q_PROPERTY(dltool::model::IModelConfig *config READ config CONSTANT)
};

class IModelConfig : public QObject {
    Q_PROPERTY(int method READ method CONSTANT)
    Q_PROPERTY(QString typeName READ typeName CONSTANT)
    Q_PROPERTY(dltool::model::ITrainParams *trainParams READ trainParams CONSTANT)
    Q_PROPERTY(dltool::model::ITestParams *testParams READ testParams CONSTANT)
};
```

### 参数模型

`IParams` 是参数分组列表模型，`ParamGroupModel` 是具体参数列表模型。

`IParams` role：

| role | 说明 |
|------|------|
| `nameEn` | 分组持久化名称 |
| `nameCn` | 分组显示名称 |
| `description` | 分组说明 |
| `enabled` | 分组是否启用 |
| `partIndex` | UI 分栏索引 |
| `count` | 分组内参数数量 |
| `groupModel` | `ParamGroupModel*` |

`ParamGroupModel` role：

| role | 说明 |
|------|------|
| `nameEn` | 参数持久化名称 |
| `nameCn` | 参数显示名称 |
| `description` | 参数说明 |
| `value` | 当前值 |
| `defaultValue` | 默认值 |
| `valueType` | 值类型：`bool`、`int`、`double`、`string` |
| `valueRange` | 数值范围，格式为 `[from, to, step]` |
| `controlType` | 控件类型：`text`、`spin`、`slider`、`checkbox`、`combo` |
| `enabled` | 是否可编辑 |
| `options` | 下拉选项 |
| `unit` | 单位 |

主要方法：

```cpp
Q_INVOKABLE ParamGroupModel *groupAt(int row) const;
Q_INVOKABLE bool setValue(int row, const QVariant &value);
Q_INVOKABLE QVariant valueAt(int row) const;
Q_INVOKABLE QVariant valueForName(const QString &name_en) const;
```

当前 `DetectionModels.cpp` 注册了目标检测任务下的 YOLOv5 和 YOLOv8 默认模型配置。

## 7. Project (`dltool.project`)

头文件：`src/project/include/project/Projects.h`

### Project

QML 类型：`Project`，不可直接创建；通过 `ProjectManager.currentProject` 获取。

```cpp
Q_PROPERTY(QString name READ name CONSTANT)
Q_PROPERTY(int method READ method CONSTANT)
Q_PROPERTY(QString path READ path CONSTANT)
Q_PROPERTY(QString description READ description NOTIFY descriptionChanged)
Q_PROPERTY(QString imageBasePath READ imageBasePath NOTIFY imageBasePathChanged)
Q_PROPERTY(data::DataManager *dataManager READ dataManager CONSTANT)
Q_PROPERTY(model::ModelManager *modelManager READ modelManager CONSTANT)

void initProject();
void openProject();
static std::tuple<bool, QString> isValid(const int method, const QString &path,
                                         bool is_new);
```

### ProjectManager

QML 单例：`ProjectManager`

```cpp
Q_PROPERTY(Project *currentProject READ currentProject NOTIFY currentProjectChanged)
Q_PROPERTY(RectentProjects *recentProjects READ recentProjects CONSTANT)

Q_INVOKABLE Project *createProject(const QString &name, const int method,
                                   const QString &path, const QString &desc,
                                   const QString image_base_path);
Q_INVOKABLE Project *openProject(const QString &path);
Q_INVOKABLE void closeProject();
Q_INVOKABLE bool updateProjectBaseInfo(const QString &path, const QString &name,
                                       const QString &description);
Q_INVOKABLE void deleteProject(const QString &path);
Q_INVOKABLE void removeFromRectentProjects(const QString &path);
Q_INVOKABLE QString isProjectValid(const int method, const QString &path,
                                   bool is_new);
Q_INVOKABLE QString projectSuffix() const;      // ".dlpro"
Q_INVOKABLE QString projectFileFilter() const;  // "Project files (*.dlpro)"
Q_INVOKABLE QVariantMap getProjectInfo(const QString &path);
Q_INVOKABLE QVariantMap getLabelInfo(const QString &path);

signals:
    void currentProjectChanged();
    void projectActivated();
```

`projectActivated()` 在项目创建、成功打开，以及再次激活当前已打开项目时触发；主界面用它切换到数据工作区。

### RectentProjects

`RectentProjects` 是最近项目列表模型，类名沿用当前代码拼写。

属性：

- `selection`：`QItemSelectionModel*`
- `currentProjectPath`：当前选中的项目路径

Role：

| role | 说明 |
|------|------|
| `name` | 项目名称 |
| `path` | 项目路径 |
| `tooltip` | 项目名、路径和修改时间 |
| `selected` | 是否选中 |

## 8. UI (`dltool.ui`)

头文件：`src/ui/include/ui/`

### DltColor、DltFont 与图标

QML 单例：

- `DltColor`：`Transparent`、`Black`、`Background`、`Primary`、`Border`、`ScrollBar`、`ToolTip`、`Hovered`、`Highlight`、`FontPrimary`、`FontDark`、`TabButton`、`Button` 等颜色。
- `DltFont`：`Caption`、`Body`、`BodyStrong`、`Subtitle`、`Title`、`TitleLarge`、`Display`。
- `DltFontIcon`：图标字体枚举，定义在 `IconsFont.h`。

### UILogger

QML 单例：`UILogger`

```cpp
Q_PROPERTY(QString message READ getColorfulMessage NOTIFY messageChanged)
Q_PROPERTY(int infoCount READ getInfoCount NOTIFY countChanged)
Q_PROPERTY(int errorCount READ getErrorCount NOTIFY countChanged)

Q_INVOKABLE void log(const int level, const QString &message);
Q_INVOKABLE int getInfoCount() const;
Q_INVOKABLE int getErrorCount() const;
Q_INVOKABLE void clearCount();
```

### ProgressManager

QML 单例：`ProgressManager`

```cpp
Q_PROPERTY(int progress READ getProgress NOTIFY progressChanged)
Q_PROPERTY(bool isRunning READ getIsRunning NOTIFY runningStateChanged)
Q_PROPERTY(QString message READ getColorfulMessage NOTIFY messageChanged)

Q_INVOKABLE void startTask(const QString &taskName = "");
Q_INVOKABLE void updateProgress(int progress);
Q_INVOKABLE void addMessage(int level, const QString &message);
Q_INVOKABLE void completeTask();
Q_INVOKABLE void reset();
```

### Utils 与 SignalHelper

QML 单例：`Utils`

```cpp
Q_INVOKABLE QColor withOpacity(const QColor &color, qreal opacity) const;
Q_INVOKABLE QString getCleanPath(const QString &path) const;
Q_INVOKABLE void openInFileExplorer(const QString &path);
```

`SignalHelper` 是 QML 单例，提供标签列表/表格选择同步、Tab 切换、Review 到 Label 跳转等跨组件信号。

### QML 控件

`src/ui/controls/` 当前包含：

```text
DltButton.qml
DltCheckBox.qml
DltComboBox.qml
DltContentDialog.qml
DltControlBackground.qml
DltEditor.qml
DltExpander.qml
DltFilledButton.qml
DltFocusRectangle.qml
DltInfoBadge.qml
DltItemDelegate.qml
DltLoader.qml
DltMenu.qml
DltMenuItem.qml
DltPage.qml
DltPopup.qml
DltProgressBar.qml
DltScrollBar.qml
DltScrollablePage.qml
DltShadow.qml
DltSlider.qml
DltSpinEditor.qml
DltSplitView.qml
DltTabButton.qml
DltText.qml
DltTextArea.qml
DltTextBoxBackground.qml
DltTextField.qml
DltTextIcon.qml
DltTextIconButton.qml
DltTextInput.qml
DltToggleSwitch.qml
DltToolTip.qml
```

QML 示例：

```qml
import dltool.ui

DltFilledButton {
    text: "导入"
}

DltProgressBar {
    value: ProgressManager.progress / 100
}
```

## 9. Tool (`dltool.tool`)

`tool` 模块主要面向应用装配，不暴露独立 C++ API。常用 QML 入口：

- `Main.qml`：主窗口，组合 Header、Content、Footer。
- `Content.qml`：StackLayout 页面容器，加载项目、图库、标注、复核、训练、测试页面。
- `header/SettingsDialog.qml`：全局设置弹窗。
- `footer/LogDialog.qml`、`footer/ProgressDialog.qml`：日志和进度详情。
