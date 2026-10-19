# 离线帮助与用户手册规格

## Problem Statement

用户在桌面应用中需要了解操作步骤、前置条件、结果和故障处理方式，但帮助入口尚未形成可用的阅读链路。仓库开发文档面向架构、构建和测试，不能替代产品用户手册。现场用户可能无法联网，也不应为了阅读帮助而安装 Python、启动服务或访问源码仓库。

## Solution

通过“帮助 → 文档”打开随程序交付的中文静态手册。手册提供快速上手、功能参考和故障诊断，在本地文件协议下支持阅读、中文搜索和页面跳转。开发与发布采用相同的文档定位契约，文档构建或完整性检查失败时禁止生成成功的发布结果。

## User Stories

1. As an application user, I want a 文档 item in the 帮助 menu, so that I can discover the manual from the application.
2. As a new user, I want to open help before creating a project, so that I can learn how to begin.
3. As a keyboard user, I want to navigate and dismiss the help menu with the keyboard, so that I do not depend on a mouse.
4. As an application user, I want the manual to open in the system-associated application, so that I can use familiar reading controls.
5. As an offline user, I want all required reading resources to be local, so that a disconnected workstation remains usable.
6. As a Chinese-speaking user, I want Chinese navigation and explanations, so that I can understand the product without translating developer documentation.
7. As a reader, I want Chinese search results and working result links, so that I can locate relevant instructions quickly.
8. As a reader, I want useful search-result highlighting where supported, so that I can identify relevant passages.
9. As a reader, I want light, dark and automatic appearance options, so that I can read comfortably.
10. As a new user, I want installation prerequisites and a first-use walkthrough, so that I can prepare a working environment.
11. As a project user, I want guidance for creating, opening and locating projects, so that I can manage my work correctly.
12. As a gallery user, I want instructions for importing and organizing images, so that I can prepare usable data.
13. As an annotator, I want task-specific annotation guidance, so that I can produce valid labels.
14. As a reviewer, I want instructions for inspecting and correcting annotations, so that I can improve data quality.
15. As a training user, I want prerequisites and instructions for supported training tasks, so that I can start and monitor model training.
16. As an evaluation user, I want explanations of evaluation inputs and observable results, so that I can interpret model performance.
17. As an export user, I want an accurate description of supported export operations, so that I do not rely on unavailable capabilities.
18. As an advanced user, I want guidance for implemented advanced features, so that I can use capabilities beyond the main workflow.
19. As a user facing a failure, I want symptom-based troubleshooting, so that I can identify a practical next action.
20. As a reader, I want screenshots and terminology that match the application, so that I can follow instructions without guessing.
21. As a reader, I want both a task-oriented quick start and a feature reference, so that I can choose the appropriate reading path.
22. As a portable-package user, I want help to work after moving the package, so that the installation location does not restrict reading.
23. As a user with a Chinese or space-containing installation path, I want help links to remain valid, so that path encoding does not prevent access.
24. As an application user, I want a clear warning when the local manual is unavailable, so that I know how to recover.
25. As an application user, I want a distinct warning when the system rejects the open request, so that I can check the file association.
26. As a developer, I want the same help behaviour in development and published packages, so that local success predicts deployed behaviour.
27. As a documentation author, I want one Markdown content source and local assets, so that edits do not diverge across formats or repositories.
28. As a documentation author, I want generated HTML and search indexes, so that I do not maintain derived content manually.
29. As a release maintainer, I want missing, stale or incomplete manual output to block release, so that users receive a working help package.
30. As a maintainer, I want observable tests and real offline-package verification, so that a successful build is not mistaken for successful user behaviour.

## Implementation Decisions

### D1 外部参考与产品边界

- MVTec 和 deeplearning-offline 都是外部参考，分别借鉴帮助菜单、静态阅读、离线检索和随程序部署的思路。
- 不视为本项目旧版本，不迁移其手册、截图或术语，不建立兼容路径，不把外部工程作为构建输入。
- 本功能不改变模型、数据或数据库契约，也不为撰写手册而实现新的业务功能。

### D2 文档工具链

- 唯一生成体系采用 Sphinx、MyST Parser 和 Furo，正文以普通 Markdown 为主，仅在必要处使用导航、交叉引用和提示块扩展。
- 使用标准 HTML builder，中文界面与中文搜索分别配置；显式声明中文分词依赖。
- 文档依赖独立于模型训练环境，锁定经过离线验收的版本组合。构建时不隐式安装依赖或下载资源。
- 版本选择由实际兼容性和验收结果决定；不得因官方声明支持某项能力而跳过验证。

### D3 内容单一事实源

- 用户手册独立于仓库开发文档，由本项目 Markdown 和本地图片唯一维护。开发文档只提供导航，不复制用户操作说明。
- HTML、搜索索引和站点资源均为派生产物，不手工修改，不作为另一份内容源维护。
- 参数默认值、格式和支持范围以现有代码与配置为准，不手写第二份完整参数定义；需要完整参考表时从事实源派生。
- 手册导航定义章节顺序，页面文件名不承担排序职责。新增公共配置或导航只维护一处。

### D4 手册信息架构

- 提供快速上手和功能参考两种阅读路径，覆盖项目、图库、标注、检查、训练、评估、导出，以及已实现的高级功能和故障诊断。
- 每个任务说明必要前提、用户操作、可观察结果和相关故障。章节覆盖不等于承诺该领域的所有能力。
- 概念、按钮名称、字段、截图和输出结果必须经当前程序核对。未实现操作不得写成可用功能；无法验证的限制或承诺不得写入。
- 共用前提和概念只维护一份，通过交叉引用复用。用户手册不暴露架构分析、访谈、开发票据或任务账本。

### D5 帮助菜单

- 复用现有帮助按钮与 QuickUI 菜单、菜单项、文档图标，只展示“文档”这一已实现项。
- 没有打开项目时仍可使用；右上角定位、窗口边界、鼠标、键盘、Esc 和关闭后的焦点恢复均需正常。
- 不增加置灰占位、新闻、日志目录或关于入口，也不在 QML 中解析文件路径或处理系统打开错误。

### D6 应用级帮助模块

- 在应用装配层提供 HelpController，通过现有应用 QML 模块暴露唯一操作 openDocumentation。
- 内部承担固定路径定位、普通文件及可读性检查、本地 URL 转换、系统打开请求、日志和通知。
- 复用 Qt 桌面打开能力与现有 SignalHelper 警告通知；不扩充通用 Utils，不引入宽泛 SystemHelper 或浏览器抽象框架。
- 使用 Qt 的本地文件 URL 转换，不手工拼接协议。系统接受请求不等于浏览器已成功呈现页面，二者分别验证。

### D7 唯一路径契约

- 文档入口固定为可执行文件所在目录的 doc 子目录中的 index.html；开发、安装和发布保持这一布局。
- 相对位置由构建配置定义一次，运行时、生成输出和打包使用派生信息，不独立维护多份可配置路径。
- 不依赖当前工作目录，不扫描源码，不探测其他目录，不做在线回退。开发便利通过布置正确产物实现。

### D8 纯离线阅读与搜索

- 使用明确的 HTML 页面链接，不依赖目录 URL、重写规则、后台服务或内嵌浏览器。
- 索引、脚本、样式、字体和图片必须全部本地可用；禁止阅读时依赖远程资源、CDN 或外部工程。
- 关闭需要额外读取正文的搜索摘要；不向发布包复制源文档或显示源码链接。
- 使用原生中文搜索、结果跳转和主题支持的高亮；不承诺即时联想搜索，不自行修改第三方搜索引擎。
- 先验证业务词命中率，再决定是否需要维护中文自定义词典，不预设额外分词层。

### D9 构建与发布

- 复用 DLT_BUILD_DOCS，提供唯一 dltool_docs 构建目标；关闭时不要求文档 Python 依赖，也不影响普通开发构建。
- 开启时生成到可执行文件旁。CMake 安装与现有 Python 发布链路复用同一产物及定位契约，不各自维护生成参数。
- 文档源、配置、导航和图片变化均须被构建识别；发布采用干净输出，删除的页面不能残留。
- 发布必须重新生成并验证文档。构建失败、必要资源缺失或本地链接失效阻止成功发布，不使用旧产物兜底。
- 完整性校验覆盖入口、搜索页、索引及页面引用的本地资源，而不只是首页存在。
- 避免重复实现 Markdown 解析、主题或搜索；必要的发布校验复用现有发布机制和成熟工具。

### D10 失败反馈

- 文档不存在、不是普通文件或不可读取时，警告“帮助文档不可用，请重新安装完整程序。”
- 系统拒绝打开请求时，警告“无法打开帮助文档，请检查 HTML 文件的默认打开方式。”
- 具体路径与技术原因记录到现有日志。失败不造成静默无响应，单次触发最多产生一条失败通知，不要求产品用户生成文档。

### D11 阅读体验

- 采用 Furo 的简洁导航和浅色、深色、自动主题，配置项目品牌色，不另建主题实现。
- 自动主题依据浏览器或系统偏好，不与 Qt 应用主题同步。不加载远程字体，字体资源本地化或使用系统字体。
- 第一阶段只提供中文，无多语言、版本切换、分析统计、登录或 API 自动文档。

## Testing Decisions

### T1 帮助模块的公开入口

- 以 HelpController 的公开操作作为主要 seam，验证 URL 请求、通知和用户可观察结果，不断言私有函数或重复实现路径算法。
- 使用真实临时文件系统和生产路径定位逻辑，覆盖有效入口、缺失文件、目录误用、中文及空格路径、URL 特殊字符、工作目录变化。
- 优先复用 Qt 已有系统打开边界截获请求、SignalHelper 信号观察和 Qt Test 机制；不为测试公开路径解析接口，不 mock 文件系统或下一层生产逻辑。
- 系统拒绝打开的确定性测试若需要替换，只在最终系统打开边界设置窄的内部 seam，不扩张 QML 接口。权限差异不能用实际可读文件伪造不可读用例。
- 先写行为测试，再实现功能。Qt 全局测试替换必须清理，不影响后续用例。

### T2 菜单交互

- 沿用现有 QML QuickTest 的公开交互测试方式，验证无项目时可用、触发次数、定位、键盘操作、Esc 和焦点恢复。
- 不依赖私有控件层次或字符串匹配源码来证明交互正确。

### T3 构建与发布命令

- 通过真实文档构建、安装和打包命令验证产物，沿用现有 Python 工具测试和发布完整性检查方式。
- 覆盖关闭文档选项、开启生成、增量内容变化、页面删除、生成失败和资源缺失。失败不得宣告发布完成或使用陈旧输出。
- 校验本地链接、锚点、资源和远程资源依赖；普通外部阅读链接可以保留，但不能成为本地核心流程的必需条件。

### T4 本地浏览器

- 使用生产版本的生成产物，在断网且无预热缓存的浏览器会话中，通过本地文件协议验证导航、图片、中文搜索、结果跳转、主题和高亮。
- Windows 优先验证 Edge 与 Chrome；HTTP 预览、官方能力说明和构建成功均不能替代此验收。
- 搜索覆盖实际章节中的标注、异常检测、阈值、训练、评估、导出等词，以及空查询和无结果查询；无对应内容时不得制造命中。

### T5 内容真实操作

- 章节与相应真实功能一起验收，使用有效项目、图像和可用模型资源，核对每条操作、截图与可观察结果。
- 未实现功能不扩张为本票据的开发任务；明确真实边界，避免补写虚构步骤或旧项目术语。

### T6 可移动发布包

- 将发布包移动到包含中文、空格及可用特殊字符的位置，改变启动工作目录，断网从应用菜单打开手册并完成搜索和阅读。
- 模拟文档缺失及系统打开拒绝，验证用户反馈。最终确认运行的可执行文件和依赖来自被验收的实际发布包。

### T7 验收与证据

- 开发期间只运行相关定向测试，沿用 CTest 的运行环境注册，不绕过项目测试入口。
- 测试命令、关键结果和真实环境证据记录在唯一任务账本，不另建进度文档。
- 票据的 ready-for-agent 表示可领取规格，不表示阻塞项完成或功能完成；完成必须有相应验证证据。

## Out of Scope

- 参考项目文档迁移、历史目录兼容、多级路径探测及在线兜底。
- 内嵌浏览器、本地 HTTP 服务、新闻、日志目录和关于菜单。
- 多语言、多版本、API 自动文档、实时联想搜索、应用与浏览器主题同步。
- 自研主题、Markdown 解析器、全文搜索引擎或通用系统工具框架。
- 新增或修复与帮助无关的模型、数据库、训练、评估和导出业务功能。
- 手工维护第二份参数定义、生成 HTML 或发布内容源。

## Further Notes

- [票据索引](help-documentation-tickets/README.md)承载领取入口与阻塞关系，定位现有代码和测试的链接也集中在该索引。
- 架构、模块、数据、开发和测试的现有约定继续通过[仓库文档入口](README.md)阅读，本规格不替代这些事实源。
- 第一张票据交付最小完整链路，不把脚手架或未完成目录当作用户能力；其余票据补齐各自可验证的阅读任务。
- 五张票据全部通过，且组合后的实际发布包通过离线验收，才构成本规格的完整交付。对工具链的调整先更新此处唯一决策，不建立并行方案。
