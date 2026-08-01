#pragma once

#include "dltool/model/Export.h"

#include <QCache>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

namespace dltool::model {

/**
 * @brief Serves cropped evaluation thumbnails for the QML evaluation grid.
 *
 * The request id contains the result revision, image path and crop rectangle.
 * Keeping the revision in the id makes Qt's image cache and this provider's
 * small in-memory cache naturally invalidate after a rerun or re-evaluation.
 * The provider only loads/crops the base image; GT/PRED overlays remain a QML
 * concern.
 */
class MODEL_API EvaluationThumbnailImageProvider final : public QQuickImageProvider
{
public:
    EvaluationThumbnailImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QImage loadImage(const QString &id, const QSize &requestedSize) const;

    mutable QMutex mutex_;
    mutable QCache<QString, QImage> cache_;
};

} // namespace dltool::model
