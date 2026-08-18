#include "model/ModelParamDefs.h"

#include "parameter/ParameterSchema.h"

#include <utility>

namespace dltool::model {

namespace {

ParamDefinition makeParameter(const QString &name_en, const QString &name_cn, const QVariant &default_value,
                              const QString &value_type, const QString &display_type, QVariantList value_range,
                              const QString &description)
{
    ParamDefinition param;
    param.name_en       = name_en;
    param.name_cn       = name_cn;
    param.description   = description;
    param.value         = default_value;
    param.default_value = default_value;
    param.value_type    = value_type;
    param.value_range   = std::move(value_range);
    param.display_type  = display_type;
    return param;
}

} // namespace

ParamDefinition makeIntegerParam(const QString &name_en, const QString &name_cn, const int default_value,
                                 const int from, const int to, const int step, const QString &description)
{
    return makeParameter(name_en, name_cn, default_value, QStringLiteral("int"), QStringLiteral("spin"),
                         QVariantList{from, to, step}, description);
}

ParamDefinition makeDoubleParam(const QString &name_en, const QString &name_cn, const double default_value,
                                const double from, const double to, const double step, const QString &description)
{
    return makeParameter(name_en, name_cn, default_value, QStringLiteral("double"), QStringLiteral("spin"),
                         QVariantList{from, to, step}, description);
}

ParamDefinition makeSliderParam(const QString &name_en, const QString &name_cn, const double default_value,
                                const double from, const double to, const double step, const QString &description)
{
    ParamDefinition param = makeDoubleParam(name_en, name_cn, default_value, from, to, step, description);
    param.display_type    = QStringLiteral("slider");
    return param;
}

ParamDefinition makeCheckParam(const QString &name_en, const QString &name_cn, const bool default_value,
                               const QString &description)
{
    return makeParameter(name_en, name_cn, default_value, QStringLiteral("bool"), QStringLiteral("checkbox"), {},
                         description);
}

ParamDefinition makeComboParam(const QString &name_en, const QString &name_cn, const QString &default_value,
                               QVariantList options, const QString &description)
{
    ParamDefinition param = makeParameter(name_en, name_cn, default_value, QStringLiteral("string"),
                                          QStringLiteral("combo"), {}, description);
    for (const QVariant &option : options) param.options.append(option);
    return param;
}

ParamDefinition makeDynamicParam(const QString &name_en, const QString &name_cn, const QVariant &default_value,
                                 const QString &display_type, const QString &backend_key, const QString &description)
{
    const QString   resolved_display_type = display_type.isEmpty() ? QStringLiteral("combo") : display_type;
    ParamDefinition param                 = makeParameter(name_en, name_cn, default_value, QStringLiteral("string"),
                                                          resolved_display_type, {}, description);
    param.backend_key                     = backend_key;
    param.kind                            = ParamKind::Dynamic;
    dltool::parameter::resolveParameterOptions(param);
    return param;
}

} // namespace dltool::model
