#pragma once

#include "dltool/feature/Export.h"

#include <QString>
#include <QVariantMap>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dltool::feature {

class FEATURE_API ImageSearchDataProvider
{
public:
    enum class ImageClusterApplyMode
    {
        Move = 0,
        Copy = 1,
    };

    struct ImageClusterAssignment
    {
        int64_t image_id{0};
        int64_t cluster_id{-1};
        double  probability{0.0};
    };

    struct ImageClusterApplyResult
    {
        size_t moved_image_count{0};
        size_t copied_image_count{0};
        size_t target_dataset_count{0};
        size_t skipped_noise_count{0};
    };

public:
    virtual ~ImageSearchDataProvider() = default;

    virtual std::vector<int64_t> selectedImageIds() const = 0;

    virtual std::vector<int64_t> allImageIds() const = 0;

    virtual QString imagePath(int64_t image_id) const = 0;

    virtual int64_t imageDatasetId(int64_t image_id) const = 0;

    virtual QString databasePath() const = 0;

    virtual std::vector<int64_t> allLabelIds() const = 0;

    virtual int64_t labelImageId(int64_t label_id) const = 0;

    virtual QVariantMap labelData(int64_t label_id) const = 0;

    virtual void clearImageSearchResults() = 0;

    virtual void setImageSearchResults(const std::vector<int64_t> &image_ids, bool enable_filter) = 0;

    virtual void clearLabelSearchResults() = 0;

    virtual void setLabelSearchResults(const std::vector<int64_t> &label_ids, bool enable_filter) = 0;

    virtual bool applyImageClusterAssignments(const std::vector<ImageClusterAssignment> &assignments,
                                              bool include_noise,
                                              ImageClusterApplyMode apply_mode,
                                              ImageClusterApplyResult &result,
                                              QString &err_msg) = 0;
};

} // namespace dltool::feature
