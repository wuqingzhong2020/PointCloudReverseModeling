# ReverseModeling 开发指南

本项目是基于 C++20、Qt Widgets 和 OCCT 的点云辅助逆向建模桌面应用。主要流程是：导入 PCD 点云，识别可观测板件的位置和厚度，生成单侧模型，再进行偏差计算和 STEP 导出。底板按两侧板识别厚度的最大值估算，缺少侧板依据时保留底部观测面，手动参数建模作为独立模式。

工程包含两个可执行程序：

| 程序 | 职责 | CMake 入口 |
|---|---|---|
| `ReverseModeler` | 桌面交互、点云与模型显示、建模操作 | [主程序 CMakeLists.txt](CMakeLists.txt) |
| `ReverseServiceTests` | 使用 Google Test 检查几何与命令服务 | [测试程序 CMakeLists.txt](../tests/CMakeLists.txt) |

开发时打开仓库根目录的 [CMakeLists.txt](../CMakeLists.txt)。两个程序直接编译同一份业务源码，工程内部不生成独立业务库。

## 目录与代码入口

```text
PointCloudReverseModeling/
├── CMakeLists.txt               # 总入口，加载两个子项目
├── CMakePresets.json            # 全仓库唯一的构建预设
├── cmake/
│   ├── common.cmake             # C++ 标准、编译选项与输出目录
│   └── Dependencies.cmake       # 全部第三方库查找与公共链接库
├── ReverseModeling/
│   ├── CMakeLists.txt           # 主程序文件清单、头文件搜索目录和链接库
│   ├── ReverseModeling.cmake    # 汇总 core、geometry、commands 的公共文件
│   ├── main.cpp                # QApplication 与 MainWindow 入口
│   ├── core/                   # 点云、参数、网格和结果的数据类型
│   ├── geometry/               # 点云读取、PCA、建模、STEP 与偏差算法
│   ├── commands/               # 异步命令、工程状态与 JSON
│   ├── workbench/              # Qt 窗口与 OCCT 视口
│   └── examples/               # 参数格式示例
└── tests/
    ├── CMakeLists.txt           # 测试程序文件清单、头文件搜索目录和链接库
    ├── tests.cmake             # 测试文件清单
    └── service_tests.cpp        # Google Test 用例
```

建议按以下顺序阅读：

| 文件 | 重点 |
|---|---|
| [main.cpp](main.cpp) | 应用启动与主窗口创建 |
| [MainWindow.cpp](workbench/MainWindow.cpp) | 用户操作、参数面板、任务轮询和界面刷新 |
| [WorkbenchActions.cpp](workbench/WorkbenchActions.cpp) / [RibbonBuilder.cpp](workbench/RibbonBuilder.cpp) | 共用动作、能力标记、Ribbon 与视图工具条 |
| [DockWorkspace.cpp](workbench/DockWorkspace.cpp) | ADS 工具窗口、窗口菜单、布局保存与恢复 |
| [RecentFilesMenu.cpp](workbench/RecentFilesMenu.cpp) | 最近成功导入的点云/模型路径、排序去重、持久保存与清空记录 |
| [WorkbenchPanels.cpp](workbench/WorkbenchPanels.cpp) / [HBeamTemplateFields.cpp](workbench/HBeamTemplateFields.cpp) | H 型钢模板入口、逐板识别结果、镜像选项、手动参数、待接入参数说明和后续操作表单 |
| [WorkbenchTheme.cpp](workbench/WorkbenchTheme.cpp) | 深色主题、自有图标与高 DPI 图标尺寸 |
| [CommandService.h](commands/CommandService.h) / [CommandService.cpp](commands/CommandService.cpp) | 命令入口、任务状态、工程快照和版本控制 |
| [Types.h](core/Types.h) | `CloudData`、`BeamParameters`、`MeshData` 等公共数据契约 |
| [PointCloud.cpp](geometry/PointCloud.cpp) | PCD 读取、单位转换、有效点过滤和 PCA 初值 |
| [PointCloudExport.cpp](geometry/PointCloudExport.cpp) | 全量点云 PCD 写出、回读核对与不覆盖提交 |
| [ModelImport.cpp](geometry/ModelImport.cpp) | STEP/STP、CWRM v2 BRep 解码和导入 |
| [BeamModel.cpp](geometry/BeamModel.cpp) | 板件建模、网格生成、STEP 导出与回读、偏差计算 |
| [PlateRecognition.cpp](geometry/PlateRecognition.cpp) | 从点云顶边条带识别厚度和站位，按两侧板最大值估算底板，保留缺测观测面 |
| [OccViewport.cpp](workbench/OccViewport.cpp) | 点云与三角网格的 OCCT 显示适配 |
| [ModelDisplayData.cpp](workbench/ModelDisplayData.cpp) / [OccModelPresentation.cpp](workbench/OccModelPresentation.cpp) | 按部件归属拆分网格、稳定配色、模型板厚标注及屏幕避让 |
| [ModelLabelVisibility.cpp](workbench/ModelLabelVisibility.cpp) | 板件主表面朝向及模型网格遮挡判断，避免显示背侧板厚 |
| [PointCloudDisplayData.cpp](workbench/PointCloudDisplayData.cpp) | 中立点云显示采样与紫青高度渐变 |

## 业务调用流程

```mermaid
flowchart LR
    A[main.cpp] --> B[MainWindow]
    B -->|提交命令、查询状态| C[CommandService]
    C --> D[PointCloud / PlateRecognition / BeamModel]
    C -->|任务与工程快照| B
    B --> E[OccViewport]
```

1. **导入文件**：STEP/STP 和 CWRM 经 `loadCadModel()` 直接创建模型，替换当前点云/模型并清空编辑历史；PCD 路径为：`MainWindow` 调用 `submitImport()`，后台依次执行 `loadPointCloud()`、`estimateBeam()` 和 `recognizePlates()`，得到点云、初始方向及逐板识别结果；识别失败保留点云与原因，不填经验板厚。
2. **生成模型**：默认通过 `submitRecognizedReconstruct()` 调用 `buildRecognizedPlates()`，构建各板件的识别尺寸，底板按两侧板厚最大值向观测面背侧生成实体，宽度夹在翼板内侧面之间，翼板下沿延伸到其底面；缺少任一侧板可靠识别值时保留底部观测面。镜像默认关闭，开启后明确记录镜像部件的来源。显式手动模式继续通过 `submitReconstruct()` / `buildBeam()` 建模。
3. **更新显示**：界面通过定时器轮询 `task()`，读取 `project()` 返回的工程快照，再更新参数、日志和 `OccViewport`。
4. **导出与偏差**：`submitExportCloud()` 写出当前全量点云，`submitExport()` 执行 STEP 写出及独立回读检查；`submitDeviation()` 计算点到当前模型板件表面的距离。

服务与几何接口见 [CommandService.h](commands/CommandService.h) 和 [Geometry.h](geometry/Geometry.h)。[Json.h](commands/Json.h) 提供任务、工程摘要的序列化和参数解析，当前 `schemaVersion` 为 1，摘要不携带点云或网格大数组。

### 数据与线程约定

- 内部尺寸统一为 **mm**；PCD 桌面导入固定使用 mm，底层接口仍接受 mm、cm 或 m；STEP 读取文件单位并转换到 mm，CWRM 按 mm。`CloudData` 中的点使用局部坐标，项目坐标为 `position + originMm`；`MeshData` 顶点使用项目坐标。
- `CommandService` 同时执行一个后台任务，忙时拒绝新提交。工作线程执行几何计算，QWidget 和 AIS 对象由 GUI 线程操作。
- 重建、导出和偏差命令携带 `expectedRevision`，旧版本请求会被拒绝。导入或重建成功后更新工程版本；失败保留原有结果。
- 取消通过 `std::stop_token` 协作完成。关闭窗口时服务请求取消并等待工作线程结束，单次 OCCT 内核调用可能需要等待其返回。
- 当前工程状态保存在内存，任务与资产 ID 在本次服务生命周期内有效。

## 桌面工作台

`MainWindow` 继承 `SARibbonMainWindow`，使用 `UseRibbonMenuBar | UseNativeFrame` 保留原生系统标题栏。SARibbon 组织顶部功能区，单层 ADS 管理固定中央视图和六类工具窗口。视图容器、日志及工程摘要由 `MainWindow` 装配，视图工具条由 `RibbonBuilder` 创建，重建参数与操作表单集中在 `WorkbenchPanels`；各文件入口见上方代码表。

布局与交互参考 CypModeler 截图及灵控平台 `D:/projectWQZ/ProjectManager/project/LingkongPlatformLauncher/src/LingkongPlatformLauncher/Launcher/MainWindow.cpp`。主要借鉴其 Ribbon 分组、组件尺寸和 OCCT 容器用法；本工程使用独立的主题与单层 Dock 管理，不引入灵控的插件系统、嵌套 QDockWidget 或构建包装。截图只提供 UI 参考，不能据此认定对应算法或文件格式已经实现。

### 工具窗口与布局生命周期

| 窗口 | 稳定 `objectName` | 初始停靠位置 | 当前内容 |
|---|---|---|---|
| 模型树 | `dock.modelTree` | 左侧 | 当前工程、点云及板件模型状态摘要 |
| 属性 | `dock.properties` | 右侧 | 工程版本、来源、有效点数和实体数；对象选择与编辑待接入 |
| 重建参数 | `dock.reconstruction` | 左侧 | 逐板识别结果、默认关闭的镜像选项及独立手动参数模式 |
| 操作面板 | `dock.operation` | 左侧 | 移动、旋转、基准面、孔洞等操作表单 |
| 日志窗口 | `dock.log` | 底部 | 日志、复制所选和清空；隐藏时继续收集 |
| 任务信息 | `dock.tasks` | 底部 | 当前任务名称、状态、阶段和进度，不维护历史任务列表 |

中央三维视图使用 `dock.viewport`，不可关闭、移动或浮动，不列入窗口菜单。辅助窗口默认隐藏，按需打开；同侧窗口可标签化组合，自动隐藏侧边栏暂未启用。窗口菜单直接复用 `CDockWidget::toggleViewAction()`，勾选表示窗口已打开，标签被其他窗口遮挡不代表关闭。

关闭辅助窗口等价于隐藏，不启用 `DockWidgetDeleteOnClose`，重开沿用原实例及内容。Ribbon“重建参数”和窗口菜单指向同一个 Dock，重复打开只显示并激活该窗口。“隐藏所有辅助窗口”保留中央视图、Ribbon 和状态栏；“恢复默认布局”恢复初始停靠安排并收起辅助窗口，不清空点云、模型、日志或参数。

布局在 `QSettings` 的 `ui/layout/v1` 组保存主窗 `geometry` 和 ADS `docks` 状态，ADS 布局版本为 1。启动顺序为：设置 ADS 全局配置 → 创建中央 Dock 并调用 `setCentralWidget()` → 注册全部工具 Dock → 捕获默认布局 → 恢复用户布局。恢复失败回退默认快照，并检查主窗与浮动窗口标题栏是否仍在可用屏幕范围内。正常关闭流程被接受后才保存布局；有后台任务时先请求取消并等待安全退出。窗口集合或保存格式发生不兼容变化时同步升级配置组与布局版本。

### 动作、参数与后续接入

“打开 → 最近打开文件”由 `RecentFilesMenu` 管理，最近 10 条路径保存在 `QSettings` 的 `files/recentPointClouds` 中。仅在导入任务成功后加入或前移记录；菜单重开与普通打开共用 `MainWindow::importCloud(path)`，按扩展名分派导入，忙时不可提交。完整路径用于区分同名文件，Windows 路径去重忽略大小写；失效文件保留记录但禁用，清空不触及源文件。

`WorkbenchActions` 统一创建 QAction、稳定名称、快捷键和能力标记，供 Ribbon、菜单及快捷键共用。`MainWindow` 根据项目快照、任务和参数修改状态更新可用性：已有业务正常提交服务；表单预览动作只打开输入界面，执行按钮禁用；尚未接入的命令整体禁用并说明原因。忙时仍可查看日志和切换工具窗口。

| 功能区域 | 已有能力与预留入口 |
|---|---|
| 文件 | 导入 PCD、STEP/STP、CWRM；导出 PCD、STEP；打开文件夹及参数导入/导出待接入 |
| 编辑与点云 | 框选、加选/减选、截取保留选区、删除选区、点云编辑撤销/重做及重置可用；更多菜单中的对齐、去重、识别精度、AI 去噪/分板待接入 |
| 重建 | 参数、开始重建和停止重建连接现有服务；局部重建待接入 |
| 模型修改 | 选点建模、障碍物、板厚/轮廓调整、槽钢/角钢、贴板/底板、过焊孔/底边孔、圆角/倒角、移动及旋转提供表单；删除模型和钢结构命令待接入 |
| 工具 | 测量、基准面提供表单；计算偏差使用现有服务 |
| 视图工具条 | 点云/实体显隐、高度渐变、可见侧板厚、适合视图、底板正视和方向立方体可用；点选择可用；线/面/体过滤、线框、剖切待接入 |

点云编辑：左键拖动矩形框选，选择工具条中的“选择 / 新增选择 / 移除选择”切换替换、加选和减选。橙色矩形表示框选范围，蓝色表示选中点；右键单击打开“截取点云 / 删除点云”菜单，右键拖动仍旋转视图。“截取点云”只保留选中点，“删除点云”移除选中点；未选点或点云隐藏时禁用。单击空白（替换模式）或 Esc 清空选区。框选贯穿当前相机视锥深度，包括遮挡点，作用于完整点云，不只处理最多 20 万个显示点。

`CloudSelectionRect` 保存局部毫米坐标到相机裁剪空间的矩阵与矩形；`geometry/CloudEditing` 处理投影、增减索引和过滤，不访问 Qt / AIS。过滤保留原始 `sourceIndex`、强度、单位和显示原点，重算当前包围盒；原始文件点数与当前有效点数分开统计。`PointCloudDisplayData::pointIndex` 将显示采样映射到当前完整点云选区，选择本身不改变识别和模型。

截取或删除通过 `CommandService` 后台执行，成功时原子更新点云、重算 PCA / 板件识别，清除旧模型、偏差与导出状态；识别失败保留编辑后的点云并给出提示。删除全部点允许得到空点云，不能继续重建。失败或取消保留操作前状态。编辑不写源 PCD 文件；全选后截取不改变数据或模型。

撤销/重做仅覆盖当前导入会话的点云编辑，最多保留 10 步，同时恢复点云、选区、识别和实际模型。重置恢复本次导入的完整点云，重置也可撤销；新编辑清空重做，重新成功导入清空历史。Delete、Ctrl+Z、Ctrl+Y 仅在视图区有焦点时执行点云删除、撤销和重做，不影响参数输入。编辑后相机位置保持不变，可双击或使用“适合视图”重新居中。

“停止重建”仅取消重建任务；其他任务使用状态栏的“取消任务”。新增 UI 命令继续通过 `CommandService` 提交，沿用 `expectedRevision` 和单任务规则；Qt、SARibbon、ADS 类型不进入 `core`、`geometry` 或业务服务契约。

重建面板默认展示逐板厚度和依据：侧板、肋板提示支持点数及截面离散度，底板显示“侧板最大值（估算）”并说明公式；不加载经验板厚和固定肋板站位。独立的“端部突出容差”已接入 `PlateBuildOptions`，默认 30 mm，0 关闭；只收齐沿梁长方向的小突出，超过阈值的长度保留并告警。修改后需重建，模型 JSON 保存本次容差。镜像默认关闭，只在显式开启时沿底板中面（未定厚时为观测面）补建并标识为镜像来源。未识别板厚显示“未识别”，仅构建观测面。手动模式单独提供尺寸、板厚和站位输入，不自动填参考板厚；编辑/选项变化后需重建，完成前禁用 STEP 导出及偏差计算。模板选择器仅列出 H 型钢默认模板，建模方式使用独立控件。`HBeamTemplateFields` 集中组织 7 项基础参数、19 项高级参数和文本帮助，替换原油箱占位表单。所有参考算法项禁用，不接入命令服务、不保存为识别配置；展开高级参数、查看帮助不将模型标为过期，也不产生几何变更。密度等口径未明确的参数在帮助中标明，不能猜测为点数或面积密度。

操作面板共用一个 Dock，在内部切换表单：移动为 X/Y/Z，旋转含轴、角度及中心，基准面提供三点/特征面卡片，孔洞、倒角和测量提供类型及必要数值控件。当前支持编辑、复位、取消和切换，几何执行禁用；关闭仅隐藏，取消或切换操作会清理对应草稿。后续接入采用“确定提交、取消放弃”，不让输入变化直接修改正式模型。

### 原生视口、主题与依赖

`OccViewport` 通过 `ForceNoScrollArea` 放入固定中央 Dock，普通 QWidget 工具条与原生视口上下布局；下拉菜单使用独立弹窗，不在原生绘制区上叠加透明输入层。保留已有 `WinIdChange` 和 resize 处理。视图导航参考 CamAssemblyWelding 的 `AIS_ViewCube`：右上角固定锚点随设备像素比缩放，不计入工件包围盒，独立检测立方体面/棱/角；Qt 定时器驱动相机动画并同步板厚标签遮挡，手动导航会中断动画。`BottomView` 动作取当前模型（无模型时取识别结果）的底板法线与长轴，设置正交相机和屏幕向上方向后适合视图，不变换点云或模型坐标。参数表单内部可使用 `QScrollArea`，工具 Dock 从内容最小尺寸获取约束；修改布局时关注分隔条、浮动面板及窄窗口下的内容裁切和鼠标坐标，不无差别设置 `WA_NativeWindow`。

模型显示使用 `MeshData::trianglePartIndices` 和 `partIds` 拆分部件，不依赖三角形连续排列。`ModelDisplayData` 将网格与同次建模的 `ModelSummary` 转成中立显示数据，板厚只读取模型快照中的逐板识别记录；手动模型读取手动参数，不能覆盖识别模型的未识别状态；底板的 `inferred_side_plate_max` 来源传递为 `thicknessEstimated`，标签追加“（估算）”，JSON 保留该来源和未取整的厚度；部件颜色按稳定 ID 分配，未知角色不猜测板厚。`OccModelPresentation` 为各部件创建独立 AIS 网格，通过自有 `Prs3d_ShadingAspect` 设置不透明配色。每块板件取两个主表面候选锚点，`ModelLabelVisibility` 按相机方向及视线与模型网格的交点筛选可见侧；背面、掠视或被遮挡的标签不参与屏幕避让。标签和引线在 OCCT 内绘制，保持屏幕字号且不参与 FitAll 包围盒。重建或重新导入时替换原呈现，隐藏实体同步隐藏其标注。配色及标签属于显示层，不改变业务几何、STEP 数据或用户参数。

`PointCloudDisplayData` 在最多 20 万个显示采样点上计算沿 PCA 短轴的高度，使用 2% / 98% 分位数映射紫青色域；退化高度使用中间紫色。`OccViewport` 通过 `Graphic3d_ArrayOfPoints` 的逐点颜色绘制，默认开启“高度渐变”，纯色模式使用青色。只在更新点云、选区或切换模式时重建显示数组，旋转、平移和缩放不重算配色。桌面选择 PCD 后固定按 mm 提交导入，不再显示单位确认或切换弹窗。

主题入口为 [WorkbenchTheme.cpp](workbench/WorkbenchTheme.cpp) 和 [workbench.qss](workbench/resources/workbench.qss)，资源通过 `.qrc` 管理。当前采用深蓝黑背景、青蓝强调色和浅色文字：视口背景 `#081521`、面板背景 `#0B202A`、强调色 `#1EA7D8`、主文字 `#DCE4EA`。图标由 QPainter 绘制多个尺寸及禁用状态，不裁剪参考软件图标，也不额外依赖 QtSvg。主窗口与 ADS 使用统一 QSS，避免库的默认主题覆盖；Ribbon 中文字体、分组标题高度和图标尺寸需协同调整，布局尺寸使用逻辑像素。

[Dependencies.cmake](../cmake/Dependencies.cmake) 统一查找 `SARibbonBar 2.8.0` 和 `qtadvanceddocking-qt6 4.5.0`。[主程序 CMakeLists.txt](CMakeLists.txt) 仅为 `ReverseModeler` 链接 `SARibbonBar::SARibbonBar` 与 `ads::qtadvanceddocking-qt6`，并启用 `AUTOMOC`/`AUTORCC`；两个 UI 库不加入 `RM_COMMON_LIBRARIES` 或服务测试目标。继续通过安装前缀消费匹配的 SDK，不引入第三方源码构建。运行时 DLL、Qt 插件和 OCCT 资源要求见[根 README](../README.md#构建与运行)，正式分发需保留所用库的许可证文件。

## 修改代码与添加文件

头文件与源文件放在同一功能目录，各目录的同名 `.cmake` 分别使用 `RM_SOURCES` 和 `RM_HEARDERS` 汇总文件。**每个文件单独一条追加语句**：

```cmake
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/Example.cpp")
list(APPEND RM_HEARDERS "${CMAKE_CURRENT_LIST_DIR}/Example.h")
```

`RM_HEARDERS` 是当前工程约定的变量名称。两个程序的 `CMakeLists.txt` 分别初始化文件清单并创建目标；公共文件由 [ReverseModeling.cmake](ReverseModeling.cmake) 汇总。主程序专用的 `main.cpp`、`workbench` 和测试专用的 `tests.cmake`，分别在对应程序的 `CMakeLists.txt` 中加入。

| 修改内容 | 维护位置 |
|---|---|
| 点云或几何算法 | `geometry/` 及 `geometry.cmake` |
| 命令、任务或工程状态 | `commands/CommandService.*`；公共数据类型放在 `core/Types.h` |
| 板件厚度及站位识别 | `geometry/PlateRecognition.cpp` |
| 界面与显示 | `workbench/` 及 `workbench.cmake` |
| 新增公共模块 | 新建同名 `.cmake`，由 `ReverseModeling.cmake` 包含 |
| 新增第三方依赖 | 在 `Dependencies.cmake` 查找；公共链接库加入 `RM_COMMON_LIBRARIES`，程序专用库在对应 `CMakeLists.txt` 链接 |
| 新增测试 | `tests/service_tests.cpp`；新增测试文件时逐个加入 `tests/tests.cmake` |

## 单元测试

测试程序位于独立的 [tests/](../tests/) 目录，直接编译公共服务实现。现有用例覆盖单位、点云属性、逐板识别、缺测厚度、镜像、几何与偏差、取消及异常输入，以及服务版本控制、失败保留和 STEP/JSON 往返。配置、构建与运行方式见 [测试程序说明](../tests/README.md)；工程整体环境与运行说明见 [根 README](../README.md)。

## 当前能力边界

- 桌面 UI 框架及现有业务入口已接入；当前唯一模板为 H 型钢；细粒度对象选择/属性编辑、新几何操作、剖切、线框及其他工件模板算法仍待实现。各入口状态见[桌面工作台](#桌面工作台)。布局记忆不保存点云、模型和参数。
- 支持 PCD 的 ASCII、binary、binary_compressed 格式。内存中保留 XYZ、intensity、源点索引、字段名称和无效点计数，其他字段保留在原文件中。
- PCA 与底部观测面拟合提供方向；当前从单侧正交板件的连续顶边条带识别各侧板、肋板的厚度和实际站位。沿条带分段计算横向 2%–98% 宽度，以中位数汇总并记录截面四分位差。缺少连续支持或厚度不稳定时不补板件，不把投影包围厚度当成测量结果。底板尚无可靠双面测厚：两侧板均通过识别时，采用未取整厚度的最大值，并记录 `inferred_side_plate_max`，这是建模估算而非底板实测值。观测面作为底板上表面，向背侧加厚；底板横向边界使用两翼板内侧面，翼板下沿贴合底板底面，肋板底面仍接底板上表面。各翼板的纵向端部独立识别；建模时仅在容差内将底板端部突出收齐到翼板共同范围，保留超阈值突出与原始识别记录。镜像时两半翼板在底板中面分界，避免底板厚度范围内出现重复实体。任一侧板证据不足时保留底部观测面。自动双面板厚、任意曲面和局部孔洞识别仍未实现。
- 默认模型由识别的单侧板件和最大值估算的底板组成，依据不足时底板为观测面；开启镜像才补建另一侧。偏差计算针对有限矩形板件表面，包含端面与边界；任意 CAD 模型的通用距离计算尚未实现。
- PCD 导出为 binary PCD，使用 float64 XYZ 项目坐标（mm），存在 intensity 时一起保存；不包含未保留的 RGB 等字段或内部源索引。仅导出当前全量点云，不对 CAD 采样；空点云或只有模型时禁用。
- 模型导入保留实体/独立曲面及位置，展开装配实例并按部件使用本程序配色；不保留装配树、原名称/颜色、线/点对象或来源软件的编辑历史。导入模型不推测板厚，不参与矩形板件偏差算法。CWRM 当前支持 v2、`shape_type=brep`：Base64 + AES-128-ECB 零填充，解码后读取头部及 OCCT BRep；编码/版本不匹配或文件损坏时拒绝并保留当前工程。通过 OpenSSL 3 Crypto 处理，不依赖安装柏楚程序。
- PCD 和 STEP 均使用原生文件路径流，兼容 Windows 中文目录/文件名；临时文件检查完成才提交，取消或失败不覆盖原文件。
- STEP 导出检查实体有效性，并独立回读核对实体数、独立观测面数、体积和包围尺寸等结果；目标文件已存在时拒绝覆盖。
- 尚未提供工程持久化、模型操作撤销重做、自动轮廓与孔洞恢复、Python 绑定或 Web 服务。

手动参数格式示例见 [beam-parameters.json](examples/beam-parameters.json)，其中数字仅演示格式，不作为识别默认值。桌面参数通过界面编辑，当前未提供该 JSON 文件的导入入口。研究背景与后续方案见 [参考资料入口](../reference/README.md)，Python 扩展约定见 [Python 脚本编码规范](../docs/Python脚本编码规范.md)。
