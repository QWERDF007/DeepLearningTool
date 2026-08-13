#include "model/ModelSelfWeightOptions.h"

#include "model/ModelStorageService.h"
#include "parameter/DynamicOptions.h"

#include <QDir>
#include <QFileInfo>
#include <QMetaType>
#include <QStringList>

namespace dltool::model {

namespace {

constexpr const char *kProviderKey     = "model.checkpoints";
constexpr const char *kContextModel    = "model_name";
constexpr const char *kContextProject  = "project_dir";
constexpr const char *kContextExts     = "extensions";

QStringList contextExtensions(const QVariantMap &context)
{
    QStringList extensions;
    const QVariant value = context.value(QString::fromLatin1(kContextExts));
    if (value.userType() == QMetaType::QStringList || value.userType() == QMetaType::QVariantList)
    {
        for (const QVariant &entry : value.toList())
            extensions.append(entry.toString());
    }
    if (extensions.isEmpty())
        extensions = {QStringLiteral("pt")};
    return extensions;
}

class ModelSelfWeightProvider final : public parameter::DynamicOptionsProvider
{
public:
    QString key() const override { return QString::fromLatin1(kProviderKey); }

    parameter::DynamicOptionsData query(const QVariantMap &context) const override
    {
        parameter::DynamicOptionsData result;

        const QString project_dir = context.value(QString::fromLatin1(kContextProject)).toString().trimmed();
        const QString model_name  = context.value(QString::fromLatin1(kContextModel)).toString().trimmed();
        if (project_dir.isEmpty() || model_name.isEmpty())
            return result;

        const ModelStorageService storage(project_dir);
        const QDir                dir(storage.trainWeightsPath(model_name));
        if (!dir.exists())
            return result;

        const QStringList extensions = contextExtensions(context);
        const QFileInfoList entries  = dir.entryInfoList(QDir::Files, QDir::Name);
        for (const QFileInfo &entry : entries)
        {
            const QString suffix = entry.suffix().toLower();
            if (extensions.contains(QStringLiteral(".") + suffix, Qt::CaseInsensitive))
                result.options.push_back({entry.completeBaseName(), entry.absoluteFilePath()});
        }
        return result;
    }
};

[[maybe_unused]] const bool providerRegistered = []
{
    registerModelSelfWeightOptionsProvider();
    return true;
}();

} // namespace

void registerModelSelfWeightOptionsProvider()
{
    parameter::registerDynamicOptionsProvider(std::make_unique<ModelSelfWeightProvider>());
}

} // namespace dltool::model
