#pragma once

#include <QObject>
#include <QString>
#include <QtQml>
#include <cstdint>

namespace dltool::data {

class ImageTagsListModel;
class LabelClassesListModel;

/**
 * Project-wide registry for user-assignable single-character shortcuts.
 * Label classes and Tags share one namespace, so a key can only belong to one
 * item in either model.
 */
class ShortcutManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ShortcutManager)
    QML_UNCREATABLE("ShortcutManager is owned by DataManager")

public:
    explicit ShortcutManager(LabelClassesListModel *label_classes, ImageTagsListModel *image_tags,
                             QObject *parent = nullptr);

    Q_INVOKABLE QString validateLabelShortcut(const QString &shortcut, int64_t label_class_id = -1) const;
    Q_INVOKABLE QString validateTagShortcut(const QString &shortcut, int64_t tag_id = -1) const;
    Q_INVOKABLE int64_t findLabelClassId(const QString &shortcut) const;
    Q_INVOKABLE int64_t findTagId(const QString &shortcut) const;

    static QString normalizedShortcut(const QString &shortcut);

private:
    QString validateFormat(const QString &shortcut, const QString &owner_name) const;

    LabelClassesListModel *label_classes_{nullptr};
    ImageTagsListModel     *image_tags_{nullptr};
};

} // namespace dltool::data
