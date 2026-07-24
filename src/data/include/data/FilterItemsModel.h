#pragma once

#include <QAbstractListModel>
#include <QtQml>
#include <vector>

namespace dltool::data {

/**
 * @brief 过滤项数据结构
 * 
 * 存储过滤下拉菜单中每个项目的信息
 */
struct FilterItem
{
    int64_t id;      // 项目ID（数据集ID或标签ID）
    QString text;    // 显示文本
    bool    checked; // 是否选中
    bool    enabled; // 是否可用

    FilterItem(int64_t id_ = -1, const QString &text_ = QString(), bool checked_ = true, bool enabled_ = true)
        : id(id_)
        , text(text_)
        , checked(checked_)
        , enabled(enabled_)
    {
    }
};

/**
 * @brief 过滤项列表模型基类
 * 
 * 为过滤下拉菜单提供数据模型
 * 支持显示项目列表和跟踪选中状态
 */
class FilterItemsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(FilterItemsModel)
    QML_UNCREATABLE("Cannot create FilterItemsModel directly!")

public:
    enum Role
    {
        IdRole = Qt::UserRole + 1, // 项目ID
        TextRole,                  // 显示文本
        CheckedRole,               // 选中状态
        EnabledRole                // 可用状态
    };
    Q_ENUM(Role)

    explicit FilterItemsModel(QObject *parent = nullptr);
    ~FilterItemsModel() override = default;

    // QAbstractListModel 接口实现
    int                    rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant               data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool                   setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags          flags(const QModelIndex &index) const override;
    QHash<int, QByteArray> roleNames() const override;

    /**
     * @brief 清空模型
     */
    Q_INVOKABLE void clear();

    /**
     * @brief 添加项目到模型
     * @param id 项目ID
     * @param text 显示文本
     * @param checked 是否选中（默认true）
     */
    Q_INVOKABLE void append(int64_t id, const QString &text, bool checked = true, bool enabled = true);

    /**
     * @brief 获取所有选中项的ID列表
     * @return 选中项ID列表
     */
    Q_INVOKABLE std::vector<int64_t> getCheckedIds() const;

    /**
     * @brief 设置所有项的选中状态
     * @param checked 选中状态
     */
    Q_INVOKABLE void setAllChecked(bool checked);

protected:
    /**
     * @brief 一次性替换全部过滤项。
     * @param items 新的过滤项列表。
     */
    void replaceItems(std::vector<FilterItem> items);

    std::vector<FilterItem> items_; // 项目列表
};

/**
 * @brief 数据集过滤项模型
 * 
 * 从DatasetsListModel填充数据集列表
 */
class DatasetFilterItemsModel : public FilterItemsModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DatasetFilterItemsModel)
    QML_UNCREATABLE("Cannot create DatasetFilterItemsModel directly!")

public:
    explicit DatasetFilterItemsModel(QObject *parent = nullptr);
    ~DatasetFilterItemsModel() override = default;

    /**
     * @brief 从DatasetsListModel填充模型
     * @param datasets_model 数据集列表模型
     */
    void populateFromDatasets(QAbstractItemModel *datasets_model);
};

/**
 * @brief 标签过滤项模型
 * 
 * 从ImageTagsListModel填充标签列表
 */
class TagFilterItemsModel : public FilterItemsModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(TagFilterItemsModel)
    QML_UNCREATABLE("Cannot create TagFilterItemsModel directly!")

public:
    explicit TagFilterItemsModel(QObject *parent = nullptr);
    ~TagFilterItemsModel() override = default;

    /**
     * @brief 从ImageTagsListModel填充模型
     * @param tags_model 标签列表模型
     */
    void populateFromTags(QAbstractItemModel *tags_model);
};

/**
 * @brief 标签类别过滤项模型
 * 
 * 从LabelClassesListModel填充标签类别列表
 */
class LabelClassFilterItemsModel : public FilterItemsModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(LabelClassFilterItemsModel)
    QML_UNCREATABLE("Cannot create LabelClassFilterItemsModel directly!")

public:
    explicit LabelClassFilterItemsModel(QObject *parent = nullptr);
    ~LabelClassFilterItemsModel() override = default;

    /**
     * @brief 从LabelClassesListModel填充模型
     * @param label_classes_model 标签类别列表模型
     */
    void populateFromLabelClasses(QAbstractItemModel *label_classes_model);
};

class CustomFilterItemsModel : public FilterItemsModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(CustomFilterItemsModel)
    QML_UNCREATABLE("Cannot create CustomFilterItemsModel directly!")

public:
    explicit CustomFilterItemsModel(QObject *parent = nullptr);
    ~CustomFilterItemsModel() override = default;

    void populateFromCustomConditions();
    void setSearchResultsAvailable(bool image_search_available, bool label_search_available);
};

} // namespace dltool::data
