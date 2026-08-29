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
 * 为 QML 评估九宫格和瀑布流视图提供带裁剪的原图特写或模型坐标热力图。
 * 请求 ID 包含预测产物快照、图像文件路径以及裁剪矩形参数。
 * 将预测快照融入请求 ID，使得预测产物更新时 Qt 内部缓存与本提供器的内存缓存能够自然失效与刷新。
 * 热力图直接读取评估使用的原始分数 TIFF；真值（GT）与预测（Pred）图元叠加由 QML 端渲染。
 */
class MODEL_API EvaluationThumbnailImageProvider final : public QQuickImageProvider
{
public:
    EvaluationThumbnailImageProvider();

    /**
     * @brief 响应 QML 图像加载请求并生成缩略图。
     * @param id 请求标识符（包含预测快照、路径及裁剪参数）。
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
