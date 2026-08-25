#pragma once

#include "dltool/model/Export.h"

#include <QImage>
#include <QMargins>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QVariantMap>

namespace dltool::model {

/**
 * @brief 异常检测推理预处理的确定性空间变换。
 *
 * 变换顺序与模型推理一致：原图 -> resize -> padding -> crop -> 模型/分数图。
 * 热力图底图和模型坐标到原图坐标的逆变换必须复用该对象，避免两套公式产生偏移。
 */
class MODEL_API AnomalyPreprocessingTransform
{
public:
    static AnomalyPreprocessingTransform fromConfig(const QSize &source_size, const QSize &model_size,
                                                     const QVariantMap &preprocessing_config);

    bool     isValid() const;
    QSize    sourceSize() const;
    QSize    resizedSize() const;
    QSize    paddedSize() const;
    QSize    modelSize() const;
    QMargins padding() const;
    QRect    cropRect() const;

    QPointF imageToModel(const QPointF &point) const;
    QPointF modelToImage(const QPointF &point) const;
    QImage  applyToImage(const QImage &source) const;

private:
    QSize    source_size_;
    QSize    resized_size_;
    QSize    padded_size_;
    QSize    model_size_;
    QMargins padding_;
    QRect    crop_rect_;
    bool     valid_{false};
};

} // namespace dltool::model
