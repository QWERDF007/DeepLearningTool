#pragma once

#include "dltool/data/Export.h"

#include <QImage>
#include <QQuickImageProvider>
#include <QSize>
#include <QString>

namespace dltool::data {

class ImageInstancesListModel;

class DATA_API ImageInstanceImageProvider : public QQuickImageProvider
{
public:
    explicit ImageInstanceImageProvider(ImageInstancesListModel *image_instances);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QImage loadThumbnail(int64_t image_id, const QSize &requested_size) const;
    QImage createEmptyImage() const;
    QImage createErrorPlaceholder(const QSize &requested_size) const;

    ImageInstancesListModel *image_instances_{nullptr};
};

} // namespace dltool::data
