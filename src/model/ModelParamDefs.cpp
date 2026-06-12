#include "model/ModelParamDefs.h"

#include <utility>

namespace dltool::model {

ParamDefinition makeIntegerParam(const QString &key, const QString &label, const int default_value,
                                 const int minimum_value, const int maximum_value, const int step_value,
                                 const QString &description)
{
    ParamDefinition param;
    param.key           = key;
    param.label         = label;
    param.description   = description;
    param.editor_type   = ParamEditorType::Integer;
    param.value         = default_value;
    param.default_value = default_value;
    param.minimum_value = minimum_value;
    param.maximum_value = maximum_value;
    param.step_value    = step_value;
    return param;
}

ParamDefinition makeDoubleParam(const QString &key, const QString &label, const double default_value,
                                const double minimum_value, const double maximum_value, const double step_value,
                                const int decimals, const QString &description)
{
    ParamDefinition param;
    param.key           = key;
    param.label         = label;
    param.description   = description;
    param.editor_type   = ParamEditorType::Double;
    param.value         = default_value;
    param.default_value = default_value;
    param.minimum_value = minimum_value;
    param.maximum_value = maximum_value;
    param.step_value    = step_value;
    param.decimals      = decimals;
    return param;
}

ParamDefinition makeSliderParam(const QString &key, const QString &label, const double default_value,
                                const double minimum_value, const double maximum_value, const double step_value,
                                const int decimals, const QString &description)
{
    ParamDefinition param = makeDoubleParam(key, label, default_value, minimum_value, maximum_value, step_value,
                                            decimals, description);
    param.editor_type     = ParamEditorType::Slider;
    return param;
}

ParamDefinition makeCheckParam(const QString &key, const QString &label, const bool default_value,
                               const QString &description)
{
    ParamDefinition param;
    param.key           = key;
    param.label         = label;
    param.description   = description;
    param.editor_type   = ParamEditorType::CheckBox;
    param.value         = default_value;
    param.default_value = default_value;
    return param;
}

ParamDefinition makeComboParam(const QString &key, const QString &label, const QString &default_value,
                               QStringList options, const QString &description)
{
    ParamDefinition param;
    param.key           = key;
    param.label         = label;
    param.description   = description;
    param.editor_type   = ParamEditorType::ComboBox;
    param.value         = default_value;
    param.default_value = default_value;
    param.options       = std::move(options);
    return param;
}

} // namespace dltool::model
