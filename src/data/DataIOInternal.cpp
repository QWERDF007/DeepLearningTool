#include "DataIOInternal.h"

#include "data/DataNameUtils.h"
#include "data/DatasetIO.h"
#include "settings/GlobalSettings.h"
#include "settings/SettingsValue.h"

#include <QFileInfo>

#include <algorithm>
#include <cmath>

namespace dltool::data::detail {

namespace {

constexpr double kDefaultMaskImportPolygonApproxRatio = 0.01;
constexpr int    kDefaultDataIOThreads                = 4;
constexpr int    kMaxDataIOThreads                    = 16;

} // namespace

QStringList imageNameFilters()
{
    return {
        "*.jpg", "*.jpeg", "*.png", "*.bmp", "*.gif", "*.tiff", "*.tif", "*.webp",
        "*.JPG", "*.JPEG", "*.PNG", "*.BMP", "*.GIF", "*.TIFF", "*.TIF", "*.WEBP",
    };
}

int dataIOThreadCount()
{
    namespace generated_field = dltool::settings::generated::field;

    const int configured = dltool::settings::settingInt(dltool::settings::GlobalSettings::getInstance(),
                                                        generated_field::Data::DataIoThreads, kDefaultDataIOThreads);
    return std::clamp(configured, 1, kMaxDataIOThreads);
}

double normalizedMaskImportPolygonApproxRatio(const double ratio)
{
    return std::isfinite(ratio) ? std::max(0.0, ratio) : kDefaultMaskImportPolygonApproxRatio;
}

double maskImportPolygonApproxRatio()
{
    namespace generated_field = dltool::settings::generated::field;

    return normalizedMaskImportPolygonApproxRatio(dltool::settings::settingDouble(
        dltool::settings::GlobalSettings::getInstance(), generated_field::Data::PolygonApproxEpsilonRatio,
        kDefaultMaskImportPolygonApproxRatio));
}

QString uniqueImageName(const QString &source_path, int64_t stable_id, const std::map<QString, int> &used_names,
                        const std::map<QString, int> &used_stems)
{
    const QFileInfo file_info(source_path);
    const QString   suffix = file_info.suffix().isEmpty() ? QString() : QString(".%1").arg(file_info.suffix());
    const QString   stem
        = file_info.completeBaseName().isEmpty() ? QString::number(stable_id) : file_info.completeBaseName();
    QString candidate = QString("%1%2").arg(stem, suffix);
    if (used_names.find(candidate) == used_names.end() && used_stems.find(stem) == used_stems.end())
        return candidate;

    QString candidate_stem = QString("%1_%2").arg(stem).arg(stable_id);
    candidate              = QString("%1%2").arg(candidate_stem, suffix);
    int index              = 1;
    while (used_names.find(candidate) != used_names.end() || used_stems.find(candidate_stem) != used_stems.end())
    {
        candidate_stem = QString("%1_%2_%3").arg(stem).arg(stable_id).arg(index++);
        candidate      = QString("%1%2").arg(candidate_stem, suffix);
    }
    return candidate;
}

void addScannedLabelClass(std::map<QString, QString> &label_class_info, const QString &raw_name, int &color_index)
{
    const QString name = sanitizeName(raw_name);
    if (name.isEmpty() || label_class_info.find(name) != label_class_info.end())
        return;

    label_class_info[name] = DatasetIO::generateDefaultColor(color_index++);
}

} // namespace dltool::data::detail
