#pragma once

#include "model/IParams.h"

#include <QString>
#include <vector>

namespace dltool::model {

struct ParamGroupDefinition
{
    QString                      name_en;
    QString                      name_cn;
    QString                      description;
    bool                         enabled{true};
    int                          part_index{0};
    std::vector<ParamDefinition> params;
};

ParamDefinition makeIntegerParam(const QString &name_en, const QString &name_cn, int default_value, int from, int to,
                                 int step, const QString &description = {});
ParamDefinition makeDoubleParam(const QString &name_en, const QString &name_cn, double default_value, double from,
                                double to, double step, const QString &description = {});
ParamDefinition makeSliderParam(const QString &name_en, const QString &name_cn, double default_value, double from,
                                double to, double step, const QString &description = {});
ParamDefinition makeCheckParam(const QString &name_en, const QString &name_cn, bool default_value,
                               const QString &description = {});
ParamDefinition makeComboParam(const QString &name_en, const QString &name_cn, const QString &default_value,
                               QStringList options, const QString &description = {});

} // namespace dltool::model
