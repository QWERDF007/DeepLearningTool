#include "model/ModelParamDefs.h"

#include <utility>

namespace dltool::model {

ParamDefinition makeIntegerParam(const QString &name_en, const QString &name_cn, const int default_value,
                                 const int from, const int to, const int step,
                                 const QString &description)
{
    ParamDefinition param;
    param.name_en       = name_en;
    param.name_cn       = name_cn;
    param.description   = description;
    param.value         = default_value;
    param.default_value = default_value;
    param.value_type    = QStringLiteral("int");
    param.value_range   = QVariantList{from, to, step};
    param.control_type  = QStringLiteral("spin");
    return param;
}

ParamDefinition makeDoubleParam(const QString &name_en, const QString &name_cn, const double default_value,
                                const double from, const double to, const double step, const QString &description)
{
    ParamDefinition param;
    param.name_en       = name_en;
    param.name_cn       = name_cn;
    param.description   = description;
    param.value         = default_value;
    param.default_value = default_value;
    param.value_type    = QStringLiteral("double");
    param.value_range   = QVariantList{from, to, step};
    param.control_type  = QStringLiteral("spin");
    return param;
}

ParamDefinition makeSliderParam(const QString &name_en, const QString &name_cn, const double default_value,
                                const double from, const double to, const double step, const QString &description)
{
    ParamDefinition param = makeDoubleParam(name_en, name_cn, default_value, from, to, step, description);
    param.control_type    = QStringLiteral("slider");
    return param;
}

ParamDefinition makeCheckParam(const QString &name_en, const QString &name_cn, const bool default_value,
                               const QString &description)
{
    ParamDefinition param;
    param.name_en       = name_en;
    param.name_cn       = name_cn;
    param.description   = description;
    param.value         = default_value;
    param.default_value = default_value;
    param.value_type    = QStringLiteral("bool");
    param.control_type  = QStringLiteral("checkbox");
    return param;
}

ParamDefinition makeComboParam(const QString &name_en, const QString &name_cn, const QString &default_value,
                               QStringList options, const QString &description)
{
    ParamDefinition param;
    param.name_en       = name_en;
    param.name_cn       = name_cn;
    param.description   = description;
    param.value         = default_value;
    param.default_value = default_value;
    param.value_type    = QStringLiteral("string");
    param.control_type  = QStringLiteral("combo");
    param.options       = std::move(options);
    return param;
}

} // namespace dltool::model
