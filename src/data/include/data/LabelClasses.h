#pragma once

#include <QAbstractListModel>
#include <QtQml>
#include <map>
#include <vector>

namespace dltool::database {
class ProjectDataBase;
} // namespace dltool::database

namespace dltool::data {

QString normalizeLabelClassGroup(const QString &group);
QString labelClassGroupDisplayName(const QString &group);
QString defaultLabelClassGroup();
QString unlabeledLabelClassGroup();
QString anomalyLabelClassGroup();
QString goodLabelClassGroup();

class LabelClass : public QObject
{
public:
    LabelClass(const int64_t id, const QString &name, const QString &color, const QString &shortcut,
               const int64_t ordinal_index, const QString &group, QObject *parent = nullptr);
    ~LabelClass();

    int64_t id() const
    {
        return id_;
    }

    QString name() const
    {
        return name_;
    }

    bool setName(const QString &name);

    QString color() const
    {
        return color_;
    }

    bool setColor(const QString &color);

    QString shortcut() const
    {
        return shortcut_;
    }

    bool setShortcut(const QString &shortcut);

    int64_t ordinalIndex() const
    {
        return ordinal_index_;
    }

    bool setOrdinalIndex(const int64_t ordinal_index);

    QString group() const
    {
        return group_;
    }

    bool setGroup(const QString &group);

private:
    int64_t id_;
    int64_t ordinal_index_;

    QString name_;
    QString color_;
    QString shortcut_;
    QString group_;
};

class LabelClassesListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(LabelClassesModel)
    QML_UNCREATABLE("Can not create LabelClassesModel directly!")
    Q_PROPERTY(QItemSelectionModel *selection READ selection CONSTANT)
    Q_PROPERTY(int currentLabelClassId READ getCurrentLabelClassId NOTIFY currentLabelClassChanged)
    Q_PROPERTY(QString currentLabelClassColor READ getCurrentLabelClassColor NOTIFY currentLabelClassChanged FINAL)
    Q_PROPERTY(QString currentLabelClassGroup READ getCurrentLabelClassGroup NOTIFY currentLabelClassChanged FINAL)
public:
    LabelClassesListModel(dltool::database::ProjectDataBase *database, QObject *parent = nullptr);
    ~LabelClassesListModel();

    enum Role
    {
        LabelClassIdRole = Qt::UserRole + 1,
        NameRole,
        ColorRole,
        ShortcutRole,
        GroupRole,
        GroupNameRole,
        OrdinalIndexRole,
        SelectedRole,
    };
    Q_ENUM(Role)

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    QHash<int, QByteArray> roleNames() const override;

    bool addLabelClass(const QString &name, const QString &color, const QString &shortcut, const QString &group);
    bool updateLabelClass(const int64_t label_class_id, const QString &name, const QString &color,
                          const QString &shortcut, const int64_t ordinal_index, const QString &group);
    bool updateLabelClass(const std::vector<int64_t> &label_class_ids, const std::vector<int64_t> &ordinal_indexes);
    bool deleteLabelClass(const int64_t label_class_id);

    int                  getLabelClassId(const QString &name) const;
    std::vector<int64_t> getAllLabelClassIds() const;

    QString getLabelClassName(const int label_class_id) const;
    QString getLabelClassColor(const int label_class_id) const;
    QString getLabelClassGroup(const int label_class_id) const;
    QString getLabelClassGroupName(const int label_class_id) const;
    bool    isUnlabeledLabelClass(const int label_class_id) const;
    bool    isAnomalyLabelClass(const int label_class_id) const;

    QItemSelectionModel *selection() const
    {
        return selection_;
    }

    int     getCurrentLabelClassId() const;
    QString getCurrentLabelClassColor() const;
    QString getCurrentLabelClassGroup() const;

    /**
     * @brief 验证标签类别的有效性
     * @param label_class_id 标签类别ID（编辑时排除自身）
     * @param name 类别名称
     * @param color 标签颜色
     * @param shortcut 快捷键
     * @param ordinal_index 排序索引
     * @return 错误信息，空字符串表示有效
     */
    Q_INVOKABLE QString isValid(const int label_class_id, const QString &name, const QString &color,
                                const QString &shortcut, const int ordinal_index) const;

    /**
     * @brief 重新排序标签类别
     * @param label_class_id 要移动的标签类别ID
     * @param new_ordinal_index 新的排序索引位置
     * @return 是否成功
     */
    Q_INVOKABLE bool reorderLabelClass(const int64_t label_class_id, const int64_t new_ordinal_index);
    Q_INVOKABLE bool updateLabelClassGroup(const int64_t label_class_id, const QString &group);

    /**
     * @brief 设置数据操作期间的写入阻断状态。
     * @param blocked 是否阻断直接写入。
     */
    void setMutationBlocked(bool blocked)
    {
        mutation_blocked_ = blocked;
    }

    /**
     * @brief 根据快捷键查找标签类别
     * @param shortcut 快捷键（不区分大小写）
     * @return 匹配的标签类别索引，未找到返回 -1；多个匹配时返回 ordinal_index 最小的
     */
    Q_INVOKABLE int findByShortcut(const QString &shortcut) const;

    /**
     * @brief 根据快捷键选中标签类别
     * @param shortcut 快捷键（不区分大小写）
     * @return 是否成功选中
     */
    Q_INVOKABLE bool selectByShortcut(const QString &shortcut);

private:
    void init();

    int getLabelClassId(const QModelIndex &index) const;

    QVariant getLabelClassName(const QModelIndex &index) const;
    QVariant getLabelClassColor(const QModelIndex &index) const;
    QVariant getLabelClassShortcut(const QModelIndex &index) const;
    QVariant getLabelClassGroup(const QModelIndex &index) const;
    QVariant getLabelClassGroupName(const QModelIndex &index) const;
    QVariant getLabelClassOrdinalIndex(const QModelIndex &index) const;
    QVariant getLabelClassSelected(const QModelIndex &index) const;

    void updateSelection(const QItemSelection &selected, const QItemSelection &deselected);

    dltool::database::ProjectDataBase *database_{nullptr};

    std::map<int64_t, LabelClass *> label_classes_;

    QItemSelectionModel *selection_{nullptr};
    bool                 mutation_blocked_{false};

signals:
    void currentLabelClassChanged();
};

} // namespace dltool::data
