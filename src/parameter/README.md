# parameter 模块说明

`parameter` 是 settings、model 和 feature 共用的参数基础模块。它不负责数据库、QML 页面或模型任务，只负责参数公共元数据、动态选项 provider 和值解析。

## 模块职责

- `ParameterSpec`：公共参数定义，包含名称、类型、默认值、展示类型和选项映射。
- `ParameterOption`：动态选项的显示值和后端实际值组合。
- `DynamicOptionsProvider`：动态选项来源的抽象接口。
- `DynamicOptionsRegistry`：进程级 provider 注册表。
- `parseParameterSpec()`：复用 `common::yaml` 工具解析 YAML 公共字段。
- `resolveParameterOptions()`：查询 provider、生成选项和选择默认实际值。
- `normalizeParameterValue()`：统一 settings/model 的输入值转换和动态值规范化。

## 动态 provider

provider 只实现数据查询：

```cpp
class MyProvider final : public DynamicOptionsProvider
{
public:
    QString key() const override { return QStringLiteral("feature.items"); }

    DynamicOptionsData query(const QVariantMap &context) const override;
};
```

查询结果中的每个 `ParameterOption` 同时包含界面显示值和后端实际值。界面使用显示值，settings 数据库和模型任务配置保存实际值。

新增 provider 后调用 `registerDynamicOptionsProvider()` 注册即可。重复 key 不会静默覆盖已有 provider。

内置硬件 provider 使用以下 key：

- `inferrt.cpu_devices`
- `inferrt.gpu_devices`
- `inferrt.compute_devices`
- `training.compute_devices`

这些 provider 返回 InferRT `ModelRuntime` 字符串。GPU 运行时包含 `tensorrt:<id>`、
`onnxruntime:<id>` 和 `openvino:<id>`，CPU 运行时包含 `onnxruntime:cpu` 和
`openvino:cpu`。`inferrt.compute_devices` 将 GPU 运行时排在 CPU 运行时前面，并推荐第一块 GPU 的
TensorRT 运行时；没有 GPU 时推荐 CPU 的 ONNX Runtime。

`training.compute_devices` 面向训练后端，返回设备名称和训练设备值的映射：GPU 使用
`cuda:<id>`，CPU 使用 `cpu`。它与 InferRT provider 分开，避免把
`tensorrt:<id>` 等推理运行时值传给训练引擎。

## YAML 字段规范

参数 YAML 使用统一字段名，不再解析旧字段别名。公共展示字段包括 `description` 和
`display_type`；静态显示值映射使用 `options_values`，联动选项使用 `options_map` 与
`options_key_field`：

```yaml
param_type: dynamic
display_type: combo
backend_key: inferrt.compute_devices
```

settings 和 model 只解析自己的分组/业务字段，公共参数字段统一由本模块解析。
