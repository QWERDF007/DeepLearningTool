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

## 2. Data 模块 API

### 2.1 DataBase

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

### 2.2 ProjectDataBase

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

### 2.3 DataManager

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

## 3. Project 模块 API

### 3.1 Project

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

### 3.2 ProjectManager

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

### 3.3 RectentProjects

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

## 4. UI 模块 API

### 4.1 Color

颜色管理类。

```cpp
namespace dltool::ui {

class Color : public QObject {
    Q_OBJECT
    // 提供主题颜色属性
};

}
```

### 4.2 Font

字体管理类。

```cpp
namespace dltool::ui {

class Font : public QObject {
    Q_OBJECT
    // 提供字体配置属性
};

}
```

### 4.3 UILogger

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

## 5. QML API

### 5.1 ProjectManager (QML)

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

### 5.2 DataManager (QML)

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

### 5.3 自定义控件 (QML)

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

## 6. 数据模型角色

### 6.1 DatasetsListModel

| 角色 | 类型 | 说明 |
|------|------|------|
| `NameRole` | QString | 数据集名称 |
| `IdRole` | int64_t | 数据集ID |
| `CountRole` | int | 图像数量 |

### 6.2 ImageInstancesListModel

| 角色 | 类型 | 说明 |
|------|------|------|
| `IdRole` | int64_t | 图像ID |
| `PathRole` | QString | 图像路径 |
| `DatasetIdRole` | int64_t | 所属数据集ID |
| `SelectedRole` | bool | 是否选中 |

### 6.3 LabelClassesListModel

| 角色 | 类型 | 说明 |
|------|------|------|
| `IdRole` | int64_t | 标签类别ID |
| `NameRole` | QString | 类别名称 |
| `ColorRole` | QString | 颜色值 |
| `ShortcutRole` | QString | 快捷键 |
| `OrdinalIndexRole` | int64_t | 排序索引 |

### 6.4 LabelInstancesListModel

| 角色 | 类型 | 说明 |
|------|------|------|
| `IdRole` | int64_t | 标签ID |
| `ImageIdRole` | int64_t | 图像ID |
| `LabelClassIdRole` | int64_t | 标签类别ID |
| `LabelTypeRole` | int64_t | 标签类型 |
| `DataRole` | QVariantMap | 标签数据 |
