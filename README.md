# ReverseModeling

点云逆向建模工程与研究资料。当前工程代码位于 [ReverseModeling](ReverseModeling/README.md)，使用 CMake / C++20，唯一产品入口为 Qt 桌面程序 `ReverseModeler`，始终包含 GUI。

第三方库由使用者预先准备，通过 `CMAKE_PREFIX_PATH` 和标准 `find_package()` 接入，可来自 vcpkg 或其他编译方式，也可分布在不同安装目录。工程只声明所需库、版本和组件，不管理第三方库的编译、安装或发布。参考文档和方案的采用状态统一见下方资料入口。

## 文档入口

| 入口 | 内容 |
|---|---|
| [主业务开发指南](ReverseModeling/README.md) | 代码结构、调用流程、数据约定与能力边界 |
| [桌面工作台开发约定](ReverseModeling/README.md#桌面工作台) | SARibbon + ADS、窗口标识、布局恢复与 UI 接入边界 |
| [单元测试程序](tests/README.md) | Google Test 构建、运行与用例选择 |
| [研究与实现参考资料](reference/README.md) | 参考文档目录、实施背景、维护约定与原始材料归档说明 |
| [C++ 编码规范](docs/cpp编码规范.md) | C++、Qt、CMake 与测试维护约定 |
| [Python 脚本编码规范](docs/Python脚本编码规范.md) | CPython SDK、脚本写法与未来宿主接口边界 |
| [envBuild 命令使用指南](docs/envBuild命令使用指南.md) | 完整命令参数、本机配置填写、常用示例与问题处理 |
| [envBuild 构建运行与打包方案](docs/envBuild构建运行与打包方案.md) | 已实现的命令约定、自动部署、安装及 CPack ZIP |

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
| OpenSSL 3，Crypto | CWRM v2 文件解码；分发时包含匹配的 Crypto 运行库及其 Apache-2.0 许可证 |
| OCCT 8+ | 几何建模、显示及 STEP 交换 |
| Qt6 Core/Gui/Widgets | 桌面界面 |
| SARibbon 2.8.0+ | 顶部 Ribbon 功能区；本机包要求 Qt 6.11+ |
| Qt Advanced Docking System 4.5.0+（Qt6） | 工具窗口停靠、浮动、显隐与布局恢复 |
| nlohmann_json 3.11+ | JSON 数据契约 |
| Google Test | `ReverseServiceTests` 测试程序 |

所有第三方包由 [Dependencies.cmake](cmake/Dependencies.cmake) 统一查找，系统线程通过 `Threads::Threads` 接入。库可由 vcpkg 或其他方式预先构建；本工程不负责下载或编译第三方库。

根 [CMakePresets.json](CMakePresets.json) 保留通用工具链设置，不再写死本机 SDK。先执行 `python envBuild.py init`，再在未纳入 Git 的 `envBuild.local.ini` 中填写 `dependencies.prefix_paths`，可每行填写一个前缀。已有构建时 init 会采用缓存中的 SDK；已有本地配置不覆盖。本机示例为 `D:/projectWQZ/LibPackByVcpkgWindows/install_vc143_cpp20/x64-windows-v143-cpp20`。

Python 入口使用 SDK 的 Python 3.15t 和标准库，无需 pip。下文的 `python` 应指向该解释器；也可使用其完整路径。CMake/CPack 从 PATH 或本地配置定位，CDB 仅 `debug` 启动命令需要，填写原生 `cdb.exe` 路径。脚本不下载或编译第三方 SDK。

### 命令行

从仓库根目录运行，无需进入 `pythonProject`。完整参数、配置文件写法和更多示例见 [envBuild 命令使用指南](docs/envBuild命令使用指南.md)：

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
python envBuild.py prepareRun release
ctest --preset windows-release
python -m unittest discover -s tests/python -v
```

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

顶部 Ribbon 按文件、编辑、点云、重建、模型修改和工具分组，中央为三维视图，底部状态栏显示任务进度并提供取消入口。通过 **窗口** 菜单显示或隐藏模型树、属性、重建参数、操作面板、日志及任务信息；工具窗口可拖动、浮动或合并为标签。右侧默认上下显示模型树和属性，其他辅助窗口默认收起，后续启动恢复上次布局。关闭工具窗口保留内容，日志隐藏时继续收集最近 2000 个文本块；“恢复默认布局”恢复右侧模型树和属性、收起其他辅助窗口，不清空当前数据。正常退出保存窗口布局，点云、模型和参数仍只保存在本次运行的内存中。

模型树按实际部件展开，显示名称、颜色和板厚。生成或导入模型后自动切到 **可选：体**：左键点击三维部件会高亮整件、定位对应树节点并显示只读属性；点击树节点也会高亮对应部件。属性包含部件 ID、板厚及依据、网格包围盒和来源等，尺寸统一为 mm；导入 STEP/CWRM 没有板厚信息时显示“未提供”。点击空白或按 Esc 清空部件选择；需要框选点云时切回 **可选：点**。

选择 PCD 文件后直接按 **毫米 mm** 导入，不再弹出单位确认窗口。打开菜单同时接受 STEP/STP、柏楚 CWRM v2 BRep 模型：STEP 自动将声明单位换算为 mm，CWRM 按 mm 读取。模型导入保留实体和曲面几何及位置，按部件配色；不恢复柏楚点云、参数模板或板厚标注。导出菜单提供 PCD、STEP：PCD 保存当前完整点云（含截取/删除结果），不是显示采样；没有点云时禁用 PCD 导出。导入模型可直接另存 STEP，通用 CAD 偏差和底板识别暂不提供。

当前模板仅提供 **H型钢 / H型钢（默认）**（含肋板）；“建模方式”单独选择点云识别或手动建模。默认进入“点云板件识别”，核对逐板厚度、状态和位置后执行“开始重建”。H 型钢模板的基础、高级参数按参考界面分组，未接入算法的项禁用并提供“?”说明；这些显示值不替代识别板厚，也不影响重建结果。“镜像对称”默认关闭，只有显式勾选才沿底板中面（未定厚时为观测面）补建另一侧。需要手工输入时切换“手动参数建模”，该模式不自动填板厚。现有 STEP 导出和偏差计算继续可用。移动、旋转、基准面、孔洞等目前提供表单，几何执行标注为待接入。

视图工具条中“点云”和“实体”的主按钮控制显隐，箭头展开显示模式菜单；未接入的模式保持禁用。“停止重建”只取消当前重建任务，导入、导出及偏差计算通过状态栏的“取消任务”取消。窗口显隐通过顶部功能区的“窗口”菜单管理。

“打开”下拉菜单提供 **最近打开文件**，按成功导入时间列出最近 10 个点云或模型文件，点击按相应格式直接重开。重复打开移到首位，记录在重启后保留；失效路径标注为“文件不存在”并禁用。“清空记录”只清除历史列表，不删除文件。

视图区右上角提供中文视图立方体，点击面切换前后、左右、上下视角，点击棱或角切换斜视角；立方体随相机旋转显示方向。其上方“底板面”按钮按当前模型或点云识别底板的法线正视，并将工件长轴横向放置、自动适合视图，仅改变相机。未得到板件识别结果或模型时该按钮禁用。

点云默认使用紫色到青色的高度渐变，沿 PCA 截面短轴区分不同高度，旋转视图不改变点的颜色。“点云”下拉菜单可切换到柔和青色的纯色显示。颜色用于观察层次，不代表已识别出具体部件；显示采样不改变原始点云或建模输入。

生成的模型按部件分别配色，腹板、两块翼板及各肋板使用稳定的非金色颜色。默认显示当前视角可见侧的板厚标签（mm），背面及被模型遮蔽的标注隐藏，旋转到另一面时随视角切换；可通过“板厚”开关整体隐藏。侧板、肋板标签来自点云顶边宽度识别结果，显示到 0.1 mm。底板厚度取两块侧板原始识别厚度的最大值，标签注明“估算”，表格依据显示“侧板最大值（估算）”；例如 20.1 mm 与 17.3 mm 取 20.1 mm。底板从观测面向背侧加厚，宽度夹在两侧翼板内侧面之间；翼板下沿贴合底板底面，肋板仍落在底板上表面。缺少任一侧板的可靠识别结果时仍标为“未识别”，仅保留观测面。识别值属于点云估计，不能直接等同于名义规格或计量验收结果；手动模式则显示明确输入的参数。可见标签在重叠时错开并用引线关联，视图过密时可放大查看。

“端部突出容差”是已接入建模的独立选项，默认 **30 mm**：底板沿梁长方向超出两翼板共同端部且不超过容差时收齐，超出容差则保留并在日志提示；设为 0 可关闭端部收齐。各翼板保留独立识别长度，调整后需重新重建，原始点云不变。此选项与仍待接入的平板/竖板投影参数不同。

当前识别面向带连续可见顶边的单侧、近似正交直板结构：分别估计两条纵向侧板和实际横向肋板的厚度、位置，不再使用固定三分点站位。缺失顶边、孔洞、曲面和弯曲轮廓不会被自动补全。STEP 可同时包含已识别板件实体和未确定厚度的独立观测面，导出会分别核对数量。

运行环境需要匹配的 `SARibbonBar.dll`、`qtadvanceddocking-qt6.dll`；默认 SDK 的 Qt 为 6.11.2，不能混用参考平台的 Qt 6.8.4 DLL。快捷键：`Ctrl+O` 打开点云或模型、`Ctrl+P` 重建参数、`Ctrl+Shift+E` 导出 STEP、`F` 适合视图。

窗口生命周期、主题资源和后续功能接入约定见[桌面工作台开发说明](ReverseModeling/README.md#桌面工作台)。
