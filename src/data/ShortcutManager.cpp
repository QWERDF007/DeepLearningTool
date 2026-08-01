#include "data/ShortcutManager.h"

#include "data/ImageTags.h"
#include "data/LabelClasses.h"

namespace dltool::data {

ShortcutManager::ShortcutManager(LabelClassesListModel *label_classes, ImageTagsListModel *image_tags,
                                 QObject *parent)
    : QObject(parent)
    , label_classes_(label_classes)
    , image_tags_(image_tags)
{
}

QString ShortcutManager::normalizedShortcut(const QString &shortcut)
{
    return shortcut.trimmed();
}

QString ShortcutManager::validateFormat(const QString &shortcut, const QString &owner_name) const
{
    const QString normalized = normalizedShortcut(shortcut);
    if (normalized.isEmpty())
    {
        return {};
    }
    if (normalized.size() != 1 || !normalized.front().isPrint())
    {
        return QString("error:%1快捷键只能是单个可打印字符").arg(owner_name);
    }
    return {};
}

QString ShortcutManager::validateLabelShortcut(const QString &shortcut, const int64_t label_class_id) const
{
    const QString format_error = validateFormat(shortcut, QString("类别"));
    if (!format_error.isEmpty())
    {
        return format_error;
    }

    const QString normalized = normalizedShortcut(shortcut);
    if (normalized.isEmpty())
    {
        return {};
    }

    const int64_t existing_label_id = findLabelClassId(normalized);
    if (existing_label_id >= 0 && existing_label_id != label_class_id)
    {
        return QString("error:类别快捷键已被其他类别使用");
    }
    if (findTagId(normalized) >= 0)
    {
        return QString("error:类别快捷键已被 Tag 使用");
    }
    return {};
}

QString ShortcutManager::validateTagShortcut(const QString &shortcut, const int64_t tag_id) const
{
    const QString format_error = validateFormat(shortcut, QStringLiteral("Tag"));
    if (!format_error.isEmpty())
    {
        return format_error;
    }

    const QString normalized = normalizedShortcut(shortcut);
    if (normalized.isEmpty())
    {
        return {};
    }

    const int64_t existing_tag_id = findTagId(normalized);
    if (existing_tag_id >= 0 && existing_tag_id != tag_id)
    {
        return QString("error:Tag 快捷键已被其他 Tag 使用");
    }
    if (findLabelClassId(normalized) >= 0)
    {
        return QString("error:Tag 快捷键已被类别使用");
    }
    return {};
}

int64_t ShortcutManager::findLabelClassId(const QString &shortcut) const
{
    return label_classes_ ? label_classes_->findIdByShortcut(normalizedShortcut(shortcut)) : -1;
}

int64_t ShortcutManager::findTagId(const QString &shortcut) const
{
    return image_tags_ ? image_tags_->findByShortcut(normalizedShortcut(shortcut)) : -1;
}

} // namespace dltool::data
