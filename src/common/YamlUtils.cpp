#include "common/YamlUtils.h"

#include <QDir>
#include <QFile>
#include <algorithm>
#include <stdexcept>
#include <string>

namespace dltool::common::yaml {

QString nodeString(const YAML::Node &node, const QString &fallback)
{
    if (!node || node.IsNull())
    {
        return fallback;
    }

    const std::string value = node.as<std::string>();
    return QString::fromUtf8(value.c_str());
}

QVariant nodeVariant(const YAML::Node &node)
{
    if (!node || node.IsNull())
    {
        return {};
    }

    if (node.IsScalar())
    {
        const QString text  = nodeString(node);
        const QString lower = text.toLower();
        if (lower == QStringLiteral("true"))
        {
            return true;
        }
        if (lower == QStringLiteral("false"))
        {
            return false;
        }

        bool            ok      = false;
        const qlonglong integer = text.toLongLong(&ok);
        if (ok)
        {
            return integer;
        }

        const double floating = text.toDouble(&ok);
        if (ok)
        {
            return floating;
        }

        return text;
    }

    if (node.IsSequence())
    {
        QVariantList list;
        for (const YAML::Node &entry : node)
        {
            list.append(nodeVariant(entry));
        }
        return list;
    }

    if (node.IsMap())
    {
        QVariantMap map;
        for (auto it = node.begin(); it != node.end(); ++it)
        {
            map.insert(nodeString(it->first), nodeVariant(it->second));
        }
        return map;
    }

    return {};
}


YAML::Node loadFile(const QFileInfo &file)
{
    QFile yaml_file(file.absoluteFilePath());
    if (!yaml_file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        const QString message = QStringLiteral("open yaml failed: %1, %2").arg(file.absoluteFilePath(),
                                                                               yaml_file.errorString());
        throw std::runtime_error(message.toUtf8().constData());
    }

    const QByteArray content = yaml_file.readAll();
    return YAML::Load(std::string(content.constData(), static_cast<size_t>(content.size())));
}

QFileInfo findConfigFile(const QString &directory, const QString &base_name, const QStringList &suffixes)
{
    const QDir config_dir(directory);
    if (!config_dir.exists())
    {
        return {};
    }

    for (const QString &suffix : suffixes)
    {
        const QFileInfo info(config_dir.filePath(base_name + suffix));
        if (info.exists() && info.isFile())
        {
            return info;
        }
    }
    return {};
}

QVector<QFileInfo> configFiles(const QStringList &directories, const QStringList &name_filters)
{
    QVector<QFileInfo> files;
    for (const QString &path : directories)
    {
        const QDir dir(path);
        if (!dir.exists())
        {
            continue;
        }

        const QFileInfoList entries = dir.entryInfoList(name_filters, QDir::Files, QDir::Name);
        for (const QFileInfo &entry : entries)
        {
            const bool exists = std::any_of(files.cbegin(), files.cend(),
                                            [&entry](const QFileInfo &file)
                                            {
                                                return file.canonicalFilePath() == entry.canonicalFilePath()
                                                    || file.absoluteFilePath() == entry.absoluteFilePath();
                                            });
            if (!exists)
            {
                files.append(entry);
            }
        }
    }
    return files;
}

} // namespace dltool::common::yaml
