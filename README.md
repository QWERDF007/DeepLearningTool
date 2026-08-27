# DeepLearningTool

DeepLearningTool 是一个基于 Qt 6/QML 和 C++20 的桌面深度学习工具，以 SQLite 项目文件组织数据集、标注和模型元数据，并以模型目录保存任务产物。

## 功能范围

- 创建、打开和管理 `.dlpro` 项目。
- 管理多数据集、图片、类别、标注和图片/标注标签。
- 导入、导出 Folder、Mask、LabelMe 和 COCO 数据。
- 提供图库、标注、复核、过滤、统计、图像搜索和智能标注工作区。
- 配置模型，运行训练、推理和评估任务。
- 通过独立 Python 进程执行模型任务，并在界面中查看任务状态和评估结果。

当前模型和功能注册以源码及配置为准，入口见 [`config/models/`](config/models/) 和 [`src/model/`](src/model/)。

## 技术栈

- C++20、Qt 6、Qt Quick/QML
- CMake 3.18+
- SQLite、sqlpp11
- yaml-cpp、spdlog、nlohmann/json
- CUDA、OpenCV、InferRT/FAISS
- Qt Test、Qt Quick Test、CTest
- EasyTrain/Python 外部任务

## 快速开始

先确认 [`cmake/`](cmake/) 中的 Qt、CUDA、SQLite、OpenCV 和 InferRT 路径符合当前机器，然后配置并构建：

```powershell
cmake -S . -B build -DDLT_BUILD_TESTS=ON
cmake --build build --config Release --parallel 4
```

运行测试请使用 CTest 或项目提供的 Python 调度脚本，不要直接启动测试可执行文件：

```powershell
ctest --test-dir build -C Release -L ordinary --output-on-failure
python tools\run_project_tests.py --project-layer full --skip-build
```

Windows 下启动应用或测试前，如需准备 DLL 和 QML 运行环境，先查看 [`tools/README.md`](tools/README.md)。

## 文档

- [文档入口](docs/README.md)
- [架构](docs/ARCHITECTURE.md)
- [模块索引](docs/MODULES.md)
- [数据模型与存储](docs/DATA_MODEL.md)
- [开发指南](docs/DEVELOPMENT.md)
- [测试指南](docs/TESTING.md)

## 许可证

[LICENSE](LICENSE)
