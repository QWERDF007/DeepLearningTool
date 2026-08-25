#include "data/DatasetSplitter.h"

#include "core/CoreDef.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>
#include <random>
#include <set>

namespace dltool::data {

namespace {

constexpr double kRatioTolerance = 1e-9;

struct Group
{
    std::vector<DatasetSplitItem> items;
};

QString splitKey(const DatasetSplitItem &item, const int method)
{
    if (method == core::DeepLearningMethod::Classification
        || method == core::DeepLearningMethod::AnomalyDetection)
    {
        return QStringLiteral("image:") + QString::number(item.image_label_class_id);
    }

    std::set<int64_t> classes(item.label_class_ids.cbegin(), item.label_class_ids.cend());
    if (classes.empty())
        return QStringLiteral("labels:");

    QString key = QStringLiteral("labels:");
    bool    first = true;
    for (const int64_t class_id : classes)
    {
        if (!first)
            key += QLatin1Char(',');
        key += QString::number(class_id);
        first = false;
    }
    return key;
}

std::array<size_t, 3> allocateCounts(const size_t count, const DatasetSplitRatios &ratios)
{
    const std::array<double, 3> proportions{ratios.train, ratios.validation, ratios.test};
    std::array<size_t, 3>      result{};
    std::array<double, 3>      remainders{};
    size_t                     allocated = 0;

    for (size_t index = 0; index < result.size(); ++index)
    {
        const double exact = static_cast<double>(count) * proportions[index];
        result[index]      = static_cast<size_t>(std::floor(exact));
        remainders[index]  = exact - static_cast<double>(result[index]);
        allocated += result[index];
    }

    size_t remaining = count - allocated;
    while (remaining > 0)
    {
        size_t best = 0;
        for (size_t index = 1; index < remainders.size(); ++index)
        {
            if (remainders[index] > remainders[best] + kRatioTolerance)
                best = index;
        }
        ++result[best];
        remainders[best] = -1.0;
        --remaining;
    }
    return result;
}

void setError(QString *error, const QString &message)
{
    if (error != nullptr)
        *error = message;
}

} // namespace

bool DatasetSplitter::validateRatios(const DatasetSplitRatios &ratios, QString *error)
{
    if (!std::isfinite(ratios.train) || !std::isfinite(ratios.validation) || !std::isfinite(ratios.test))
    {
        setError(error, QStringLiteral("数据集划分比例必须是有限数字"));
        return false;
    }
    if (ratios.train < 0.0 || ratios.validation < 0.0 || ratios.test < 0.0)
    {
        setError(error, QStringLiteral("数据集划分比例不能为负数"));
        return false;
    }
    if (!ratios.use_validation && std::abs(ratios.validation) > kRatioTolerance)
    {
        setError(error, QStringLiteral("未启用验证集时，验证集比例必须为 0"));
        return false;
    }

    const double sum = ratios.train + ratios.test + (ratios.use_validation ? ratios.validation : 0.0);
    if (std::abs(sum - 1.0) > kRatioTolerance)
    {
        setError(error, QStringLiteral("训练集、验证集和测试集比例之和必须等于 1"));
        return false;
    }
    if (ratios.train <= 0.0 || ratios.test <= 0.0 || (ratios.use_validation && ratios.validation <= 0.0))
    {
        setError(error, QStringLiteral("启用的子集比例必须大于 0"));
        return false;
    }
    return true;
}

DatasetSplitResult DatasetSplitter::split(const std::vector<DatasetSplitItem> &items, const int method,
                                          const DatasetSplitRatios &ratios, const uint32_t random_seed)
{
    DatasetSplitResult result;
    if (!validateRatios(ratios, &result.error))
        return result;
    if (!core::DeepLearningMethod::isSupportedMethod(method))
    {
        result.error = QStringLiteral("当前项目类型不支持数据集划分");
        return result;
    }
    if (items.empty())
    {
        result.error = QStringLiteral("不能划分空数据集");
        return result;
    }

    std::map<QString, Group> groups;
    std::set<int64_t>        unique_image_ids;
    for (const DatasetSplitItem &item : items)
    {
        if (item.image_id < 0 || !unique_image_ids.insert(item.image_id).second)
        {
            result.error = QStringLiteral("数据集包含无效或重复的图像 ID");
            return result;
        }
        groups[splitKey(item, method)].items.push_back(item);
    }

    std::mt19937 random_engine(random_seed);
    for (auto &[key, group] : groups)
    {
        Q_UNUSED(key)
        std::shuffle(group.items.begin(), group.items.end(), random_engine);
        const std::array<size_t, 3> counts = allocateCounts(group.items.size(), ratios);
        size_t                       offset = 0;
        for (size_t index = 0; index < counts[0]; ++index)
            result.train_image_ids.push_back(group.items[offset++].image_id);
        for (size_t index = 0; index < counts[1]; ++index)
            result.validation_image_ids.push_back(group.items[offset++].image_id);
        for (size_t index = 0; index < counts[2]; ++index)
            result.test_image_ids.push_back(group.items[offset++].image_id);
    }

    // 数据库和 UI 都按图像 ID 的稳定顺序处理结果；随机性只决定成员归属。
    std::sort(result.train_image_ids.begin(), result.train_image_ids.end());
    std::sort(result.validation_image_ids.begin(), result.validation_image_ids.end());
    std::sort(result.test_image_ids.begin(), result.test_image_ids.end());
    result.success = true;
    return result;
}

} // namespace dltool::data
