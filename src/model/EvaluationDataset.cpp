#include "model/EvaluationDataset.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>
#include <memory>

namespace dltool::model {

namespace {

/// 图像文件列表的文档大小上限。
constexpr qint64      kMaxEvaluationFileBytes = 256LL * 1024LL * 1024LL;
/// 图像文件列表的记录数量上限。
constexpr std::size_t kMaxEvaluationRecords = 5'000'000;

/**
 * @brief 解析 CSV 行（支持引号转义）。
 * @param line 输入行。
 * @param valid 输出解析有效性（引号未闭合视为无效），可为 nullptr。
 * @return 字段列表。
 */
QList<QString> parseCsvLine(const QString &line, bool *valid = nullptr)
{
    QList<QString> fields;
    QString        field;
    bool           quoted = false;
    for (int i = 0; i < line.size(); ++i)
    {
        const QChar c = line.at(i);
        if (c == QChar('"'))
        {
            if (quoted && i + 1 < line.size() && line.at(i + 1) == QChar('"'))
            {
                field += QChar('"');
                ++i;
            }
            else
            {
                quoted = !quoted;
            }
        }
        else if (c == QChar(',') && !quoted)
        {
            fields.push_back(field);
            field.clear();
        }
        else
        {
            field += c;
        }
    }
    fields.push_back(field);
    if (valid != nullptr)
        *valid = !quoted;
    return fields;
}

/**
 * @brief 判断协作取消令牌是否已被置位。
 * @param cancel 协作取消令牌，可为空。
 * @return 已置位返回 true。
 */
bool isCancelled(const std::shared_ptr<std::atomic_bool> &cancel)
{
    return cancel != nullptr && cancel->load(std::memory_order_relaxed);
}

} // namespace

bool readEvaluationImageList(const QString &path, QList<QPair<qint64, QString>> &rows,
                             const std::shared_ptr<std::atomic_bool> &cancel_token, QString *err_msg)
{
    rows.clear();
    const QFileInfo file_info(path);
    if (!file_info.exists() || !file_info.isFile())
    {
        if (err_msg)
            *err_msg = QString("图像文件列表不存在: %1").arg(path);
        return false;
    }
    if (file_info.size() > kMaxEvaluationFileBytes)
    {
        if (err_msg)
            *err_msg = QString("图像文件列表超过大小限制: %1").arg(path);
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (err_msg)
            *err_msg = QString("打开图像文件列表失败: %1").arg(file.errorString());
        return false;
    }
    QTextStream  stream(&file);
    QSet<qint64> ids;
    bool         first = true;
    while (!stream.atEnd())
    {
        if (isCancelled(cancel_token))
        {
            if (err_msg)
                *err_msg = QString("评估已取消");
            return false;
        }
        QString line = stream.readLine();
        if (line.trimmed().isEmpty())
            continue;
        if (first && !line.isEmpty() && line.at(0) == QChar(0xfeff))
            line.remove(0, 1);
        bool                 csv_valid = false;
        const QList<QString> fields    = parseCsvLine(line, &csv_valid);
        if (first && csv_valid && fields.size() == 2
            && fields.at(0).trimmed().compare(QString("image_id"), Qt::CaseInsensitive) == 0)
        {
            first = false;
            continue;
        }
        first = false;
        if (!csv_valid || fields.size() != 2)
        {
            if (err_msg)
                *err_msg = QString("图像文件列表行格式无效: %1").arg(line);
            return false;
        }
        bool          ok         = false;
        const qint64  image_id   = fields.at(0).trimmed().toLongLong(&ok);
        const QString image_path = fields.at(1).trimmed();
        if (!ok || image_id < 0 || image_path.isEmpty())
            continue;
        if (ids.contains(image_id))
            continue;
        ids.insert(image_id);
        rows.push_back({image_id, image_path});
        if (rows.size() > static_cast<int>(kMaxEvaluationRecords))
        {
            if (err_msg)
                *err_msg = QString("图像文件列表记录数量超过限制");
            return false;
        }
    }
    if (rows.isEmpty())
    {
        if (err_msg)
            *err_msg = QString("图像文件列表没有有效图像: %1").arg(path);
        return false;
    }
    return true;
}

} // namespace dltool::model
