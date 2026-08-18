#pragma once

#include "dltool/model/Export.h"

#include <QCache>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

namespace dltool::model {

/**
 * @brief 评估缩略图图像提供器（QQuickImageProvider）。
 *
 * 为 QML 评估九宫格和瀑布流视图提供带裁剪的局部特写图像。
 * 请求 ID 包含结果修订版本号、图像文件路径以及裁剪矩形参数。
 * 将版本号融入请求 ID，使得在重新评测时 Qt 内部缓存与本提供器的内存缓存能够自然失效与刷新。
 * 本提供器只负责原图加载与几何区域裁剪，真值（GT）与预测（Pred）图元叠加由 QML 端渲染。
 */
class MODEL_API EvaluationThumbnailImageProvider final : public QQuickImageProvider
{
public:
    EvaluationThumbnailImageProvider();

    /**
     * @brief 响应 QML 图像加载请求并生成缩略图。
     * @param id 请求标识符（包含修订版本、路径及裁剪参数）。
     * @param size 输出生成的图像实际尺寸。
     * @param requestedSize QML 端请求的目标尺寸。
     * @return 裁剪后的 QImage 实例。
     */
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QImage loadImage(const QString &id, const QSize &requestedSize) const;

    mutable QMutex                  mutex_; ///< 缓存互斥锁。
    mutable QCache<QString, QImage> cache_; ///< 内存缩略图 LRU 缓存。
};

} // namespace dltool::model
