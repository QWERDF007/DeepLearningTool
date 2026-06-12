#pragma once

#include "model/IParams.h"

#include <QString>

#include <vector>

namespace dltool::model {

struct ParamGroupDefinition
{
    QString                      key;
    QString                      label;
    QString                      description;
    bool                         enabled{true};
    std::vector<ParamDefinition> params;
};

ParamDefinition makeIntegerParam(const QString &key, const QString &label, int default_value,
                                 int minimum_value, int maximum_value, int step_value,
                                 const QString &description = {});
ParamDefinition makeDoubleParam(const QString &key, const QString &label, double default_value,
                                double minimum_value, double maximum_value, double step_value,
                                int decimals, const QString &description = {});
ParamDefinition makeSliderParam(const QString &key, const QString &label, double default_value,
                                double minimum_value, double maximum_value, double step_value,
                                int decimals, const QString &description = {});
ParamDefinition makeCheckParam(const QString &key, const QString &label, bool default_value,
                               const QString &description = {});
ParamDefinition makeComboParam(const QString &key, const QString &label, const QString &default_value,
                               QStringList options, const QString &description = {});

} // namespace dltool::model
