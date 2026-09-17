# feature 模块说明

## 模块定位

`feature` 构建目标为 `dltool_feature`，默认 QML URI 为 `dltool.feature`。它承载需要模型推理、特征计算或外部训练流程的高级能力，目前包含图像相似搜索、标注 ROI 搜索、DINO 细粒度区域检索、图像/标注 ROI 特征聚类、智能标注和小样本学习。

## 架构设计

- `RegionSearchController` 基于 InferRT `DinoRegionSearch` 实现细粒度多尺度目标区域检索，负责 Profile 预设与全局设置合并、`region_search/` 强契约索引生命周期与 `buildReason` 诊断、检索任务阶段进度控制以及候选框去重与标注实例回写。
- `ImageSearchController` 基于 InferRT + FAISS 执行以图搜图。
- `RoiSearchController` 继承 `SearchControllerBase`，基于 InferRT + ROIAlign + FAISS 执行标注实例 ROI 检索。
- `ImageClusterController` 基于 InferRT + HDBSCAN 执行全图特征聚类并写回数据集。
- `RoiClusterController` 继承 `ImageClusterController`，执行标注 ROI 实例特征聚类并写回数据集。
- `SmartAnnotationController` 负责智能标注模型加载、缓存和推理请求，返回 QML 可消费的 mask/polygon 结果。
- `FewShotLearningController` 是小样本学习的 QML 编排层，负责读取页面选择、创建隐藏的 FS-SAM2 普通模型记录，并通过普通模型任务接口启动任务链。
- `FeatureManager` 向 QML 暴露 `imageSearch`、`roiSearch`、`regionSearch`、`imageCluster`、`roiCluster`、`smartAnnotation` 和 `fewShotLearning` 属性，保持页面调用方式稳定统一。

Feature 控制器的项目关闭顺序、线程等待和迟到回调规则见 [`docs/ARCHITECTURE.md`](../../docs/ARCHITECTURE.md)；搜索和聚类的后台进度回调在控制器进入关闭状态后不再投递到全局进度模型，行为测试见 [`tests/feature/test_FeatureLifecycle.cpp`](../../tests/feature/test_FeatureLifecycle.cpp)。

## 与其他模块的关系

- 依赖 `settings` 读取图像搜索、区域检索、聚类、智能标注和小样本学习配置。
- 依赖 `model` 的任务中心接口对接外部训练/推理任务。
- 依赖 `ui` 和 `quickui` 提供 feature QML 组件使用的基础控件。
- 检索与聚类通过 provider 接口使用 `data` 的图像列表、过滤结果和数据集写回能力；区域检索完成后直接通过 `data::DataManager` 创建候选标注实例。
- 通过 `setup_inferrt(feature)` 接入 InferRT、FAISS、CUDA 和 OpenCV 相关能力。

## 边界定义

- 本模块不管理项目数据库 schema，也不直接操作底层数据库连接。
- 图像搜索结果过滤仍属于 `data::GlobalFilter`，由 `DataManager` 作为 provider 写回。
`ImageSearchController` 当前执行以图搜图的流程如下：

1. QML 调用 `searchSelectedImages()` 或 `search(ids, dataset_ids)`。前者从 `ImageSearchDataProvider::selectedImageIds()` 获取当前选中图像，再转入通用搜索入口。
2. 控制器检查是否已有任务运行、数据 provider 是否存在、查询图像 ID 是否有效，以及 `ImageSearch` 设置是否已加载并启用。
3. `buildSearchRequest()` 从全局设置读取模型名称、权重文件、特征层、TopK、索引目录、是否重建索引、归一化方式、预处理后端、FAISS 后端、索引存储方式、推理运行时、推理精度和 batch size。
4. `validateSearchRequest()` 校验模型名、特征层和权重文件。
5. `collectGallery()` 从所有图像中收集图库图像；如果传入 `dataset_ids`，只收集这些数据集内存在文件的图像。
6. `collectQuery()` 将查询图像 ID 转换为存在的图像文件路径。
7. `computeIndexPath()` 生成 FAISS 索引路径。默认索引目录位于项目数据库同级的 `image_search` 子目录，索引文件名由模型名和特征层名组成，后缀为 `.faiss`。
8. 控制器清空旧搜索结果，启动进度任务，并在线程中执行 `executeSearch()`。
9. `executeSearch()` 创建 `irt::features::ImageSearch`，调用 `buildOrLoad()` 构建或加载图库特征索引。
10. 对每张查询图调用 `search.search(query_image, top_k)`，将多查询结果按图像 ID 合并，同一图像只保留最高分。
11. `sortedSearchResultIds()` 按分数降序得到结果图像 ID。
12. `finishSearch()` 回到主线程后调用 `data_provider_->setImageSearchResults(result_ids, enable_filter)`，由 data 模块应用图像过滤结果。

## 标注 ROI 搜索流程

`RoiSearchController` 继承 `ImageSearchController`，复用搜索调度、线程和进度逻辑，但查询对象和图库对象从图像改为标注 ROI：

1. QML 调用 `search(ids, search_scope)`，其中 `ids` 是标注实例 ID，`search_scope` 包含数据集和类别范围。
2. 控制器读取 `RoiSearch` 设置，包含模型、权重、空间特征层、TopK、索引配置、推理运行时、推理精度，以及 ROIAlign 参数 `pooled_height`、`pooled_width`、`sampling_ratio`、`aligned`、PCA 开关和 PCA 维度；ROIAlign 流程由 InferRT 固定执行。
3. `validateSearchRequest()` 先执行图像搜索基础校验，再根据当前模型从设置 schema 中获取可用空间特征层。如果配置的特征层不可用，会回退到该模型最后一个可用空间特征层并写回设置。
3. `validateSearchRequest()` 先执行图像搜索基础校验，再根据当前模型从设置 schema 中获取可用空间特征层。如果配置的特征层不可用，会回退到该模型最后一个可用空间特征层并写回设置。
4. `collectGallery()` 遍历所有标注 ID，找到对应图像和数据集，过滤不存在的图像文件，并通过 `roiFromLabelData()` 从标注数据中的 `x/y/width/height` 构造 `RoiSearchBox`。
5. 如果传入 `search_scope`，图库只保留范围内的标注 ROI。
6. `collectQuery()` 对查询标注执行同样的图像路径校验和 ROI 解析。
7. `computeIndexPath()` 生成 ROI 索引路径。默认索引目录位于项目数据库同级的 `roi_search` 子目录，索引后缀为 `.roi.faiss`。
8. `executeSearch()` 创建 `irt::features::RoiSearch`，调用 `buildOrLoad()` 构建或加载 ROI 特征索引。
9. 对每个查询 ROI 调用 `search.search(query_item.image_path, query_item.roi, top_k)`，同一标注只保留最高分。
10. 搜索结果按分数降序排序后，`applyResults()` 调用 `data_provider_->setLabelSearchResults(result_ids, enable_filter)`，由 data 模块应用标注过滤结果。

## DINO 区域检索流程

`RegionSearchController` 基于 InferRT `DinoRegionSearch` 实现细粒度多尺度目标区域检索：

1. **输入与上下文**：QML 弹出 `RegionSearchDialog.qml`，绑定查询标注（`query_label_id`、原图路径及包围框），并可选择搜索的目标数据集范围。
2. **参数配置与 Profile 模板**：
   - 参数以 `config/profiles/`（如 `dinov2_vits14_reg4.yaml`）为 Preset 底稿，并与全局设置项（`config/settings/RegionSearchSettings.yaml` 划分为基础、预设、模型、图库、特征、运行时、检索、粗排、精排、权重、判定 11 个独立概念节）合并覆盖；
   - 切换 `profile_path` 时，调用 `applyProfileToSettings()` 解析 Profile 并即时写回 `GlobalSettings`，驱动 QML 界面刷新。
3. **图库特征索引状态检查（`checkNeedsBuild`）**：
   - 检查项目根目录下 `region_search/index.yaml` 与 `index_scope.yaml`；
   - 比对 `index.yaml` 中 `manifest` 的关键契约参数（`model_name`、`weights_id`、`encoder_edge`、`view_overlap`、`quantize_int8`）；
   - 检查请求的目标数据集 ID 是否全部包含于 `index_scope.yaml` 的 `indexed_datasets`；
   - 若任一条件不满足，设置 `needsBuild = true` 并记录详细的 `buildReason` 诊断（如参数差异、缺失文件、未覆盖的数据集 ID 等）。
4. **执行检索（`start`）**：
   - **建库阶段（若 `needsBuild` 为真）**：
     - 收集目标图库图像，调用 `irt::features::DinoRegionSearch::buildIndex()`；
     - 阶段进度：`Encode` (0%~70%) -> `Quantize` (70%~85%) -> `Cluster` (85%~95%) -> `Save` (95%~100%)；
     - 建库完成后生成 `index.yaml` 与 `index_scope.yaml`。
   - **检索阶段**：
     - 调用 `irt::features::DinoRegionSearch::search()`；
     - 管线阶段进度：`Fusion` (70%~75%) -> `FineMatch` (75%~85%) -> `FineExtract` (85%~95%) -> `Output` (95%~100%)；
     - 通过单调递增阈值节流器保证 UI 进度平滑递增。
5. **结果去重与标注写入**：
   - 根据 `fine_nms_iou` 对候选检测框去重，并依判定阈值过滤；
   - 调用 `DataManager` 的标注创建接口，将检测到的候选框作为新标注实例存入对应图像和数据集，并发出完成信号与结果摘要。

## 图像聚类流程

`ImageClusterController` 当前执行图像聚类并把聚类结果应用到数据集的流程如下：

1. QML 调用 `clusterSelectedImages()` 或 `cluster(image_ids, dataset_ids)`。前者从当前选中图像构造 `image_ids`，后者可以按指定图像或指定数据集聚类。
2. 控制器检查是否已有聚类任务运行、data provider 是否存在、输入范围是否非空，以及 `ImageCluster` 设置是否已加载并启用。
3. `buildClusterRequest()` 从全局设置读取模型、权重、特征层、归一化、预处理后端、推理运行时、推理精度、batch size、PCA 参数、是否包含噪声点、结果应用方式，以及 HDBSCAN 参数。
4. `validateClusterRequest()` 校验模型名、特征层和权重文件。
5. `collectClusterItems()` 收集要聚类的图像。如果传入 `image_ids`，直接使用这些图像；否则遍历所有图像并按 `dataset_ids` 过滤。不存在的图像文件会被跳过。
6. 聚类至少需要 2 张有效图像。
7. 控制器重置旧结果，启动进度任务，并在线程中执行 `executeCluster()`。
8. `executeCluster()` 创建 `irt::features::ImageCluster`，调用 `cluster.cluster(weights_file, items, progress_callback)` 抽取特征并执行聚类。
9. InferRT 返回每张图像的 `cluster_id` 和 `probability`，同时返回特征维度、簇数量和噪声数量。
10. `finishCluster()` 回到主线程后调用 `data_provider_->applyImageClusterAssignments()`。
11. data 模块根据配置的应用方式将图像复制或移动到聚类生成的数据集中；如果未启用 `include_noise`，噪声图像会被跳过。
12. 控制器记录本次复制或移动的图像数量、目标数据集数量和噪声跳过数量，并更新结果状态。

## 标注 ROI 聚类流程

`RoiClusterController` 继承 `ImageClusterController`，将聚类对象从整图特征切换为标注实例 ROI 特征：
1. 收集选定标注的图像路径与 ROI 矩形；
2. 调用 InferRT ROI 特征抽取与 HDBSCAN 聚类；
3. 聚类完成后将标注实例分配写回目标数据集。

## 智能标注 Mask 到轮廓流程

`SmartAnnotationController` 当前将 SAM 输出的 mask 转换为 polygon 的流程如下：

1. SAM 推理选项固定为 `SAMMaskOutputMode::Single`，并读取 mask 阈值、最大填洞面积和最大去噪面积等配置。
2. SAM 推理输出后固定取第 `0` 个 mask，不再根据 IoU 选择 mask。
3. `selectedBinaryMask()` 将选中的 mask 转为二值 mask。
4. `maskToPaddedMat()` 将二值 mask 转为 `CV_8UC1`，并在外侧添加 1 像素背景边框。
5. `signedDistanceField()` 分别对前景和背景执行 `cv::distanceTransform()`，相减得到 signed distance field。
6. 通过 `cv::compare(signed_distance, 0.0F, ..., cv::CMP_GT)` 从距离场恢复前景区域。
7. `cv::findContours(..., cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE)` 提取所有外轮廓，并只保留面积最大的外轮廓。
8. `buildContourPostprocessOptions()` 读取 `polygon_approx_epsilon`；`maskToPolygons()` 在轮廓转点前执行 OpenCV `cv::approxPolyDP()`，用该系数计算 `epsilon = polygon_approx_epsilon * arcLength(contour, true)`，为 `0` 时跳过点压缩。
9. `contourToPolygon()` 对拟合后的整数轮廓点逐点调用 `refineContourPoint()`，利用距离场将点修正到零等值线附近，得到亚像素轮廓点。
10. `normalizePolygon()` 去除连续近重复点和闭合重复点，过滤无效小面积轮廓，并保证轮廓方向为逆时针。
11. 轮廓点先处于 SAM 输入图坐标系，随后通过 `mapInputPolygonToSource()` 映射回原图坐标系。
12. `postprocessContourPolygon()` 对映射后的 polygon 做规范化，并根据配置决定是否继续执行 B 样条。
13. 如果 `polygon_spline_enabled` 为 `false`，最终 polygon 直接使用映射后的 OpenCV 多边形拟合结果。
14. 如果 `polygon_spline_enabled` 为 `true`，再将 polygon 送入 `irt::ops::splPrep()` 做 B 样条拟合。
15. B 样条阶段使用 `splPrep()` 返回的 `parameters` 调用 `irt::ops::evaluateBSpline()` 得到最终 polygon 点；拟合失败时回退到映射后的 polygon。
16. 如果最终 polygon 少于 3 个点，则回退为 mask 外接矩形。

## 小样本学习流程

`FewShotLearningController` 不维护独立训练/推理进程，只负责编排普通模型记录和任务：

1. QML 创建并绑定训练、验证、测试数据集和类别选择 ViewModel，然后调用无参 `startFsSam2()`。
2. `FewShotLearningController` 从 ViewModel 读取选择，通过 `ModelManager.addModelRecord()` 创建隐藏的 `FS-SAM2` 普通模型记录。
3. 控制器把训练/验证/测试数据选择和全局小样本设置写入该模型的普通 train/test 参数。
4. 控制器调用 `ModelTaskController.addModelTask()` 和 `startModelTask()` 添加并启动 BoxToMask、训练、推理任务。
5. 本次运行的训练数据、日志、权重和预测结果保存到项目目录 `models/<model_name>/`，其中预测结果位于 `results/predictions/`，训练权重位于 `weights/fs_sam2/best_model.pt`。
6. 检测项目会先运行 `box_to_mask.py`，再运行 `train.py` 和 `predict.py`；分割和异常检测项目直接进入训练。
7. `FewShotLearningController` 监听任务表状态推进下一步；任务停止和进程退出仍走普通 `ModelTaskController`/`ExternalModelTaskRunner` 链路。
8. 测试任务完成后，`FewShotLearningController` 按测试数据集调用 `DataManager.importMaskData()` 导入预测 mask；小样本 UI 的运行状态在测试任务完成时结束，导入状态归普通导入流程维护。
