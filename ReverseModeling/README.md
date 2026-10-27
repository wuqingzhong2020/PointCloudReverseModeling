# ReverseModeling 工程

C++20 / CMake 独立应用。唯一产品入口为 Qt Widgets + OCCT 桌面程序 `ReverseModeler`，始终包含 GUI，建模操作由程序内部的异步命令服务执行。第三方库由 [LibPackByVcpkgWindows](../../../../LibPackByVcpkgWindows/README.md) 维护，其 [vcpkg.json](../../../../LibPackByVcpkgWindows/vcpkg.json) 是依赖来源；本工程直接消费预编译 SDK，不要求开发者下载或配置 vcpkg，也不安装或重编第三方库。

## 已实现的首版范围

- 读取已有 PCD 文件，支持 ASCII、binary、binary_compressed；必须指定原始单位 mm/cm/m，内部统一 mm。保留有效 XYZ、intensity、源点索引、字段名称及无效点计数，原文件只读。其他字段仍保留在原文件中，当前没有全部映射进内存数据契约。
- 计算 PCA 方向与包围尺寸初值。平面或退化点云仍可导入查看，无法提供梁框架时返回提示。
- 导入后填入可编辑的参考参数，点击“一键构建”生成初始 H 型钢/肋板模型，无需勾选确认。每个肋板位置在腹板两侧各生成一个矩形板件，两个位置对应三块主板加四块肋板，共七个实体。
- OCCT 实体有效性检查、XCAF 命名与装配、STEP 导出及独立回读；检查单位、实体数、体积、包围范围和构件名称。目标文件已存在时拒绝覆盖。
- 对该辅助模型的有限矩形板件表面计算逐点距离及均值、RMS、P95、最大值。该算法包含板件端面与边界；它是点到各独立板件表面的距离，不是布尔并集外表面或任意 CAD 模型的通用距离。
- 异步任务查询、阶段进度、协作取消、预期版本检查及失败时保留原模型；桌面工作台直接调用内部服务。

这是**辅助参数化建模首版**。PCA 包围尺寸受离群点和遮挡影响，不是最终尺寸拟合；参考板厚、肋板数量/位置和截面方向用于生成初始模型，最终结果仍需结合点云与实物核对。尚未实现方案中的 H 型钢自动分区、任意轮廓/孔洞恢复、CCCoreLib 通用偏差、工程保存/恢复、撤销重做、Python 绑定、Web 服务及精确选择/测量。单向点到模型偏差不能代替尺寸与覆盖率验收。软件没有扫描和设备采集功能。

参考策略由 C++ 服务统一提供，Qt 与未来 Web 可复用：通常使用腹板 **8 mm**、翼板 **12 mm**、肋板 **8 mm**，肋板站位为梁长的 **1/3、2/3**。截面初始方向按高度不小于宽度选择；小尺寸数据按同一比例缩小板厚，避免几何冲突。参考参数按 4 位小数记录；过小或超范围尺寸不生成默认值，导入查看仍可使用。这些值是软件的初始参考预设，不是标准型钢规格或点云测量结果。

## 工程组织与界面边界

```text
ReverseModeling/
├── CMakeLists.txt
├── CMakePresets.json
├── cmake/                     # 预编译 SDK、依赖查找及运行环境
├── include/reverse/           # 中立数据契约与服务 API
├── src/geometry/              # PCL 导入、Eigen 初值、OCCT 建模/交换与偏差
├── src/commands/              # 异步调度、事务提交、JSON 摘要
├── src/app/                   # ReverseModeler 入口
├── src/workbench/             # Qt Widgets 与 AIS 显示适配
├── examples/                  # 参数数据契约示例
└── tests/                     # 当前工程的定向服务与几何检查
```

`ReverseModelCore` 为公共数据契约，`ReverseGeometry` 隐藏 PCL/OCCT 类型，`ReverseCommands` 维护工程与任务状态。`ReverseGeometry`、`ReverseCommands` 和 `ReverseWorkbench` 都是链接进桌面程序的内部静态库；`ReverseCommands` 不单独运行。`ReverseWorkbench` 只负责桌面交互和显示；后台线程不操作 QWidget/AIS 对象。当前视图使用 AIS_PointCloud 与 AIS_Triangulation 显示中立点/网格数据，尚未实现 CAD 子形状选择。

未来浏览器通过通信适配调用同一服务。JSON 使用 `schemaVersion: 1`，消息携带任务 ID、资产/模型 ID 和版本；点云/网格大数组不放入普通命令响应。现在的状态保存在内存，ID 在本次服务生命周期内有效；服务持久化、断线恢复与请求幂等属于 Web 阶段的待实现项。当前同一服务只运行一个几何任务，忙时拒绝新的提交。

## Windows 配置与编译

要求 CMake 3.25+、Visual Studio 2022 的 v143 x64 C++ 工具链，以及配套 SDK。`RM_SDK_ROOT` 直接指向包含 `include`、`lib`、`share` 的 SDK 目录，不能指向它的父目录或其中的 `bin`、`lib` 子目录。建议在 VS 2022 Developer PowerShell 中运行：

```powershell
cd D:/projectWQZ/PointCloudReverseModeling/ReverseModeling
$env:RM_SDK_ROOT = 'D:/projectWQZ/LibPackByVcpkgWindows/install_vc143_cpp20/x64-windows-v143-cpp20'

# 编译 Qt 桌面程序及定向测试。
cmake --preset vc143-cpp20
cmake --build --preset windows-release --target ReverseModeler ReverseServiceTests
ctest --preset windows-release
```

`ReverseServiceTests` 是由 `BUILD_TESTING` 控制的开发验证目标，不作为产品入口安装或发布。仅构建产品时使用 `cmake --build --preset windows-release --target ReverseModeler`。

应用按当前系统编译、当前系统运行，使用匹配本机工具链和架构的预编译 SDK。`LibPackByVcpkgWindows` 是当前 SDK 的维护工程，其 `vcpkg.json` 仅用于维护端管理依赖；应用不读取该清单、不调用 vcpkg，也不要求安装 vcpkg 工具或加载其工具链脚本。应用仍需要第三方头文件、库、运行时及 CMake 导出配置；替换 SDK 时须匹配 ABI、目录布局和导出目标约定。

通过 `find_package(... CONFIG)` 直接读取 `RM_SDK_ROOT` 中的导出目标，工程不再自动拼接平台子目录。应用只链接已使用的模块；PCL 不链接 `pcl_visualization`，OCCT 按 SDK 的现有配置消费 VTK。主要直接依赖为 PCL common/io、Eigen、OCCT、已安装的 nlohmann_json，以及必需的 Qt6 Core/Gui/Widgets。Qt 开发文件缺失时配置失败。版本由 SDK 解析，不写死旧 Qt 版本或旧 ThirdPartyCompile 路径。

所有机器路径通过环境变量或 CMake cache 提供。升级 SDK 后应使用新的配置目录，避免旧缓存的包路径和 ABI 混用。当前工程只使用 CMake，不参与父工程的 qmake 转换流程。

分发给其他开发者时，复制完整的预编译 SDK，保留 `include`、`lib`、`bin`、`share`、`tools`、`debug` 及 Qt 插件/资源的相对目录结构。接收方准备 MSVC v143 x64、Windows SDK 和 CMake，将 `RM_SDK_ROOT` 指向自己机器上包含这些目录的 SDK 路径即可；不需要额外的平台子目录参数、`VCPKG_ROOT`、vcpkg 可执行程序或工具链脚本。Linux 使用相同的路径规则，指向匹配编译器的 SDK，当前未做 Linux 编译验证。

Qt Creator 选择 `Windows / Qt desktop / installed SDK`。在项目的构建环境中设置 `RM_SDK_ROOT`，保存后重新运行 CMake；如果修改的是系统/用户环境变量，需要重新启动 Qt Creator。非空的 `RM_SDK_ROOT` 环境变量始终优先，并同步更新同名 CMake 缓存，旧路径不会遮蔽新的环境设置。仅在没有设置环境变量时，才使用 CMake 缓存或 `-DRM_SDK_ROOT:PATH=...` 指定的路径。若要通过 CMake 配置单独选择 SDK，先移除构建环境中的同名变量。运行配置选择 `ReverseModeler` 启动界面。

构建临时目录统一位于仓库根的 `build/<预设名>/`，当前为 `PointCloudReverseModeling/build/vc143-cpp20/`。预设名称 `vc143-cpp20` 对应已选工具集 `v143` 与工程使用的 C++20；`${presetName}` 按该名称展开，不读取 `CMakeLists.txt` 中的编译器或标准变量，也不会在编译器检测后自动改名。升级工具集或语言标准时，应同步调整对应预设的名称和配置。

Qt Creator 已有配置如果仍使用旧的 `windows-vs2022` 预设或 `ReverseModeling/out` 路径，应执行 `Build > Reload CMake Presets`，选择 `vc143-cpp20` 对应配置，并确认构建目录已切换到新位置；必要时在构建设置中重新选择该目录。由 CMake 在新位置生成缓存和工程文件，不直接搬移旧缓存。

旧的 `windows-vcpkg` / `windows-vcpkg-release` 配置已移除。从旧配置迁移时，请选择上述 installed SDK 配置并使用新的构建目录，移除 Qt Creator 初始配置或环境中的 vcpkg `CMAKE_TOOLCHAIN_FILE`，不要复用原工具链缓存；旧的 `RM_USE_VCPKG_TOOLCHAIN`、`VCPKG_*` 构建参数也不再需要。工程会在加载工具链前提示残留设置，不会自动修改用户环境。

## 运行桌面程序

各配置的可执行程序和工程库统一生成到 `PointCloudReverseModeling/output/<编译器>-cpp<标准>/<构建模式>/`。编译器标记取实际工具链版本（MSVC 使用工具集版本，如 `vc143`；GCC 使用主版本，如 `gcc16`），C++ 标准取 `CMAKE_CXX_STANDARD`，构建模式使用小写。当前 Windows / v143 / C++20 / Release 对应 `../output/vc143-cpp20/release/`，桌面程序为 `ReverseModeler.exe`。CMake 命令中的 `-B .../PointCloudReverseModeling/build/vc143-cpp20` 指定缓存、项目文件和编译中间文件的位置，生成的 Release 程序输出到前述 `output` 目录。开发环境需设置同一 SDK 的 DLL 和 OCCT 资源路径：

```powershell
$sdkPrefix = $env:RM_SDK_ROOT
$env:PATH = "$sdkPrefix/bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$sdkPrefix/Qt6/plugins"
$env:CSF_SHMessage = "$sdkPrefix/share/opencascade/resources/SHMessage"
$env:CSF_XSMessage = "$sdkPrefix/share/opencascade/resources/XSMessage"
$env:CSF_ShadersDirectory = "$sdkPrefix/share/opencascade/resources/Shaders"

& '../output/vc143-cpp20/release/ReverseModeler.exe'
```

参数数据契约见 [examples/beam-parameters.json](examples/beam-parameters.json)。它是**虚构尺寸的格式示例**，不适用于指定 PCD 的尺寸验收；桌面工作台通过参数面板编辑参数，当前不提供该 JSON 文件的导入入口。所有建模参数为 mm，`ribStations` 是从梁起点沿长度方向的距离；位置须使整块肋板位于梁内，肋板不得相互重叠。`swapCrossSection` 交换框架的宽/高轴，提交的 width/height 也须按新方向填写。

Qt 工作台须先选输入单位。导入后已填入参考值，可直接点“一键构建”，也可先修改尺寸、厚度、肋板位置和方向。“一键构建”始终使用面板当前值，不会覆盖用户修改；只有点击“恢复参考值”才重新填入默认参数。任何参数编辑都会禁止导出旧模型，须重新构建。显示点数限额只影响界面采样，不影响建模初值和偏差使用的有效点集。关闭窗口时会请求取消并等待任务结束；OCCT 单次内核调用期间可能无法立即响应取消。

VS 调试目标与 CTest 已注入对应运行环境。当前 Release 的 Qt platform plugins 位于 `$sdkPrefix/Qt6/plugins`，Debug 对应 `$sdkPrefix/debug/Qt6/plugins`，不能混用。`cmake --install` 当前仅安装应用目标，不自动生成可独立分发的 DLL/资源包。

## 已执行的验证

2026-10-27，本机 SDK 消费使用 `find_package(OpenCASCADE CONFIG QUIET REQUIRED ...)`，避免其 VTKCompileTools 对 MSVC 发出的无关提示，同时保留必需依赖校验。在全新的系统临时目录清除 `VCPKG_*`、`CMAKE_TOOLCHAIN_FILE` 环境变量，并确保进程 PATH 中不可找到 vcpkg 后，使用 `windows-vs2022` 完成 CMake 配置与生成，原 VTK 警告未再出现；生成的 16 个 MSBuild 工程中未发现 vcpkg 可执行程序、工具链脚本或 `applocal.ps1` 调用。本轮仅验证配置与生成，未编译应用或运行测试。

2026-10-27，构建入口收敛为单一 GUI 程序后，`cmake --list-presets=all` 解析通过，仅显示桌面配置及其 Release 构建/测试预设；构建代码中的 GUI 开关、无界面预设和 CLI 目标引用已清理，`git diff --check` 通过。本次未执行编译、CTest 或 GUI 运行验证。

以下保留历史版本的验证记录，其中的命令行程序现已移除；历史构建结果不代表本次单一 GUI 构建配置的编译验收。

2026-10-27，移除外部 vcpkg 模式后，在全新的系统临时目录使用 `windows-vs2022` / Release 完成 CMake 配置与生成。验证进程清除了 `VCPKG_ROOT` 和 `CMAKE_TOOLCHAIN_FILE` 环境变量，并用 Windows 反斜杠格式的 `RM_SDK_ROOT` 环境变量恢复空缓存；Qt、PCL、OCCT 等依赖均从指定 SDK 找到。SDK 内 VTK 编译工具导出配置仍有一条 MSVC/交叉编译相关警告，未阻断生成；本轮未编译应用，也未做 SDK 异机迁移验证。

2026-10-27，曾使用外部 vcpkg 工具链完成 Windows / Qt6 / v143 / Release 桌面及命令行程序编译和定向 CTest（1/1），并验证工具链缓存恢复。这是已移除模式的历史结果，不代表当前无 vcpkg 配置的编译验收。

同日先修复 Qt Creator 的 `RM_SDK_ROOT:STRING=` 空缓存问题，随后将路径选择规则调整为非空环境变量优先并同步缓存，以解决旧的父目录缓存遮蔽新 SDK 路径的问题；未设置环境变量时仍可使用 CMake 配置值。

2026-10-26，使用指定 Windows SDK、MSVC v143 / C++20 / Release，在系统临时目录完成：

- `ReverseModeler`、`ReverseCmd` 与 `ReverseServiceTests` 编译、链接，定向 CTest 通过；Qt Base 为该 SDK 的 6.11.2。
- 合成 7 板件样例的实体数/体积、有限面已知距离、中文 PCD/STEP 路径与 STEP 独立回读。
- 参考参数的 8/12/8 mm 板厚、三分点肋板、宽高方向与小尺寸适配；构建保留用户修改的参数。
- 输入单位、intensity/源索引、无效点计数、异常 PCD 头、取消、旧版本拒绝、失败保留原状态与禁止覆盖输出。
- 指定 `reverse_modeling_result-20260922-08-17-37.pcd` 实际导入 1,428,008 个有效点，字段为 x/y/z/intensity。此项以 `--unit mm` 验证导入与初值计算，不代表已经确认实物单位或实现该样件自动建模。

Qt 工作台已完成指定 PCD 的界面导入、参考值填充和一键构建，生成 7 个板件；关闭点云显示后已确认金色模型可见。三角网格使用 `AIS_Triangulation` 支持的显示模式 0，避免全局套用实体着色模式 1 导致已构建模型不可见。首次窗口布局暴露的 OCCT 视口尺寸问题已修正：show 后延迟初始化、绘制前同步实际窗口尺寸、resize 后重绘，并处理 DPI 变化；参考了 CamAssemblyWelding 中的 WidgetOccView，未修改旧插件。尚未完成 GUI 导出操作链和跨屏 DPI 验收。未修改维护工程的 manifest、ports、SDK 文件，也未构建第三方库。总体后续路线见 [独立软件方案](../reference/逆向建模实现方案-独立软件.md)。
