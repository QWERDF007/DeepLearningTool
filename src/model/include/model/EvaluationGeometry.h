#pragma once

#include "dltool/model/Export.h"

#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace dltool::model {

/**
 * @brief 评估几何协议使用的轴对齐包围盒。
 */
struct MODEL_API EvaluationBox
{
    double x{0.0}; ///< 左上角 x 坐标。
    double y{0.0}; ///< 左上角 y 坐标。
    double w{0.0}; ///< 宽度。
    double h{0.0}; ///< 高度。

    /**
     * @brief 判断包围盒是否有效（宽高均大于 0）。
     * @return 有效返回 true。
     */
    bool valid() const
    {
        return w > 0.0 && h > 0.0;
    }
};

/**
 * @brief 将包围盒序列化为评估协议 bbox 映射（x/y/width/height）。
 * @param box 包围盒。
 * @return 协议映射。
 */
MODEL_API QVariantMap evaluationBoxMap(const EvaluationBox &box);

/**
 * @brief 读取几何记录的协议类型（小写）。
 * @param geometry 几何记录。
 * @return 类型名称；未声明时返回空字符串。
 */
MODEL_API QString evaluationGeometryType(const QVariantMap &geometry);

/**
 * @brief 规范化几何记录：补全 type/format/coordinate_system/bounds/values。
 *
 * 预测产物可能是简化记录（只有 points 或只有 bbox 数值），此函数将其
 * 统一为评估协议要求的完整形态。
 * @param source 原始几何记录。
 * @param box 该几何对应的包围盒（无效时跳过补全）。
 * @return 规范化后的几何记录。
 */
MODEL_API QVariantMap canonicalGeometry(const QVariantMap &source, const EvaluationBox &box);

/**
 * @brief 从任意几何/边界记录读取包围盒。
 *
 * 支持协议字段（values、x/y/width/height、cx/cy/width/height、points）
 * 以及嵌套 bounds/bbox 键，全部数值必须有限。
 * @param value 几何或边界记录。
 * @param box 输出包围盒。
 * @return 解析出有效包围盒返回 true。
 */
MODEL_API bool readBox(const QVariantMap &value, EvaluationBox &box);

/**
 * @brief 构造 mask 类型几何记录的本地文件 URL。
 * @param geometry 几何记录。
 * @param root 相对 artifact_path 的解析根目录。
 * @return 文件 URL；非 mask 类型或文件不存在时返回空。
 */
MODEL_API QString maskUrl(const QVariantMap &geometry, const QString &root);

/**
 * @brief 校验预测几何记录是否符合评估协议。
 *
 * 要求声明 coordinate_system=image_pixels；bbox 必须为 xywh 且位于图像
 * 边界内；polygon 至少 3 个有限点；mask 的 artifact_path 必须位于
 * task_root 内且文件存在。
 * @param geometry 预测几何记录。
 * @param image_width 图像宽度（<=0 时跳过边界检查）。
 * @param image_height 图像高度。
 * @param task_root mask artifact 允许的根目录。
 * @param err_msg 校验失败时输出错误信息，可为 nullptr。
 * @return 校验通过返回 true。
 */
MODEL_API bool validateGeometryProtocol(const QVariantMap &geometry, int image_width, int image_height,
                                        const QString &task_root, QString *err_msg = nullptr);

/**
 * @brief 计算两个包围盒的交并比（IoU）。
 * @param lhs 左侧包围盒。
 * @param rhs 右侧包围盒。
 * @return IoU 值；任一包围盒无效时返回 0。
 */
MODEL_API double intersectionOverUnion(const EvaluationBox &lhs, const EvaluationBox &rhs);

/**
 * @brief 计算 GT 与预测边界的并集边界。
 * @param gt GT 几何记录。
 * @param pred 预测几何记录。
 * @return 并集边界映射；两者都无法解析时返回空。
 */
MODEL_API QVariantMap unionBounds(const QVariantMap &gt, const QVariantMap &pred);

/**
 * @brief 以 GT 与预测边界为中心计算裁剪区域（含 5% 边距）。
 * @param gt GT 几何记录。
 * @param pred 预测几何记录。
 * @param image_width 图像宽度（<=0 时不做裁剪）。
 * @param image_height 图像高度。
 * @return 裁剪边界映射；并集无法解析时返回空。
 */
MODEL_API QVariantMap cropBounds(const QVariantMap &gt, const QVariantMap &pred, int image_width, int image_height);

/**
 * @brief 将边界归一化到裁剪视口内（0~1）。
 * @param bounds 待归一化的边界记录。
 * @param crop 裁剪视口边界记录。
 * @return 归一化边界映射；视口无效时返回空。
 */
MODEL_API QVariantMap normalizedOverlayBounds(const QVariantMap &bounds, const QVariantMap &crop);

/**
 * @brief 将多边形点列归一化到裁剪视口内（0~1）。
 * @param geometry 含 points 的几何记录。
 * @param crop 裁剪视口边界记录。
 * @return 归一化点列；点数不足 3 个时返回空。
 */
MODEL_API QVariantList normalizedOverlayPoints(const QVariantMap &geometry, const QVariantMap &crop);

} // namespace dltool::model
