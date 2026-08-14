#pragma once

#include "dltool/model/Export.h"

#include <QString>
#include <QVariantMap>

namespace dltool::model {

/**
 * @brief 注册模型 checkpoint 动态选项 provider。
 *
 * provider 返回官方权重和项目内同框架、同架构模型的训练权重分组，
 * 供 backend_key 为 model.checkpoints 的参数使用。
 */
MODEL_API void registerModelCheckpointsProvider();

} // namespace dltool::model
