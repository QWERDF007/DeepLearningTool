# DeepLearningTool API 参考文档

## 1. Common 模块 API

### 1.1 Logger

日志系统封装，基于 spdlog 实现。

```cpp
namespace dltool::common {

// 获取默认日志输出目标
std::vector<spdlog::sink_ptr> defaultSinks();

// 设置日志器
std::shared_ptr<spdlog::logger> setupLogger(const std::vector<spdlog::sink_ptr> &sinks);

}
```

**使用示例**:
```cpp
auto sinks = dltool::common::defaultSinks();
auto logger = dltool::common::setupLogger(sinks);
logger->set_level(spdlog::level::debug);
spdlog::set_default_logger(logger);
```

### 1.2 Singleton

单例模板类，支持 C++ 和 Qt QML 两种模式。

```cpp
namespace dltool::common {

template<typename T>
class Singleton {
public:
    static T *getInstance();      // Qt管理生命周期
    static T *getInstanceCPP();   // C++静态变量生命周期
};

}
```

**宏定义**:
```cpp
// C++ 单例
SINGLETON(ClassName)

// Qt QML 单例
QT_QML_SINGLETON(ClassName)
```

### 1.3 CrashHandler

跨平台崩溃处理器。

```cpp
namespace dltool::common {

class CrashHandler {
public:
    void setup();  // 初始化崩溃处理
};

}
```

---

## 2. Settings 模块 API

### 2.1 GlobalSettings

全局设置管理器单例，提供对所有设置的统一访问。

```cpp
namespace dltool::settings {

class GlobalSettings : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(GlobalSettings)
    QT_QML_SINGLETON(GlobalSettings)
    
    Q_PROPERTY(ProjectSettings* project READ project CONSTANT)
    Q_PROPERTY(DataSettings* data READ data CONSTANT)
    Q_PROPERTY(UISettings* ui READ ui CONSTANT)
    
public:
    static GlobalSettings* getInstance();
    
    ProjectSettings* project() const;
    DataSettings* data() const;
    UISettings* ui() const;
    
    Q_INVOKABLE void load();   // 加载所有设置
    Q_INVOKABLE void save();   // 保存所有设置
    Q_INVOKABLE void reset();  // 重置所有设置为默认值
};

}
```

**使用示例**:
```cpp
// C++ 访问
auto* settings = dltool::settings::GlobalSettings::getInstance();
int margin = settings->data()->thumbnailMargin();
settings->data()->setThumbnailMargin(15);
settings->save();
```

```qml
// QML 访问
import dltool.settings

Item {
    Component.onCompleted: {
        console.log("Margin:", GlobalSettings.data.thumbnailMargin)
        GlobalSettings.data.thumbnailMargin = 15
        GlobalSettings.save()
    }
}
```

### 2.2 ProjectSettings

项目相关设置。

```cpp
namespace dltool::settings {

class ProjectSettings : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(ProjectSettings)
    
    Q_PROPERTY(int maxRecentProjects READ maxRecentProjects WRITE setMaxRecentProjects NOTIFY maxRecentProjectsChanged)
    Q_PROPERTY(int autoSaveInterval READ autoSaveInterval WRITE setAutoSaveInterval NOTIFY autoSaveIntervalChanged)
    Q_PROPERTY(bool autoSaveEnabled READ autoSaveEnabled WRITE setAutoSaveEnabled NOTIFY autoSaveEnabledChanged)
    
public:
    int maxRecentProjects() const;        // 默认: 10
    void setMaxRecentProjects(int value);
    
    int autoSaveInterval() const;         // 默认: 300 (秒)
    void setAutoSaveInterval(int value);
    
    bool autoSaveEnabled() const;         // 默认: true
    void setAutoSaveEnabled(bool value);
    
signals:
    void maxRecentProjectsChanged();
    void autoSaveIntervalChanged();
    void autoSaveEnabledChanged();
};

}
```

### 2.3 DataSettings

数据处理相关设置。

```cpp
namespace dltool::settings {

class DataSettings : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(DataSettings)
    
    Q_PROPERTY(int thumbnailMargin READ thumbnailMargin WRITE setThumbnailMargin NOTIFY thumbnailMarginChanged)
    Q_PROPERTY(int thumbnailCacheSize READ thumbnailCacheSize WRITE setThumbnailCacheSize NOTIFY thumbnailCacheSizeChanged)
    Q_PROPERTY(int imageLoadThreads READ imageLoadThreads WRITE setImageLoadThreads NOTIFY imageLoadThreadsChanged)
    Q_PROPERTY(int labelBorderWidth READ labelBorderWidth WRITE setLabelBorderWidth NOTIFY labelBorderWidthChanged)
    Q_PROPERTY(int labelFillOpacity READ labelFillOpacity WRITE setLabelFillOpacity NOTIFY labelFillOpacityChanged)
    
public:
    int thumbnailMargin() const;          // 默认: 10
    void setThumbnailMargin(int value);
    
    int thumbnailCacheSize() const;       // 默认: 100 (MB)
    void setThumbnailCacheSize(int value);
    
    int imageLoadThreads() const;         // 默认: 4
    void setImageLoadThreads(int value);
    
    int labelBorderWidth() const;         // 默认: 2
    void setLabelBorderWidth(int value);
    
    int labelFillOpacity() const;         // 默认: 30 (%)
    void setLabelFillOpacity(int value);
    
signals:
    void thumbnailMarginChanged();
    void thumbnailCacheSizeChanged();
    void imageLoadThreadsChanged();
    void labelBorderWidthChanged();
    void labelFillOpacityChanged();
};

}
```

### 2.4 UISettings

界面相关设置。

```cpp
namespace dltool::settings {

class UISettings : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(UISettings)
    
    Q_PROPERTY(double imageCellScale READ imageCellScale WRITE setImageCellScale NOTIFY imageCellScaleChanged)
    Q_PROPERTY(double imageCellScaleFrom READ imageCellScaleFrom WRITE setImageCellScaleFrom NOTIFY imageCellScaleFromChanged)
    Q_PROPERTY(double imageCellScaleTo READ imageCellScaleTo WRITE setImageCellScaleTo NOTIFY imageCellScaleToChanged)
    Q_PROPERTY(double imageCellScaleStepSize READ imageCellScaleStepSize WRITE setImageCellScaleStepSize NOTIFY imageCellScaleStepSizeChanged)
    
    Q_PROPERTY(double imageBrightness READ imageBrightness WRITE setImageBrightness NOTIFY imageBrightnessChanged)
    Q_PROPERTY(double imageBrightnessFrom READ imageBrightnessFrom CONSTANT)
    Q_PROPERTY(double imageBrightnessTo READ imageBrightnessTo CONSTANT)
    Q_PROPERTY(double imageBrightnessStepSize READ imageBrightnessStepSize CONSTANT)
    
    Q_PROPERTY(double imageContrast READ imageContrast WRITE setImageContrast NOTIFY imageContrastChanged)
    Q_PROPERTY(double imageContrastFrom READ imageContrastFrom CONSTANT)
    Q_PROPERTY(double imageContrastTo READ imageContrastTo CONSTANT)
    Q_PROPERTY(double imageContrastStepSize READ imageContrastStepSize CONSTANT)
    
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    
public:
    // 图像单元格缩放
    double imageCellScale() const;              // 默认: 1.0
    void setImageCellScale(double value);
    
    double imageCellScaleFrom() const;          // 默认: 0.5
    void setImageCellScaleFrom(double value);
    
    double imageCellScaleTo() const;            // 默认: 4.0
    void setImageCellScaleTo(double value);
    
    double imageCellScaleStepSize() const;      // 默认: 0.25
    void setImageCellScaleStepSize(double value);
    
    // 图像亮度
    double imageBrightness() const;             // 默认: 0.0, 范围: -1.0 到 1.0
    void setImageBrightness(double value);
    
    double imageBrightnessFrom() const;         // 常量: -1.0
    double imageBrightnessTo() const;           // 常量: 1.0
    double imageBrightnessStepSize() const;     // 常量: 0.1
    
    // 图像对比度
    double imageContrast() const;               // 默认: 0.0, 范围: -1.0 到 1.0
    void setImageContrast(double value);
    
    double imageContrastFrom() const;           // 常量: -1.0
    double imageContrastTo() const;             // 常量: 1.0
    double imageContrastStepSize() const;       // 常量: 0.1
    
    // 主题和语言
    QString theme() const;                      // 默认: "dark"
    void setTheme(const QString& value);
    
    QString language() const;                   // 默认: "zh_CN"
    void setLanguage(const QString& value);
    
signals:
    void imageCellScaleChanged();
    void imageCellScaleFromChanged();
    void imageCellScaleToChanged();
    void imageCellScaleStepSizeChanged();
    void imageBrightnessChanged();
    void imageContrastChanged();
    void themeChanged();
    void languageChanged();
};

}
```

---

## 3. Data 模块 API

### 3.1 DataBase

数据库连接管理基类。

```cpp
namespace dltool::data {

class DataBase : public QObject {
public:
    DataBase(const QString &path, QObject *parent = nullptr);
    
    // 获取连接池
    sqlpp::sqlite3::connection_pool *connectionPool();
    
    // 静态连接方法
    static sqlpp::sqlite3::connection connect(const QString &path, const int flags);
};

}
```

### 3.2 ProjectDataBase

项目数据库操作类。

```cpp
namespace dltool::data {

class ProjectDataBase : public DataBase {
public:
    ProjectDataBase(const QString &path, QObject *parent = nullptr);
    
    // 项目操作
    bool initProject(const QString &name, const int method, const QString &path,
                     const QString &description, const QString image_base_path,
                     const qint64 ctime, const qint64 mtime, QString &err_msg) const;
    bool openProject(QString &name, int &method, QString &path, QString &description,
                     QString image_base_path, qint64 &ctime, qint64 &mtime, QString &err_msg) const;
    bool updateProject(const QString &name, const QString &path, const QString &description,
                       const QString &image_base_path, const qint64 mtime, QString &err_msg) const;
    
    // 数据集操作
    bool getAllDatasets(std::vector<int64_t> &dataset_ids, std::vector<QString> &names, QString &err_msg) const;
    bool addDataset(const QString &name, int64_t &dataset_id, QString &err_msg) const;
    bool updateDataset(const int64_t dataset_id, const QString &name, QString &err_msg) const;
    bool deleteDataset(const int64_t dataset_id, QString &err_msg) const;
    
    // 图像操作
    int64_t getImagesCount(const int64_t dataset_id) const;
    bool addImages(const int64_t dataset_id, const std::vector<QString> &paths,
                   std::vector<int64_t> &image_ids, QString &err_msg) const;
    bool getImages(const int64_t dataset_id, std::vector<int64_t> &image_ids,
                   std::vector<QString> &paths, QString &err_msg) const;
    bool deleteImages(const std::vector<int64_t> &image_ids, QString &err_msg) const;
    
    // 标签类别操作
    bool getAllLabelClasses(std::vector<int64_t> &label_class_ids, std::vector<QString> &names,
                            std::vector<QString> &colors, std::vector<QString> &shortcuts,
                            std::vector<int64_t> &ordinal_indices, QString &err_msg) const;
    bool addLabelClass(const QString &name, const QString &color, const QString &shortcut,
                       const int64_t ordinal_index, int64_t &label_class_id, QString &err_msg) const;
    bool updateLabelClass(const int64_t label_class_id, const QString &name, const QString &color,
                          const QString &shortcut, const int64_t ordinal_index, QString &err_msg) const;
    bool deleteLabelClass(const int64_t label_class_id, QString &err_msg) const;
    
    // 标签操作
    bool getAllLabels(std::vector<int64_t> &label_ids, std::vector<int64_t> &image_ids,
                      std::vector<int64_t> &label_class_ids, std::vector<int64_t> &label_types,
                      std::vector<std::vector<uint8_t>> &labels_data, QString &err_msg) const;
    bool addLabels(const std::vector<int64_t> &image_ids, const std::vector<int64_t> &label_class_ids,
                   const std::vector<int64_t> &label_types, const std::vector<std::vector<uint8_t>> &labels_data,
                   std::vector<int64_t> &label_ids, QString &err_msg) const;
    bool updateLabelsData(const std::vector<int64_t> &label_ids,
                          const std::vector<std::vector<uint8_t>> &labels_data, QString &err_msg) const;
    bool deleteLabels(const std::vector<int64_t> &label_ids, QString &err_msg) const;
    
    // 标签标签操作
    bool getAllTagClasses(std::vector<int64_t> &tag_class_ids, std::vector<QString> &names, QString &err_msg) const;
    bool addTagClass(const QString &name, int64_t &tag_class_id, QString &err_msg) const;
};

}
```

### 3.3 DataManager

数据管理器，统一管理所有数据模型。

```cpp
namespace dltool::data {

class DataManager : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(DataManager)
    
    // QML属性
    Q_PROPERTY(DatasetsListModel *datasets READ datasets CONSTANT)
    Q_PROPERTY(ImageInstancesListModel *imageInstances READ imageInstances CONSTANT)
    Q_PROPERTY(LabelClassesListModel *labelClasses READ labelClasses CONSTANT)
    Q_PROPERTY(ImageTagsListModel *imageTags READ imageTags CONSTANT)
    Q_PROPERTY(LabelInstancesListModel *labelInstances READ labelInstances CONSTANT)
    Q_PROPERTY(ImageLabelsListModel *imageLabelsList READ imageLabelsList CONSTANT)
    Q_PROPERTY(ImageLabelsTableModel *imageLabelsTable READ imageLabelsTable CONSTANT)
    Q_PROPERTY(ImageInfoListModel *imageInfo READ imageInfo CONSTANT)
    
public:
    DataManager(const int method, ProjectDataBase *database, QObject *parent = nullptr);
    
    // 数据集操作
    Q_INVOKABLE QList<QString> getAllDatasetsName() const;
    Q_INVOKABLE int getDatasetId(const QString &dataset_name) const;
    Q_INVOKABLE QString getDatasetName(const int dataset_id) const;
    Q_INVOKABLE void addDataset(const QString &name);
    Q_INVOKABLE void updateDataset(const int64_t dataset_id, const QString &name);
    Q_INVOKABLE void deleteDataset(const int64_t dataset_id);
    
    // 数据导入
    Q_INVOKABLE void importData(const int64_t dataset_id, const int data_format,
                                const QString &image_dir, const QString &data_dir);
    
    // 图像操作
    Q_INVOKABLE void deleteSelectedImages();
    
    // 标签类别操作
    Q_INVOKABLE void addLabelClass(const QString &name, const QString &color, const QString &shortcut);
    Q_INVOKABLE void updateLabelClass(const int64_t label_class_id, const QString &name,
                                      const QString &color, const QString &shortcut, const int64_t ordinal_index);
    Q_INVOKABLE void deleteLabelClass(const int64_t label_class_id);
    
    // 标签操作
    Q_INVOKABLE void addLabels(const std::vector<int64_t> &image_ids,
                               const std::vector<int64_t> &label_class_ids,
                               const std::vector<QVariantMap> &data);
    Q_INVOKABLE void updateLabels(const std::vector<int64_t> &label_ids,
                                  const std::vector<QVariantMap> &data);
    Q_INVOKABLE void deleteLabels(const std::vector<int64_t> &label_ids);
    
    // 标签标签操作
    Q_INVOKABLE void addTagClass(const QString &name);
};

}
```

---

## 4. Project 模块 API

### 4.1 Project

项目实体类。

```cpp
namespace dltool::project {

class Project : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Project)
    
    // QML属性
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(int method READ method CONSTANT)
    Q_PROPERTY(QString path READ path CONSTANT)
    Q_PROPERTY(QString description READ description NOTIFY descriptionChanged)
    Q_PROPERTY(QString imageBasePath READ imageBasePath NOTIFY imageBasePathChanged)
    Q_PROPERTY(data::DataManager *dataManager READ dataManager CONSTANT)
    
public:
    // 创建新项目
    Project(const QString &name, const int method, const QString &path,
            const QString &description, const QString image_base_path,
            const qint64 ctime, const qint64 mtime, QObject *parent = nullptr);
    
    // 打开已有项目
    Project(const QString &path, QObject *parent = nullptr);
    
    void initProject();   // 初始化新项目
    void openProject();   // 打开已有项目
    
    // 验证项目有效性
    static std::tuple<bool, QString> isValid(const int method, const QString &path, bool is_new);
    
    // 属性访问器
    QString name() const;
    int method() const;
    QString path() const;
    QString description() const;
    QString imageBasePath() const;
    qint64 ctime() const;
    qint64 mtime() const;
    data::DataManager *dataManager() const;
    
signals:
    void descriptionChanged();
    void imageBasePathChanged();
};

}
```

### 4.2 ProjectManager

项目管理器单例。

```cpp
namespace dltool::project {

class ProjectManager : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(ProjectManager)
    QT_QML_SINGLETON(ProjectManager)
    
    Q_PROPERTY(Project *currentProject READ currentProject NOTIFY currentProjectChanged)
    Q_PROPERTY(RectentProjects *recentProjects READ recentProjects CONSTANT)
    
public:
    // 项目操作
    Q_INVOKABLE Project *createProject(const QString &name, const int method,
                                       const QString &path, const QString &desc,
                                       const QString image_base_path);
    Q_INVOKABLE Project *openProject(const QString &path);
    Q_INVOKABLE void closeProject();
    Q_INVOKABLE bool updateProjectBaseInfo(const QString &path, const QString &name,
                                           const QString &description);
    Q_INVOKABLE void deleteProject(const QString &path);
    Q_INVOKABLE void removeFromRectentProjects(const QString &path);
    
    // 验证
    Q_INVOKABLE QString isProjectValid(const int method, const QString &path, bool is_new);
    
    // 项目文件信息
    Q_INVOKABLE QString projectSuffix() const;      // 返回 ".dlpro"
    Q_INVOKABLE QString projectFileFilter() const;  // 返回 "Project files (*.dlpro)"
    
    // 项目信息查询
    Q_INVOKABLE QVariantMap getProjectInfo(const QString &path);
    Q_INVOKABLE QVariantMap getLabelInfo(const QString &path);
    
    // 属性访问器
    Project *currentProject() const;
    RectentProjects *recentProjects() const;
    
signals:
    void currentProjectChanged();
};

}
```

### 4.3 RectentProjects

最近项目列表模型。

```cpp
namespace dltool::project {

class RectentProjects : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QItemSelectionModel *selection READ selection CONSTANT)
    Q_PROPERTY(QString currentProjectPath READ currentProjectPath NOTIFY currentProjectPathChanged)
    
public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        PathRole,
        ToolTipRole,
        SelectedRole,
    };
    
    // QAbstractListModel 接口
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QHash<int, QByteArray> roleNames() const override;
    
    // 项目操作
    bool addProject(const QString &path);
    bool updateProjectBaseInfo(const QString &path, const QString &new_name, const qint64 new_mtime);
    bool openProject(const QString &path);
    bool removeProject(const QString &path);
    bool setCurrentProjectPath(const QString &path);
    
signals:
    void currentProjectPathChanged();
};

}
```

---

## 5. UI 模块 API

### 5.1 Color

颜色管理类。

```cpp
namespace dltool::ui {

class Color : public QObject {
    Q_OBJECT
    // 提供主题颜色属性
};

}
```

### 5.2 Font

字体管理类。

```cpp
namespace dltool::ui {

class Font : public QObject {
    Q_OBJECT
    // 提供字体配置属性
};

}
```

### 5.3 UILogger

UI日志输出类，用于在界面显示日志。

```cpp
namespace dltool::ui {

class UILogger : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(UILogger)
    QT_QML_SINGLETON(UILogger)
    
    // 作为 spdlog sink 接收日志消息
};

}
```

---

## 6. QML API

### 6.1 GlobalSettings (QML)

```qml
import dltool.settings

// 访问单例
GlobalSettings.project      // ProjectSettings 实例
GlobalSettings.data         // DataSettings 实例
GlobalSettings.ui           // UISettings 实例

// 方法
GlobalSettings.load()       // 加载所有设置
GlobalSettings.save()       // 保存所有设置
GlobalSettings.reset()      // 重置所有设置

// 使用示例
Item {
    // 读取设置
    property int margin: GlobalSettings.data.thumbnailMargin
    property double brightness: GlobalSettings.ui.imageBrightness
    
    // 修改设置
    Slider {
        from: GlobalSettings.ui.imageBrightnessFrom
        to: GlobalSettings.ui.imageBrightnessTo
        stepSize: GlobalSettings.ui.imageBrightnessStepSize
        value: GlobalSettings.ui.imageBrightness
        onValueChanged: GlobalSettings.ui.imageBrightness = value
    }
    
    // 保存设置
    Button {
        text: "保存设置"
        onClicked: GlobalSettings.save()
    }
}
```

### 6.2 ProjectManager (QML)

```qml
import dltool.project

// 访问单例
ProjectManager.currentProject      // 当前项目
ProjectManager.recentProjects      // 最近项目列表

// 方法
ProjectManager.createProject(name, method, path, desc, imageBasePath)
ProjectManager.openProject(path)
ProjectManager.closeProject()
ProjectManager.deleteProject(path)
ProjectManager.isProjectValid(method, path, isNew)
```

### 6.3 DataManager (QML)

```qml
import dltool.data

// 通过 Project 访问
project.dataManager.datasets           // 数据集列表模型
project.dataManager.imageInstances     // 图像实例列表模型
project.dataManager.labelClasses       // 标签类别列表模型
project.dataManager.imageTags          // 图像标签列表模型
project.dataManager.labelInstances     // 标签实例列表模型

// 方法
dataManager.addDataset(name)
dataManager.deleteDataset(datasetId)
dataManager.importData(datasetId, format, imageDir, dataDir)
dataManager.addLabelClass(name, color, shortcut)
dataManager.deleteLabelClass(labelClassId)
```

### 6.4 自定义控件 (QML)

```qml
import dltool.ui

// 按钮
DltButton { text: "按钮" }
DltFilledButton { text: "填充按钮" }
DltTextIconButton { text: "图标按钮"; icon: "\uE710" }

// 输入
DltTextField { placeholderText: "请输入..." }
DltTextArea { placeholderText: "多行文本..." }
DltComboBox { model: ["选项1", "选项2"] }

// 容器
DltPage { title: "页面标题" }
DltScrollablePage { }
DltSplitView { }

// 对话框
DltContentDialog {
    title: "标题"
    message: "消息内容"
    positiveText: "确定"
    negativeText: "取消"
}

// 其他
DltProgressBar { value: 0.5 }
DltSlider { from: 0; to: 100 }
DltMenu { }
DltToolTip { text: "提示" }
```

---

## 7. 数据模型角色

### 7.1 DatasetsListModel

| 角色 | 类型 | 说明 |
|------|------|------|
| `NameRole` | QString | 数据集名称 |
| `IdRole` | int64_t | 数据集ID |
| `CountRole` | int | 图像数量 |

### 7.2 ImageInstancesListModel

| 角色 | 类型 | 说明 |
|------|------|------|
| `IdRole` | int64_t | 图像ID |
| `PathRole` | QString | 图像路径 |
| `DatasetIdRole` | int64_t | 所属数据集ID |
| `SelectedRole` | bool | 是否选中 |

### 7.3 LabelClassesListModel

| 角色 | 类型 | 说明 |
|------|------|------|
| `IdRole` | int64_t | 标签类别ID |
| `NameRole` | QString | 类别名称 |
| `ColorRole` | QString | 颜色值 |
| `ShortcutRole` | QString | 快捷键 |
| `OrdinalIndexRole` | int64_t | 排序索引 |

### 7.4 LabelInstancesListModel

| 角色 | 类型 | 说明 |
|------|------|------|
| `IdRole` | int64_t | 标签ID |
| `ImageIdRole` | int64_t | 图像ID |
| `LabelClassIdRole` | int64_t | 标签类别ID |
| `LabelTypeRole` | int64_t | 标签类型 |
| `DataRole` | QVariantMap | 标签数据 |
