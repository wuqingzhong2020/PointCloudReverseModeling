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

## 目录结构

```text
PointCloudReverseModeling/
├── CMakeLists.txt         # 总入口，加载主业务和单元测试两个子项目
├── CMakePresets.json      # 全仓库唯一的 CMake 预设
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
| OCCT 8+ | 几何建模、显示及 STEP 交换 |
| Qt6 Core/Gui/Widgets | 桌面界面 |
| SARibbon 2.8.0+ | 顶部 Ribbon 功能区；本机包要求 Qt 6.11+ |
| Qt Advanced Docking System 4.5.0+（Qt6） | 工具窗口停靠、浮动、显隐与布局恢复 |
| nlohmann_json 3.11+ | JSON 数据契约 |
| Google Test | `ReverseServiceTests` 测试程序 |

所有第三方包由 [Dependencies.cmake](cmake/Dependencies.cmake) 统一查找，系统线程通过 `Threads::Threads` 接入。库可由 vcpkg 或其他方式预先构建；本工程不负责下载或编译第三方库。

根 [CMakePresets.json](CMakePresets.json) 的 `CMAKE_PREFIX_PATH` 默认指向 `D:/projectWQZ/LibPackByVcpkgWindows/install_vc143_cpp20/x64-windows-v143-cpp20`。其他环境可修改该值，或在配置时通过 `-DCMAKE_PREFIX_PATH="<安装前缀1>;<安装前缀2>"` 覆盖；也可在 Qt Creator 的 CMake 配置中修改。更换工具链或依赖位置后，使用新的构建目录，或运行 `cmake --fresh --preset vc143-cpp20` 重新配置。

### 命令行

在 VS 2022 Developer PowerShell 中从仓库根目录运行。执行 CTest 前，先将对应配置的依赖 DLL 目录加入 `PATH`：

```powershell
cmake --preset vc143-cpp20
cmake --build --preset windows-release
ctest --preset windows-release
```

只需编译一个目标时，给构建命令追加 `--target ReverseModeler` 或 `--target ReverseServiceTests`。Google Test 的运行和筛选见 [测试程序说明](tests/README.md)。

中间文件位于 `build/vc143-cpp20/`，当前 Release 可执行文件位于 `output/vc143-cpp20/release/`。一般输出规则为 `output/<编译器>-cpp<标准>/<构建模式>/`，由 [common.cmake](cmake/common.cmake) 设置。

运行或执行 CTest 前，将对应配置的依赖 DLL 目录加入 `PATH`，并按安装布局配置 Qt 插件和 OCCT 资源。`CMAKE_PREFIX_PATH` 只用于配置阶段的依赖查找；Debug 和 Release 需使用各自匹配的运行库。配置好运行环境后：

```powershell
& './output/vc143-cpp20/release/ReverseModeler.exe'
```

### Qt Creator

打开仓库根目录的 `CMakeLists.txt`，选择 `Windows / Qt desktop + Google Test / MSVC v143 / C++20` 预设，并在运行配置中选择 `ReverseModeler` 或 `ReverseServiceTests`。桌面程序使用默认本机部署配置；若部署步骤选中了 Application Manager，应在 **Deploy Settings** 中切换为默认部署或停用其安装步骤。

`cmake --install` 仅安装主程序；独立运行所需的 DLL、Qt 插件和 OCCT 资源需要按实际发布方式另行准备。

## 桌面工作台

顶部 Ribbon 按文件、编辑、点云、重建、模型修改和工具分组，中央为三维视图，底部状态栏显示任务进度并提供取消入口。通过 **窗口** 菜单显示或隐藏模型树、属性、重建参数、操作面板、日志及任务信息；工具窗口可拖动、浮动或合并为标签。首次启动辅助窗口默认收起，后续启动恢复上次布局。关闭工具窗口保留内容，日志隐藏时继续收集最近 2000 个文本块；“恢复默认布局”收起辅助窗口，不清空当前数据。正常退出保存窗口布局，点云、模型和参数仍只保存在本次运行的内存中。

打开 PCD 后明确选择原始单位，核对重建参数，再执行“开始重建”。参数编辑后点击“应用参数”；直接开始重建时也会提示应用尚未提交的编辑。现有 STEP 导出和偏差计算继续可用。移动、旋转、基准面、孔洞等目前提供表单，几何执行标注为待接入。

视图工具条中“点云”和“实体”的主按钮控制显隐，箭头展开显示模式菜单；未接入的模式保持禁用。“停止重建”只取消当前重建任务，导入、导出及偏差计算通过状态栏的“取消任务”取消。窗口入口也位于视图工具条旁，独立于工具条溢出区域。

运行环境需要匹配的 `SARibbonBar.dll`、`qtadvanceddocking-qt6.dll`；默认 SDK 的 Qt 为 6.11.2，不能混用参考平台的 Qt 6.8.4 DLL。快捷键：`Ctrl+O` 打开点云、`Ctrl+P` 重建参数、`Ctrl+Shift+E` 导出 STEP、`F` 适合视图。

窗口生命周期、主题资源和后续功能接入约定见[桌面工作台开发说明](ReverseModeling/README.md#桌面工作台)。
