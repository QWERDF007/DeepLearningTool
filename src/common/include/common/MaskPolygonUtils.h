#pragma once

#include "dltool/common/Export.h"

#include <QPointF>
#include <cstdint>
#include <vector>

namespace dltool::common {

COMMON_API double polygonArea(const std::vector<QPointF> &points);

COMMON_API std::vector<QPointF> normalizePolygon(std::vector<QPointF> points);

COMMON_API std::vector<std::vector<QPointF>> maskToPolygons(const std::vector<uint8_t> &mask, int width, int height,
                                                            bool   keep_max                     = true,
                                                            double polygon_approx_epsilon_ratio = 0.0);

} // namespace dltool::common
