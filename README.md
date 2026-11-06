# ReverseModeling

点云逆向建模工程与研究资料。当前工程代码位于 [ReverseModeling](ReverseModeling/README.md)，使用 CMake / C++20，唯一产品入口为 Qt 桌面程序 `ReverseModeler`，始终包含 GUI。

第三方库由使用者预先准备，通过 `CMAKE_PREFIX_PATH` 和标准 `find_package()` 接入，可来自 vcpkg 或其他编译方式，也可分布在不同安装目录。工程只声明所需库、版本和组件，不管理第三方库的编译、安装或发布。参考文档和方案的采用状态统一见下方资料入口。

## 文档入口

| 入口 | 内容 |
|---|---|
| [主业务开发指南](ReverseModeling/README.md) | 代码结构、调用流程、数据约定与能力边界 |
| [桌面工作台开发约定](ReverseModeling/README.md#桌面工作台) | SARibbon + ADS、窗口标识、布局恢复与 UI 接入边界 |
| [单元测试程序](tests/README.md) | Google Test 构建、运行与用例选择 |
| [启动命令使用指南](docs/启动命令使用指南.md) | Git Bash 启动参数、实际路径示例、启动录制与脚本回放 |
| [文件导入导出与格式转换](docs/文件导入导出与格式转换.md) | PCD、STEP、CWRM 导入导出，模型采样及 file.convert 用法 |
| [命令框架与 Python 使用指南](docs/命令框架与Python使用指南.md) | 命令窗口、help、Python、宏录制、rm_define 和代码生成 |
| [研究与实现参考资料](reference/README.md) | 参考文档目录、实施背景、维护约定与原始材料归档说明 |
| [C++ 编码规范](docs/cpp编码规范.md) | C++、Qt、CMake 与测试维护约定 |
| [Python 脚本编码规范](docs/Python脚本编码规范.md) | CPython SDK、脚本写法与宿主接口边界 |
| [envBuild 命令使用指南](docs/envBuild命令使用指南.md) | 首次配置、编译与运行、补部署、清理、打包交付及常见问题 |

## 目录结构

```text
PointCloudReverseModeling/
├── CMakeLists.txt         # 总入口，加载主业务和单元测试两个子项目
├── CMakePresets.json      # 全仓库唯一的 CMake 预设
├── envBuild.py            # 构建、运行、清理和打包命令入口
├── envBuild.example.ini   # 本机配置的模板，init 生成 envBuild.local.ini
├── pythonProject/         # Python 命令编排模块（不随产品分发）
├── README.md
├── .gitignore
├── cmake/                # 公共构建设置与全部第三方依赖查找
├── ReverseModeling/      # 主业务子项目，生成 ReverseModeler
│   ├── CMakeLists.txt
│   ├── ReverseModeling.cmake  # 公共服务文件清单入口
│   ├── main.cpp
│   ├── commands/
│   ├── core/
│   ├── geometry/
│   ├── workbench/
│   └── examples/
├── tests/                # 单元测试子项目，生成 ReverseServiceTests
│   ├── CMakeLists.txt
│   ├── README.md
│   ├── tests.cmake
│   └── service_tests.cpp
├── docs/
│   ├── cpp编码规范.md
│   └── Python脚本编码规范.md
└── reference/
    ├── README.md         # 研究与实现参考资料入口
    └── ...               # 研究与实现参考文档，含配套 Word 阅读版本
```

每个包含 C++ 文件的目录维护一个同名 `.cmake`，通过 `RM_SOURCES` 和 `RM_HEARDERS` 分别追加源文件与头文件，每个文件单独一条 `list(APPEND ...)`。主程序入口 `ReverseModeling/main.cpp` 在主程序的 `CMakeLists.txt` 中单独追加。各程序的 `CMakeLists.txt` 组合公共及专用文件清单，设置头文件搜索目录和链接库；所有第三方库的 `find_package()` 集中在 `cmake/Dependencies.cmake`。

## 构建与运行

### 环境与依赖

当前 Windows 预设使用 CMake 3.25+、Visual Studio 2022 MSVC v143、x64 和 C++20。依赖库需与工具链、架构及 Debug/Release 运行库匹配：

| 依赖 | 用途 |
|---|---|
| Eigen3 | PCA 与线性代数 |
| PCL 1.15+，common/io | PCD 点云读取 |
| OpenSSL 3，Crypto | CWRM v2 文件编解码；分发时包含匹配的 Crypto 运行库及其 Apache-2.0 许可证 |
| OCCT 8+ | 几何建模、显示及 STEP 交换 |
| Qt6 Core/Gui/Widgets | 桌面界面 |
| SARibbon 2.8.0+ | 顶部 Ribbon 功能区；本机包要求 Qt 6.11+ |
| Qt Advanced Docking System 4.5.0+（Qt6） | 工具窗口停靠、浮动、显隐与布局恢复 |
| nlohmann_json 3.11+ | JSON 数据契约 |
| Google Test | `ReverseServiceTests` 测试程序 |
| CPython / pybind11 | 产品内建 Python；采用 SDK 的 `CPython::Python` 和匹配的自由线程 ABI |
| Python / Jinja2（仅构建机） | `RmCommandGen` 生成参数描述类与文档，安装方法见命令框架指南 |

所有第三方包由 [Dependencies.cmake](cmake/Dependencies.cmake) 统一查找，系统线程通过 `Threads::Threads` 接入。库可由 vcpkg 或其他方式预先构建；本工程不负责下载或编译第三方库。

根 [CMakePresets.json](CMakePresets.json) 保留通用工具链设置。先执行 `python envBuild.py init`，再在未纳入 Git 的 `envBuild.local.ini` 中填写本机 SDK 的实际安装前缀 `dependencies.prefix_paths`，可每行填写一个前缀；填写安装根目录，而非 `bin` 子目录。工程和 SDK 可以放在各自选择的位置，无需采用固定盘符或目录结构。已有构建时 init 会采用缓存中的 SDK；已有本地配置不覆盖。

`envBuild.py` 入口使用 SDK 的 Python 3.15t 和标准库，无需 pip。下文的 `python` 应指向该解释器；也可使用其完整路径。命令代码生成器另需构建机 Jinja2 环境，见[命令框架指南](docs/命令框架与Python使用指南.md#4-构建依赖和生成)。CMake/CPack 从 PATH 或本地配置定位，CDB 仅 `debug` 启动命令需要，填写原生 `cdb.exe` 路径。脚本不下载或编译第三方 SDK。

### 命令行

在你克隆或解压工程的根目录（包含 `envBuild.py` 和 `CMakeLists.txt`）打开终端，无需进入 `pythonProject`。完整参数、配置文件写法和更多示例见 [envBuild 命令使用指南](docs/envBuild命令使用指南.md)：

```powershell
python envBuild.py -h
python envBuild.py init
python envBuild.py info
python envBuild.py compile release
python envBuild.py release
python envBuild.py packageAll release
```

| 命令 | 作用 |
|---|---|
| `info` / `init` | 查看环境 / 创建缺失的本地配置 |
| `compile [模式]` | 配置、编译并自动部署；默认编译主程序与单元测试，不运行测试 |
| `release` / `debug` | 直接运行 Release / 用 CDB 调试 Debug；均不编译 |
| `prepareRun [模式]` | 手动补部署 DLL、Qt 插件和 OCCT 资源，不编译 |
| `package [模式]` | 将已有主程序及依赖通过 CMake 安装规则打成 ZIP，不编译 |
| `packageAll [模式]` | 编译主程序、自动部署并打包 |
| `clean [类别] [模式]` | 默认仅清理编译产物；具体删除与保留范围见 `clean -h` |

模式支持 `release`、`debug`、`release_debug`，初始默认 Release。全部命令支持 `-h/--help`；空命令显示总帮助，输错命令或参数显示相关帮助并返回 2。有副作用的命令支持 `--dry-run`。`compile` 支持 `--target ReverseModeler` / `ReverseServiceTests`、`--jobs`、`--rebuild`、`--fresh`；更换 SDK 后需显式 `--fresh` 重建缓存。

中间文件位于 `build/vc143-cpp20/`，当前 Release 可执行文件位于 `output/vc143-cpp20/release/`。一般输出规则为 `output/<编译器>-cpp<标准>/<构建模式>/`，由 [common.cmake](cmake/common.cmake) 设置。

构建、测试和手工验证时留存的日志及配套诊断记录统一放在 `output/log/`，不散放在 `output/` 根目录；日志不提交 Git、不进入发布包。命令默认输出到终端，保存示例见 [命令使用指南](docs/envBuild命令使用指南.md#6-产物在哪怎样交付)。

主程序正常构建后自动部署匹配配置的 DLL、Qt 插件、`qt.conf`、OCCT 资源和 SDK 许可材料，可直接双击 EXE。无需注入 SDK PATH，也无需再执行 `deployqt`（该命令已取消）。`prepareRun` 仅用于文件缺失、依赖更新等手动补部署场景；部署失败时编译命令会返回失败。运行中 EXE 被占用时先自行关闭程序，不会自动强杀进程。

```powershell
& './output/vc143-cpp20/release/ReverseModeler.exe'
& './output/vc143-cpp20/release/ReverseModeler.exe' -c help cloud.import
& './output/vc143-cpp20/release/ReverseModeler.exe' -f 'D:/scripts/建模.py' aa 'bb cc'
python envBuild.py prepareRun release
ctest --preset windows-release
python -m unittest discover -s tests/python -v
```

主程序启动支持 `--input_file 点云.pcd`、`--output_dir 输出目录`、`--record_file 录制.py`、`-c 命令 [参数...]`、`-f 脚本.py [参数...]` 和 `-h/--help`。只优先提取这三个指定的全局路径键，再解析 `-c/-f`；`--spacing` 等业务参数保留。指定输出目录后，顶部“导出并退出”将完整模型交付为 `reserveModel.step` 和两字段 `result.json`，成功提交才退出；普通导出和关闭不标记成功。`-h/--help` 在终端打印用法后直接退出，不打开 GUI；`--record_file` 最先开始录制，已有文件直接覆盖并打印日志；启动路径会转换为命令写入脚本。`project.export_and_exit` 交付后保存录制并退出；其他 `-c/-f` 执行完保持工作台打开，脚本通过 `sys.argv[1:]` 读取参数。更多示例见[启动参数说明](docs/命令框架与Python使用指南.md#启动时执行命令或脚本)。

Google Test 的用例筛选见 [测试程序说明](tests/README.md)。测试目标也会部署自身运行库。

ZIP 和 SHA256 校验文件位于 `output/packages/`。发布包仅收集安装规则指定的主程序和运行环境，不包含测试程序、用户点云/模型和旧 EXE。解压后运行 `ReverseModeler/bin/ReverseModeler.exe`；Debug 包仅用于内部开发，包含主程序 PDB。

不使用 Python 时，同一套 CMake 规则仍可直接使用：

```powershell
cmake --preset vc143-cpp20 -DCMAKE_PREFIX_PATH="<SDK 安装前缀>"
cmake --build --preset windows-release
cmake --build build/vc143-cpp20 --config Release --target prepare_runtime
cmake --install build/vc143-cpp20 --config Release --prefix output/install/release
cpack --config build/vc143-cpp20/CPackConfig.cmake -C Release -G ZIP -B output/package-staging/manual-release
```

`cmake --install` 和直接 `cpack` 均不编译；已有主程序和匹配 SDK 必须可用。完整部署规则见 [Runtime.cmake](cmake/Runtime.cmake)，安装/打包规则见 [Packaging.cmake](cmake/Packaging.cmake)。

### Qt Creator

打开仓库根目录的 `CMakeLists.txt`，选择 `Windows / Qt desktop + Google Test / MSVC v143 / C++20` 预设，并在运行配置中选择 `ReverseModeler` 或 `ReverseServiceTests`。桌面程序使用默认本机部署配置；若部署步骤选中了 Application Manager，应在 **Deploy Settings** 中切换为默认部署或停用其安装步骤。

在 Qt Creator 的 CMake 配置中填写同一 `CMAKE_PREFIX_PATH`。首次链接及默认增量构建均会检查部署；只构建主目标且未重新链接时，可构建 `prepare_runtime` 补齐运行文件。VS 多配置目录共用缓存，`--fresh` 会重新配置 Debug/Release 的共同构建信息。

## 桌面工作台

顶部 Ribbon 按文件、编辑、点云、重建、模型修改和工具分组，中央为三维视图，底部状态栏显示任务进度并提供取消入口。通过 **窗口** 菜单显示或隐藏模型树、属性、重建参数、操作面板、命令窗口及任务信息；工具窗口可拖动、浮动或合并为标签。默认左侧整列为重建参数和操作面板标签组，激活操作面板；中央上方为三维视图、下方为命令窗口，两者大致等高；右侧上下分别显示模型树和属性，两者大致等高。命令窗口仅位于中央列，左侧工具页贯穿工作区全高。任务信息默认隐藏，后续启动恢复上次布局。关闭工具窗口保留内容，命令窗口统一显示命令与运行日志；“恢复默认布局”恢复上述排列，不清空当前数据。正常退出保存窗口布局，点云、模型和参数仍只保存在本次运行的内存中。

模型树按实际部件展开，显示名称、颜色和板厚。生成或导入模型后自动切到 **可选：体**：左键点击三维部件会高亮整件、定位对应树节点并显示只读属性；点击树节点也会高亮对应部件。属性包含部件 ID、板厚及依据、网格包围盒和来源等，尺寸统一为 mm；导入 STEP/CWRM 没有板厚信息时显示“未提供”。点击空白或按 Esc 清空部件选择；需要框选点云时切回 **可选：点**。选择过滤支持 **点 / 线 / 面 / 体**：单击分别选取模型真实顶点、拓扑边、完整几何面或整个部件，四种类型均支持批量选择：普通点击替换，Shift（或 Ctrl）点击追加，再点已选对象取消；工具条加选/减选同样适用。所有选中对象同时高亮，属性框显示类型、数量及各对象编号；切换拾取类型或按 Esc 清空选择。点模式下拖动仍框选点云，模型的三角网格细分线不作为可选边。画框后可左键拖动四边/四角调整大小，拖动框内部移动位置；松开后更新选中点，加选/减选也支持调整最近一次框。

选择 PCD 文件后直接按 **毫米 mm** 导入，不再弹出单位确认窗口。打开菜单同时接受 STEP/STP、柏楚 CWRM v2 BRep 模型：STEP 自动将声明单位换算为 mm，CWRM 按 mm 读取。模型导入保留实体和曲面几何及位置，采用浅灰实体与深色轮廓；不恢复柏楚点云、参数模板或板厚标注。导出菜单支持当前点云 PCD、模型 STEP/CWRM，以及单独的“模型采样导出 (*.pcd)”。当前点云导出保存完整点云（含截取/删除结果），不是显示采样；模型采样点云不等于原始扫描数据。`file.convert` 支持 STEP/CWRM 互转和模型采样为 PCD，禁止任何 PCD 输入（包括 PCD 转 PCD），正常点云导入导出不受影响。详见[文件导入导出与格式转换](docs/文件导入导出与格式转换.md)。

当前模板仅提供 **H型钢 / H型钢（默认）**（含筋板），桌面重建固定使用点云板件识别，不再提供建模方式选择或手动参数建模入口。重建参数面板保留镜像对称、端部突出容差、识别腹板孔和忽略小孔半径四项设置，采样/噪声/轮廓容差自动估计；不再展示未接入的基础/高级识别参数，移除文件/点数信息、识别表格及模型摘要；板件厚度与依据可在模型树和属性面板查看。“镜像对称”默认关闭，显式勾选才沿腹板中面（未定厚时为观测面）补建另一侧。STEP 导出继续可用；含识别孔或拟合轮廓的模型不使用旧矩形板件偏差公式。基准面已支持三点/特征面设置、预览确认、正视以及点云剖视（偏移、反向）；命令可录制回放。移动、旋转已支持实体变换及原位置预览。圆角/倒角按等厚板件的平面轮廓处理，板厚不变，支持跨部件多边、回车或按钮统一预览和应用，整批失败保持原模型；添加孔已支持圆形/三角形过焊孔及圆形/矩形底边孔，具备底边选择、异步预览、应用和命令录制。模型调整中的“修改板厚”支持选中一个或多个等厚实体板件，读取实际板厚，保持中面并向两侧等量增减；回车或“预览”只看效果，“应用”才修改实体。修改后的板厚同步显示在模型树、属性及标签中，并支持撤销、重做、命令录制和 STEP 导出。“外扩板件轮廓”支持多个等厚板件在板面内等距外扩，保持板厚和封闭内孔，提供预览、对比原模型和应用；命令为 `model.expand_contour`。

在“可选：体”模式选中一个模型后右键，延长/截取/合并/分割到板只需再点选一个目标板，立即执行；框选切割端部在松开鼠标时执行；修复单个或全部板件孔洞点击即执行。这些操作不打开操作面板，不需要填写数值、预览或点击应用。菜单悬停显示操作前后示意图及文字说明，多选时单件操作禁用；修复全部不依赖当前选择。支持 Ctrl+Z 撤销、重做和命令录制。批量过焊孔保留原有参数、预览和应用流程。具体用法与适用边界见 [板件编辑说明](ReverseModeling/README.md#右键板件编辑)。

视图工具条中“点云”和“实体”的主按钮控制显隐，箭头展开显示模式菜单；未接入的模式保持禁用。“停止重建”只取消当前重建任务，导入、导出及偏差计算通过状态栏的“取消任务”取消。窗口显隐通过顶部功能区的“窗口”菜单管理。

“打开”下拉菜单提供 **最近打开文件**，按成功导入时间列出最近 10 个点云或模型文件，点击按相应格式直接重开。重复打开移到首位，记录在重启后保留；失效路径标注为“文件不存在”并禁用。“清空记录”只清除历史列表，不删除文件。

视图区右上角提供中文视图立方体，点击面切换前后、左右、上下视角，点击棱或角切换斜视角；立方体随相机旋转显示方向。其上方“底板面”按钮按当前模型或点云识别底板的法线正视，并将工件长轴横向放置、自动适合视图，仅改变相机。未得到板件识别结果或模型时该按钮禁用。

点击 **测量** 进入两点测量：依次左键点选可见模型表面或点云，显示总距离和 X/Y/Z 分量（mm，保留两位小数），轴向线分别为红、绿、蓝色。选定第一点后移动鼠标可预览第二点，第三次点击开始下一组；右键菜单“退出测量”、再次点击测量按钮或 Esc 退出。测量期间仍支持旋转、平移、缩放和视角切换。XYZ 是工程坐标轴方向的绝对坐标差，不随相机旋转；模型优先捕获鼠标周围 16 个逻辑像素内可见的真实顶点/边端点，橙色标记表示已吸附；超出范围回退到显示三角面交点。点云取当前显示采样中的实际点。结果写入命令窗口，仅用于观察，不修改模型、板厚或导出结果；重新加载数据、开始任务或隐藏测量对象后清除。

当前直接导入已有 PCD 进行板件重建，不提供扫描分段拼接或二维图像识别选区；因此移除了“自动对齐”“拼接时去重”“2D 识别精度”及对应的“更多”菜单。柏楚中前两项用于对齐/拼接扫描数据，后一项控制图像识别掩膜边界，并非本工程的重建精度参数。适用条件和原生调用依据见[柏楚流程分析](reference/CypModeler-点云导入重建与CWRM导出分析.md#15-自动对齐拼接去重与2d识别精度补充2026-11-05)。

点云默认使用紫色到青色的高度渐变，沿 PCA 截面短轴区分不同高度，旋转视图不改变点的颜色。“点云”下拉菜单可切换到柔和青色的纯色显示。颜色用于观察层次，不代表已识别出具体部件；显示采样不改变原始点云或建模输入。

生成和导入的模型统一使用浅灰色哑光表面与深色拓扑轮廓；选中部件使用浅青填充、蓝色边框，板厚标签橙底白字。视口按显卡能力启用最高 8 倍 MSAA，减少斜边和高亮轮廓的像素锯齿。默认显示当前视角可见侧的板厚标签（mm），背面及被模型遮蔽的标注隐藏，旋转到另一面时随视角切换；可通过“板厚”开关整体隐藏。侧板、肋板标签来自点云顶边宽度识别结果，显示到 0.1 mm。板厚标签、模型树、属性与板厚输入框统一保留一位小数，不追加“估算”提示。底板厚度取两块侧板原始识别厚度的最大值，表格依据显示“侧板最大值”；例如 20.1 mm 与 17.3 mm 取 20.1 mm。底板从观测面向背侧加厚，宽度夹在两侧翼板内侧面之间；翼板下沿贴合底板底面，肋板仍落在底板上表面。缺少任一侧板的可靠识别结果时仍标为“未识别”，仅保留观测面。识别值属于点云估计，不能直接等同于名义规格或计量验收结果。可见标签在重叠时错开并用引线关联，视图过密时可放大查看。

“端部突出容差”是已接入建模的独立选项，默认 **30 mm**：底板沿梁长方向超出两翼板共同端部且不超过容差时收齐，超出容差则保留并在日志提示；设为 0 可关闭端部收齐。各翼板保留独立识别长度，调整后需重新重建，原始点云不变。此选项与仍待接入的平板/竖板投影参数不同。

“撤销 / 重做 / 重置”共用最多 10 步工程历史，覆盖点云编辑、模型重建、部件删除及已确认基准面。重置恢复最近一次导入状态，并可撤销此次重置；新导入建立新历史边界。对应命令为 `project.undo`、`project.redo`、`project.reset`，录制时逐条追加，中途开始录制自动补写当前会话前序操作，保留可回放的撤销/重做分支。详细规则见[命令使用指南](docs/命令框架与Python使用指南.md#撤销重做重置与录制)。

当前识别面向俯视线扫的单侧、近似正交 H 型钢：两翼板及筋板独立估计板厚和高度，低于翼板的筋板也参与识别；短筋板与同站位的可见断带不再统一拉满。连续可见顶边按自动容差拟合直线/圆弧；俯视可见的腹板圆孔及长圆孔支持自动识别和半径过滤，筋板上方可见的四分之一圆缺口保留；下方遮挡缺口和背面连接不凭先验补造。具体证据与边界见[俯视线扫优化说明](ReverseModeling/README.md#俯视线扫-h-型钢识别)。STEP 可同时包含已识别板件实体和未确定厚度的独立观测面，导出会分别核对数量。

运行环境需要匹配的 `SARibbonBar.dll`、`qtadvanceddocking-qt6.dll`；默认 SDK 的 Qt 为 6.11.2，不能混用参考平台的 Qt 6.8.4 DLL。快捷键：`Ctrl+O` 打开点云或模型、`Ctrl+P` 重建参数、`Ctrl+Shift+E` 导出 STEP、`F` 适合视图。

窗口生命周期、主题资源和后续功能接入约定见[桌面工作台开发说明](ReverseModeling/README.md#桌面工作台)。

### 局部重建

已经重建后发现多余点云时，可先截取/删除点云，再切换“可选：体”选择需要修正的部件，点击“局部重建”。选择规则并预览，满意后应用。Shift 支持多选。只替换选中的部件，未选部件的几何、位置、板厚与 ID 保持不变；整批任一部件失败则不提交，原因输出到命令窗口。

- **平板（保持原板面）**：使用原板件关联的剩余点云拟合外轮廓，保留原方向和当前板厚，适合清理点云后的局部修正。
- **水平板（基准面方向）**：沿当前基准面法向；没有基准面时采用世界 Z。与该方向明显不符的点云会报错，避免把竖板压成水平板。
- **包围盒**：按原板件方向包络剩余支持点，三方向均须有有效跨度。
- 轮廓支持矩形、包络圆、倒角矩形、凸多边形、任意多边形及连续曲线。倒角退距和凹轮廓容差可调整；稀疏、分离或自交轮廓会明确报错。仅拟合外轮廓，不重新识别内孔。

预览不改模型、不录制；应用通过 `model.reconstruct_local` 命令执行，支持撤销、重做及脚本录制/回放。例如：

```text
model.reconstruct_local --parts '["web"]' --type plate --contour rectangle
```

部件必须具备原始点云关联，导入 STEP/CWRM、手工新增或缺少支持点的部件不能局部重建。关联按首次重建时板面附近的原始点索引建立，后续只使用仍保留的点；已移动/旋转的部件须先撤销变换。选中部件已有的开孔、圆角或外扩等手工修改会由新轮廓替代，不影响未选部件。
