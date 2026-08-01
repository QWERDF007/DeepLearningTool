#include "common/YamlUtils.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QMetaType>
#include <QSaveFile>
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
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
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

std::string toYamlString(const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
}

YAML::Node scalarNode(const QString &value)
{
    return YAML::Node(toYamlString(value));
}

YAML::Node variantToYaml(const QVariant &value)
{
    if (!value.isValid() || value.isNull())
    {
        return {};
    }

    const int type_id = value.typeId();
    if (type_id == QMetaType::QVariantMap)
    {
        YAML::Node        node(YAML::NodeType::Map);
        const QVariantMap map = value.toMap();
        for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        {
            node[toYamlString(it.key())] = variantToYaml(it.value());
        }
        return node;
    }

    if (type_id == QMetaType::QVariantHash)
    {
        YAML::Node         node(YAML::NodeType::Map);
        const QVariantHash hash = value.toHash();
        for (auto it = hash.constBegin(); it != hash.constEnd(); ++it)
        {
            node[toYamlString(it.key())] = variantToYaml(it.value());
        }
        return node;
    }

    if (type_id == QMetaType::QVariantList)
    {
        YAML::Node         node(YAML::NodeType::Sequence);
        const QVariantList list = value.toList();
        for (const QVariant &entry : list)
        {
            node.push_back(variantToYaml(entry));
        }
        return node;
    }

    if (type_id == QMetaType::QStringList)
    {
        YAML::Node        node(YAML::NodeType::Sequence);
        const QStringList list = value.toStringList();
        for (const QString &entry : list)
        {
            node.push_back(scalarNode(entry));
        }
        return node;
    }

    switch (type_id)
    {
    case QMetaType::Bool:
        return YAML::Node(value.toBool());
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return YAML::Node(value.toLongLong());
    case QMetaType::Double:
    case QMetaType::Float:
        return YAML::Node(value.toDouble());
    default:
        return scalarNode(value.toString());
    }
}

void setMapValue(YAML::Node &node, const QString &key, const YAML::Node &value)
{
    node[toYamlString(key)] = value;
}

void setMapValue(YAML::Node &node, const QString &key, const QString &value)
{
    node[toYamlString(key)] = scalarNode(value);
}

void setMapValue(YAML::Node &node, const QString &key, int value)
{
    node[toYamlString(key)] = value;
}

void setMapValue(YAML::Node &node, const QString &key, qint64 value)
{
    node[toYamlString(key)] = static_cast<long long>(value);
}

void setMapValue(YAML::Node &node, const QString &key, bool value)
{
    node[toYamlString(key)] = value;
}

YAML::Node loadFile(const QFileInfo &file)
{
    QFile yaml_file(file.absoluteFilePath());
    if (!yaml_file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        const QString message
            = QStringLiteral("open yaml failed: %1, %2").arg(file.absoluteFilePath(), yaml_file.errorString());
        throw std::runtime_error(message.toUtf8().constData());
    }

    const QByteArray content = yaml_file.readAll();
    return YAML::Load(std::string(content.constData(), static_cast<size_t>(content.size())));
}

bool writeFile(const QString &path, const YAML::Node &node, QString *err_msg, const QString &open_error_prefix,
               const QString &emit_error_prefix)
{
    QFile yaml_file(path);
    if (!yaml_file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        if (err_msg != nullptr)
        {
            const QString prefix
                = open_error_prefix.isEmpty() ? QStringLiteral("write yaml failed") : open_error_prefix;
            *err_msg = QStringLiteral("%1: %2, %3").arg(prefix, path, yaml_file.errorString());
        }
        return false;
    }

    YAML::Emitter out;
    out << node;
    if (!out.good())
    {
        if (err_msg != nullptr)
        {
            const QString prefix = emit_error_prefix.isEmpty() ? QStringLiteral("emit yaml failed") : emit_error_prefix;
            *err_msg             = QStringLiteral("%1: %2").arg(prefix, QString::fromUtf8(out.GetLastError().c_str()));
        }
        return false;
    }

    yaml_file.write(out.c_str());
    yaml_file.write("\n");
    return true;
}

bool writeFileAtomic(const QString &path, const YAML::Node &node, QString *err_msg,
                     const QString &open_error_prefix, const QString &emit_error_prefix,
                     const QString &commit_error_prefix)
{
    YAML::Emitter out;
    out << node;
    if (!out.good())
    {
        if (err_msg != nullptr)
        {
            const QString prefix = emit_error_prefix.isEmpty() ? QStringLiteral("emit yaml failed") : emit_error_prefix;
            *err_msg             = QStringLiteral("%1: %2").arg(prefix, QString::fromUtf8(out.GetLastError().c_str()));
        }
        return false;
    }

    QSaveFile yaml_file(path);
    if (!yaml_file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (err_msg != nullptr)
        {
            const QString prefix
                = open_error_prefix.isEmpty() ? QStringLiteral("write yaml failed") : open_error_prefix;
            *err_msg = QStringLiteral("%1: %2, %3").arg(prefix, path, yaml_file.errorString());
        }
        return false;
    }

    const QByteArray content = QByteArray(out.c_str()) + QByteArrayLiteral("\n");
    if (yaml_file.write(content) != content.size())
    {
        if (err_msg != nullptr)
            *err_msg = QString("写入 YAML 临时文件失败: %1, %2").arg(path, yaml_file.errorString());
        yaml_file.cancelWriting();
        return false;
    }
    if (!yaml_file.commit())
    {
        if (err_msg != nullptr)
        {
            const QString prefix
                = commit_error_prefix.isEmpty() ? QString("提交 yaml 失败") : commit_error_prefix;
            *err_msg = QStringLiteral("%1: %2, %3").arg(prefix, path, yaml_file.errorString());
        }
        return false;
    }
    return true;
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
