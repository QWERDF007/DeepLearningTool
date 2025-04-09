#pragma once

#include <QAbstractListModel>
#include <QtQml>
#include <map>

namespace dltool::data {
class ProjectDataBase;
}

namespace dltool::project {

class LabelClass : public QObject
{
public:
    LabelClass(const int64_t id, const QString &name, const QString &color, const QString &shortcut,
               const int64_t ordinal_index, QObject *parent = nullptr);
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

private:
    int64_t id_;
    int64_t ordinal_index_;

    QString name_;
    QString color_;
    QString shortcut_;
};

class LabelClassesListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
public:
    LabelClassesListModel(data::ProjectDataBase *database, QObject *parent = nullptr);
    ~LabelClassesListModel();

    enum Role
    {
        LabelClassIdRole = Qt::UserRole + 1,
        NameRole,
        ColorRole,
        ShortcutRole,
        OrdinalIndexRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    QHash<int, QByteArray> roleNames() const override;

    bool addLabelClass(const QString &name, const QString &color, const QString &shortcut);
    bool updateLabelClass(const int64_t label_class_id, const QString &name, const QString &color,
                          const QString &shortcut, const int64_t ordinal_index);
    bool deleteLabelClass(const int64_t label_class_id);

    int     getLabelClassId(const QString &name) const;
    QString getLabelClassName(const int label_class_id) const;

private:
    void init();

    int getLabelClassId(const QModelIndex &index) const;

    QVariant getLabelClassName(const QModelIndex &index) const;
    QVariant getLabelClassColor(const QModelIndex &index) const;
    QVariant getLabelClassShortcut(const QModelIndex &index) const;

    data::ProjectDataBase *database_{nullptr};

    std::map<int64_t, LabelClass *> label_classes_;
};

} // namespace dltool::project
