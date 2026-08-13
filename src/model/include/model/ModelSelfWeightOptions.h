#pragma once

#include "dltool/model/Export.h"

#include <QString>
#include <QVariantMap>

namespace dltool::model {

/**
 * @brief 枚举当前模型自身训练产生的权重文件的动态选项。
 *
 * 供 backend_key 为 model.checkpoints 的 combo 参数使用（如 test 侧的 checkpoint）。
 * query(context) 通过 context 中的 model_name/project_dir/extensions 定位
 * <project>/models/<model_name>/train/weights 目录并按扩展名过滤权重文件。
 */
MODEL_API void registerModelSelfWeightOptionsProvider();

} // namespace dltool::model
