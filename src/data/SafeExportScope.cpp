#include "data/DataIO.h"

#include "common/Utils.h"

#include <QDir>
#include <QFileInfo>

#include <utility>

namespace dltool::data {

SafeExportScope::SafeExportScope(const QString &target_dir, RenameDirectory rename_directory)
    : rename_directory_(rename_directory ? std::move(rename_directory)
                                        : RenameDirectory([](const QString &from, const QString &to)
                                                          { return QDir().rename(from, to); }))
    , target_dir_(common::cleanPath(target_dir))
{
    if (target_dir_.isEmpty())
    {
        error_ = QStringLiteral("目标目录为空");
        return;
    }

    const QFileInfo target_fi(target_dir_);
    const QDir      parent_dir = target_fi.dir();
    const QString   candidate_staging
        = parent_dir.filePath(QStringLiteral(".staging_%1_%2").arg(target_fi.fileName()).arg(common::uuid()));

    QString dir_err;
    if (common::ensureDirectory(candidate_staging, &dir_err))
    {
        staging_dir_ = candidate_staging;
        valid_       = true;
    }
    else
    {
        error_ = QStringLiteral("无法创建导出暂存目录: %1").arg(dir_err);
    }
}

SafeExportScope::~SafeExportScope()
{
    discard();
}

void SafeExportScope::discard()
{
    if (!published_ && !staging_dir_.isEmpty())
    {
        QDir(staging_dir_).removeRecursively();
    }
}

bool SafeExportScope::publish(QString &err_msg)
{
    if (!valid_)
    {
        err_msg = error_;
        return false;
    }

    if (published_)
        return true;

    if (!QDir(staging_dir_).exists())
    {
        err_msg = QStringLiteral("暂存目录不存在: %1").arg(staging_dir_);
        return false;
    }

    const QFileInfo target_fi(target_dir_);
    const QDir      parent_dir = target_fi.dir();

    if (!QFileInfo::exists(target_dir_))
    {
        if (!common::ensureDirectory(parent_dir.path(), &err_msg))
            return false;

        if (rename_directory_(staging_dir_, target_dir_))
        {
            published_ = true;
            return true;
        }

        err_msg = QStringLiteral("发布暂存导出失败: %1 -> %2").arg(staging_dir_, target_dir_);
        return false;
    }

    // Target directory already exists: back it up first to preserve original content if publish fails
    const QString backup_dir = parent_dir.filePath(
        QStringLiteral(".backup_%1_%2").arg(target_fi.fileName()).arg(common::uuid()));

    if (!rename_directory_(target_dir_, backup_dir))
    {
        err_msg = QStringLiteral("无法备份既有目标目录: %1").arg(target_dir_);
        return false;
    }

    // Now move staging_dir_ to target_dir_
    if (!rename_directory_(staging_dir_, target_dir_))
    {
        if (!rename_directory_(backup_dir, target_dir_))
        {
            err_msg = QStringLiteral("发布暂存导出失败，恢复原目标失败: %1；原内容保留在: %2")
                          .arg(target_dir_, backup_dir);
            return false;
        }
        err_msg = QStringLiteral("发布暂存导出失败，已恢复原目标内容: %1").arg(target_dir_);
        return false;
    }

    // Clean up backup directory and staging
    QDir(backup_dir).removeRecursively();
    QDir(staging_dir_).removeRecursively();
    published_ = true;
    return true;
}

} // namespace dltool::data
