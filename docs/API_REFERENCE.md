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
    void setup();
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

## 2. Settings (`dltool.settings`)

头文件：`src/settings/include/settings/`

### GlobalSettings

QML 单例：`GlobalSettings`

```cpp
namespace dltool::settings {

class GlobalSettings : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(GlobalSettings)
    QT_QML_SINGLETON(GlobalSettings)

    Q_PROPERTY(ProjectSettings *project READ project CONSTANT)
    Q_PROPERTY(DataSettings *data READ data CONSTANT)
    Q_PROPERTY(UISettings *ui READ ui CONSTANT)

public:
    Q_INVOKABLE void load();
    Q_INVOKABLE void save();
    Q_INVOKABLE void reset();
    Q_INVOKABLE void setAutoSaveEnabled(bool enabled);
    Q_INVOKABLE bool autoSaveEnabled() const;
};

}
```

QML 示例：

```qml
import dltool.settings

Item {
    Component.onCompleted: {
        GlobalSettings.data.thumbnailMargin = 12
        GlobalSettings.save()
    }
}
```

设置项持久化到软件目录下的 `db/settings.db`，表结构由 `src/database/include/database/ddl/create_settings.sql` 定义。

### ProjectSettings

| 属性 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `maxRecentProjects` | `int` | `10` | 最近项目数量限制 |
| `autoSaveInterval` | `int` | `300` | 自动保存间隔，单位秒 |
| `autoSaveEnabled` | `bool` | `true` | 是否启用自动保存 |

### DataSettings

| 属性 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `thumbnailMargin` | `int` | `10` | 缩略图边距 |
| `thumbnailCacheSize` | `int` | `100` | 缩略图缓存大小，MB |
| `imageLoadThreads` | `int` | `4` | 图像加载线程数 |
| `labelBorderWidth` | `int` | `2` | 标注边框宽度 |
| `labelFillOpacity` | `int` | `30` | 标注填充透明度，0-100 |
| `imageCellScale` | `double` | `1.0` | 图像网格缩放 |
| `imageCellScaleFrom` | `double` | `0.5` | 图像网格缩放下限 |
| `imageCellScaleTo` | `double` | `4.0` | 图像网格缩放上限 |
| `imageCellScaleStepSize` | `double` | `0.25` | 图像网格缩放步长 |
| `labelThumbnailScale` | `double` | `1.0` | 标注缩略图缩放 |
| `labelThumbnailScaleFrom` | `double` | `0.5` | 标注缩略图缩放下限 |
| `labelThumbnailScaleTo` | `double` | `4.0` | 标注缩略图缩放上限 |
| `labelThumbnailScaleStepSize` | `double` | `0.25` | 标注缩略图缩放步长 |
| `labelThumbnailAspectRatio` | `double` | `1.0` | 标注缩略图宽高比 |
| `labelThumbnailAspectRatioFrom` | `double` | `0.5` | 宽高比下限 |
| `labelThumbnailAspectRatioTo` | `double` | `2.0` | 宽高比上限 |
| `labelThumbnailAspectRatioStepSize` | `double` | `0.1` | 宽高比步长 |
| `labelThumbnailBorderPadding` | `double` | `0.1` | 标注缩略图边界扩展 |
| `labelThumbnailBorderPaddingFrom` | `double` | `0.0` | 边界扩展下限 |
| `labelThumbnailBorderPaddingTo` | `double` | `1.0` | 边界扩展上限 |
| `labelThumbnailBorderPaddingStepSize` | `double` | `0.1` | 边界扩展步长 |

`imageCellScale` 已迁移到 `DataSettings`，不要再从 `UISettings` 读取。

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
    static sqlpp::sqlite3::connection connect(const QString &path, const int flags);
    static QString applicationDatabasePath(const QString &fileName);
};
```

### ProjectDataBase

负责 `.dlpro` 项目数据库。主要能力：

- 项目元信息：`initProject()`、`openProject()`、`updateProject()`、`getProjectInfo()`、`getLabelInfo()`。
- 数据集：`getAllDatasets()`、`addDataset()`、`updateDataset()`、`deleteDataset()`。
- 图像：`addImages()`、`getImage()`、`getImages()`、`getAllImages()`、`deleteImages()`。
- 标签类别：`getAllLabelClasses()`、`addLabelClass()`、`updateLabelClass()`、`deleteLabelClass()`。
- 图像标签：`getAllTagClasses()`、`addTagClass()`、`updateTagClass()`、`deleteTagClass()`、`getAllTags()`、`addImagesTag()`、`deleteImagesTag()`。
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

保存全局设置，默认数据库位置为软件目录下的 `db/settings.db`。设置表使用 `group_name + setting_key` 唯一定位一项，值以文本和类型标记保存。

```cpp
class SettingsDataBase : public DataBase {
public:
    QVariant value(const QString &group, const QString &key,
                   const QVariant &default_value, QString &err_msg) const;
    bool setValue(const QString &group, const QString &key,
                  const QVariant &value, QString &err_msg) const;
};
```

## 4. Data (`dltool.data`)

头文件：`src/data/include/data/`

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
Q_INVOKABLE QString getMethodName(const int method);
```

当前 `getMethodTypes()` 返回 `Classification`、`Detection` 和 `Segmentation`，项目有效性校验接受这三类。

### DataFormat

QML 单例：`DataFormat`

```cpp
static Q_INVOKABLE QList<QString> getSupportedDataFormat();  // ["LabelMe", "COCO"]
static Q_INVOKABLE QList<QString> getSupportedExportDataFormat(); // ["LabelMe", "COCO"]
static Q_INVOKABLE QList<QString> getSupportedImageFormat(); // jpg/jpeg/png/bmp/webp
static Q_INVOKABLE int getDataFormat(const QString &name);
static bool isDataFormatSupported(const int data_format);
```

当前导入支持 LabelMe、COCO；导出支持 LabelMe、COCO。目标检测使用 bbox 标注；语义分割使用多边形点集 `points`，并保留 bbox 作为显示、筛选和缩略图裁剪的外接框。LabelMe polygon、COCO polygon `segmentation` 和 COCO RLE `segmentation` 会导入为点集，导出时也会按点集写出。

### DataManager

QML 类型：`DataManager`，不可直接创建；通过 `Project.dataManager` 获取。

```cpp
class DataManager : public QObject {
    Q_PROPERTY(DatasetsListModel *datasets READ datasets CONSTANT)
    Q_PROPERTY(ImageInstancesListModel *imageInstances READ imageInstances CONSTANT)
    Q_PROPERTY(LabelClassesListModel *labelClasses READ labelClasses CONSTANT)
    Q_PROPERTY(ImageTagsListModel *imageTags READ imageTags CONSTANT)
    Q_PROPERTY(LabelInstancesListModel *labelInstances READ labelInstances CONSTANT)
    Q_PROPERTY(ImageLabelsListModel *imageLabelsList READ imageLabelsList CONSTANT)
    Q_PROPERTY(ImageLabelsTableModel *imageLabelsTable READ imageLabelsTable CONSTANT)
    Q_PROPERTY(ImageInfoListModel *imageInfo READ imageInfo CONSTANT)
    Q_PROPERTY(GlobalFilter *globalFilter READ globalFilter CONSTANT)
    Q_PROPERTY(DatasetFilterItemsModel *datasetFilterItems READ datasetFilterItems CONSTANT)
    Q_PROPERTY(TagFilterItemsModel *tagFilterItems READ tagFilterItems CONSTANT)
    Q_PROPERTY(LabelClassFilterItemsModel *labelClassFilterItems READ labelClassFilterItems CONSTANT)
    Q_PROPERTY(CategoryStatisticsModel *categoryStatisticsModel READ categoryStatisticsModel CONSTANT)
    Q_PROPERTY(int method READ method CONSTANT)

public:
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
    Q_INVOKABLE void updateLabels(const std::vector<int64_t> &label_ids,
                                  const std::vector<QVariantMap> &data);
    Q_INVOKABLE void updateLabelsClass(const std::vector<int64_t> &label_ids,
                                       const std::vector<int64_t> &label_class_ids);
    Q_INVOKABLE void deleteLabels(const std::vector<int64_t> &label_ids);

    Q_INVOKABLE void addTagClass(const QString &name);
    Q_INVOKABLE QString getImageName(const int64_t image_id) const;
    Q_INVOKABLE QString getImagePath(const int64_t image_id) const;
    Q_INVOKABLE QString getImageDatasetName(const int64_t image_id) const;
    Q_INVOKABLE QString getImageTagName(const int64_t image_id) const;
};
```

QML 示例：

```qml
import dltool.project

Item {
    property var dataManager: ProjectManager.currentProject
                              ? ProjectManager.currentProject.dataManager
                              : null

    Component.onCompleted: {
        if (dataManager) {
            dataManager.addDataset("train")
        }
    }
}
```

### GlobalFilter

QML 类型：`GlobalFilter`，通过 `DataManager.globalFilter` 获取。

```cpp
enum class FilterType {
    Dataset,
    Tag,
    LabelClass,
    ImageLabelClass
};

Q_PROPERTY(bool isActive READ isActive NOTIFY filterStateChanged)
Q_PROPERTY(int activeFilterCount READ activeFilterCount NOTIFY filterStateChanged)
Q_PROPERTY(QString filterSummary READ filterSummary NOTIFY filterStateChanged)

Q_INVOKABLE void setFilter(FilterType type, const std::vector<int64_t> &ids);
Q_INVOKABLE void setFilterEnabled(FilterType type, bool enabled);
Q_INVOKABLE void clearFilter(FilterType type);
Q_INVOKABLE std::vector<int64_t> getActiveIds(FilterType type) const;
Q_INVOKABLE void clearAllFilters();
```

### 常用模型 Role

| 模型 | QML 名称 | 主要 role |
|------|----------|-----------|
| `DatasetsListModel` | `DatasetsModel` | `dataset_id`、`name`、`stats`、`progress` |
| `ImageInstancesListModel` | `ImageInstancesModel` | `image_id`、`name`、`path`、`selected`、`hasLabels` |
| `ImageInfoListModel` | `ImageInfoModel` | `title`、`value` |
| `LabelClassesListModel` | `LabelClassesModel` | `label_class_id`、`name`、`color`、`shortcut`、`ordinal_index`、`selected` |
| `ImageTagsListModel` | `ImageTagsModel` | `tag_id`、`name`、`selected_images_stats`、`current_image_stats` |
| `LabelInstancesListModel` | `LabelInstancesModel` | `label_id`、`image_id`、`label_class_id`、`label_class_name`、`label_class_color`、`data` |
| `ImageLabelsListModel` | `ImageLabelsListModel` | `label_id`、`image_id`、`label_class_id`、`data`、`color`、`selected`、`hovered` |
| `ImageLabelsTableModel` | `ImageLabelsTableModel` | `display`、`data`、`class_data`、`selected` |
| `FilterItemsModel` | `FilterItemsModel` | `id`、`text`、`checked` |
| `CategoryStatisticsModel` | `CategoryStatisticsModel` | `categoryId`、`categoryName`、`categoryColor`、`instanceCount`、`imageCount`、`instancePercentage`、`imagePercentage` |

标注 `data` 的公共字段为 `x`、`y`、`width`、`height`。语义分割标注额外包含 `point_count` 和 `points`，其中 `points` 是 `{x, y}` 点列表；检测标注会忽略该字段。

## 5. Project (`dltool.project`)

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

`projectActivated()` 在项目创建、成功打开，以及再次激活当前已打开项目时触发；主界面用它切换到图库页。

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

## 6. UI (`dltool.ui`)

头文件：`src/ui/include/ui/`

### DltColor 与 DltFont

QML 单例：

- `DltColor`：`Transparent`、`Black`、`Background`、`Primary`、`Border`、`ScrollBar`、`ToolTip`、`Hovered`、`Highlight`、`FontPrimary`、`FontDark`、`TabButton`、`Button` 等颜色。
- `DltFont`：`Caption`、`Body`、`BodyStrong`、`Subtitle`、`Title`、`TitleLarge`、`Display`。

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

### Utils

QML 单例：`Utils`

```cpp
Q_INVOKABLE QColor withOpacity(const QColor &color, qreal opacity) const;
Q_INVOKABLE QString getCleanPath(const QString &path) const;
Q_INVOKABLE void openInFileExplorer(const QString &path);
```

### QML 控件

`src/ui/controls/` 当前包含：

```text
DltButton.qml
DltCheckBox.qml
DltComboBox.qml
DltContentDialog.qml
DltControlBackground.qml
DltEditor.qml
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
