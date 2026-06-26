#pragma once

#include <QString>
#include <QVariantList>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace dltool::feature {

inline std::vector<int64_t> parseInt64Ids(const QVariantList &values, bool filterNonNegative = false,
                                          bool sortAndUnique = false)
{
    std::vector<int64_t> result;
    result.reserve(static_cast<size_t>(values.size()));
    for (const QVariant &value : values)
    {
        bool    ok = false;
        int64_t id = value.toLongLong(&ok);
        if (ok && (!filterNonNegative || id >= 0))
            result.push_back(id);
    }
    if (sortAndUnique)
    {
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
    }
    return result;
}

} // namespace dltool::feature
