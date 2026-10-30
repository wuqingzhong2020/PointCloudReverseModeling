# envBuild 构建、运行与打包方案

> 状态：已实现。本文保留已确认的命令和部署设计，并同步当前实现边界；日常操作、完整命令参数和配置示例见 [envBuild 命令使用指南](envBuild命令使用指南.md)。

## 1. 目标与适配原则

目标是：**主程序编译并完成自动部署后，可直接双击 EXE 运行，无需手工补 DLL 或设置 SDK PATH；发布包解压后同样可直接运行。**

在仓库根目录增加 `envBuild.py`，提供以下九个入口：

```text
python envBuild.py info
python envBuild.py init
python envBuild.py debug
python envBuild.py release
python envBuild.py clean
python envBuild.py compile
python envBuild.py prepareRun
python envBuild.py package
python envBuild.py packageAll
```

参考灵控平台的命令分工，保留本项目的单仓库、CMake、Qt6 和现有输出目录约定。**CMake 负责构建、运行依赖收集和安装，Qt 的 CMake 部署接口负责 Qt 库及插件，CPack 负责生成发布包；Python 只作为命令入口和流程编排。**

已确认取消独立的 `deployqt` 命令，Qt 部署并入统一运行环境部署。`prepareRun` 保留为手动补部署入口，主程序正常编译完成后自动执行同一套部署规则。脚本不接管业务功能，不下载或编译第三方 SDK。

首期面向 **Windows / VS 2022 / MSVC v143 / x64 / C++20**。现有代码包含部分 Linux 支持，但本方案不将其视为已完成跨平台部署；Linux 命令适配另行安排。

## 2. 两个工程的实际差异

| 项目 | ProjectManager 参考实现 | 本项目采用的方式 |
|---|---|---|
| 仓库组织 | 多子项目、多个 Git 仓库，另有批量 Git 操作 | 单 Git 仓库；不迁移 clone、pull、push、branchReset 等命令 |
| 构建系统 | qmake 与 CMake 两条路径；其 CMake 编译分支使用 Ninja | 仅 CMake，沿用当前 Visual Studio 多配置生成器，不新增 Ninja 要求 |
| 配置维度 | 多编译器、Qt5/Qt6、各类平台目录 | 首期一个 Windows 预设和 Qt6，支持 Debug/Release |
| `debug/release` | 启动调试器或直接运行程序 | 保留含义，**这两个命令不代表编译** |
| 产物清单 | 通过 `project.ini` 管理 Launcher、插件和演示程序 | 从本项目 CMake 目标取得产物，不引入 `project.ini` |
| 开发输出 | 带 Qt 版本的多层目录，Debug 名称存在后缀约定 | 沿用 `output/vc143-cpp20/debug或release/`；两个配置均叫 `ReverseModeler.exe` |
| 运行依赖 | `3rdPartyCopy.py` 按平台的大量库清单复制，Qt 单独部署 | CMake 按二进制依赖部署，统一补充 Qt 插件和 OCCT 资源；取消独立 deployqt |
| 打包 | ZIP、Qt IFW 安装包及平台专用逻辑 | 使用 CPack ZIP 和 CMake 安装规则；Python 不自行压缩或维护另一套复制规则 |
| `packageAll` | 可通过配置跳过编译 | 默认始终进行增量编译，再部署和打包，避免含义依赖隐藏开关 |

当前已提供 `windows-release`、`windows-debug` 构建/测试预设。`cmake --install` 安装主程序到 `bin/` 并执行完整运行环境部署。

## 3. 配置和工具来源

### 3.1 配置文件分工

| 文件 | 职责 | 是否提交 Git |
|---|---|---|
| `CMakePresets.json` | 生成器、工具集、架构、构建目录，以及 Debug/Release 构建和测试预设 | 是 |
| `cmake/common.cmake` | C++ 标准、运行库和编译产物输出规则，继续作为现有规则的来源 | 是 |
| `envBuild.example.ini` | 配置字段、注释和示例 | 是 |
| `envBuild.local.ini` | 本机 SDK 前缀、工具路径及必要的运行资源位置 | 否 |
| `build/.../envbuild/` 下的生成信息 | 实际 CMake 目标路径、依赖位置和各配置的部署依据 | 否 |

不复制参考工程的 `ZJGG_*`、Qt5、多仓库地址和插件平台配置。`init` 从示例生成本地文件，已有文件保留，不静默覆盖用户修改。

本地配置的主要字段：

```ini
[build]
configure_preset = vc143-cpp20
default_mode = release
parallel_jobs = 0

[dependencies]
; 支持多个安装前缀，每行一个；由脚本转换为 CMake 的分号列表。
prefix_paths =
    D:/projectWQZ/LibPackByVcpkgWindows/install_vc143_cpp20/x64-windows-v143-cpp20

[tools]
; 留空表示自动查找；工具路径只在需要它的命令中检查。
cmake =
cdb =

[runtime]
; 默认从 CMake 已选中的包推导；分散安装的 SDK 可填写补充目录。
release_dll_dirs =
debug_dll_dirs =
occt_resource_dir =
```

上面的 SDK 路径仅为当前机器示例，不写死在 Python 逻辑中。`parallel_jobs = 0` 表示使用构建工具默认并行策略。`cpack` 从所选 CMake 安装定位，`windeployqt` 由所选 Qt 的 CMake 部署接口定位，不在 Python 配置中另选一套 Qt 工具。

### 3.2 避免多处配置互相覆盖

1. 同类设置按“显式命令行参数 → 本地配置 → 工程预设/工具自动发现”选择；缺失值与无效值分开处理。显式指定的工具无效时直接报错，不悄悄换一个版本。
2. SDK 前缀经 `-DCMAKE_PREFIX_PATH=...` 传入 CMake；具体库版本、Qt 目录和实际输出位置以 **CMake 配置后的结果** 为准，Python 不再维护一套库版本表。
3. 已将预设内的本机 SDK 绝对路径迁到本地配置，预设保留通用构建规则。Qt Creator 继续通过其 CMake 设置提供 `CMAKE_PREFIX_PATH`，无需依赖 Python 才能构建。
4. 发现已有 CMake 缓存与本次工具链、架构或 SDK 来源不一致时停止，提示使用显式 `--fresh` 或另一构建目录；不自动删除缓存。
5. `info` 展示“请求配置”和“已有缓存实际配置”的差异，便于发现命令行与 Qt Creator 使用了不同 SDK。

### 3.3 Python 和外部工具

- 按当前 [Python 脚本编码规范](Python脚本编码规范.md)，以目标 SDK 的 Python 3.15t 为基线；脚本仅使用标准库和仓库内自有模块，不执行 `pip install`。
- `envBuild.py` 是外部开发工具，不是产品内的 Python 功能；不会因此向 `ReverseModeler` 链接或打包 CPython。
- 允许通过 `subprocess` 的 `cwd` 和独立 `env` 为编译、部署、运行子进程准备环境，不修改系统环境变量，不污染调用者会话。
- 保留 CMake 3.25+ 要求；Visual Studio 生成器负责发现 MSVC。若依赖分析需要 `dumpbin` 等工具，从本次 CMake 选择的工具链取得，不能仅因普通 PowerShell 的 PATH 中没有 `cl` 就认定未安装 VS。
- CDB 只对 `debug` 必需；缺少 CDB 不阻止 Release 编译和打包。主程序自动部署需要匹配的 Qt 部署工具，缺少时应使部署失败，而不是将不能独立运行的产物报告为已就绪。

## 4. 命令约定

未指定模式时采用本地配置的 `default_mode`，初始值为 `release`；`debug`、`release` 启动命令本身固定配置，不受该默认值影响。

`compile/prepareRun/package/packageAll` 支持 `release`、`debug`、`release_debug`；最后一种明确表示分别处理两个配置。`packageAll` 中的 All 表示完整流程，**不是默认同时打两个配置的包**。

### 4.1 命令行为总表

| 命令 | 不带额外参数的行为 | 主要参数与边界 |
|---|---|---|
| `info` | 只读显示配置、工具、依赖、产物和部署状态 | 不创建配置、不执行 CMake 配置、不修复环境 |
| `init` | 创建缺失的本地配置，并检查已有环境 | 已有配置不覆盖；不下载 SDK、不编译、不启动程序 |
| `debug` | 用 CDB 启动 Debug 的 `ReverseModeler.exe` | 缺少程序或 CDB 时给出具体补齐命令；不隐式编译 |
| `release` | 直接启动 Release 的 `ReverseModeler.exe` | 不隐式编译或复制 DLL |
| `clean` | 等价于 `clean out`，清理默认配置的编译产物 | 清理类别见第 4.4 节；不默认删除全部 output |
| `compile` | CMake 配置并增量编译，主程序随后自动部署完整运行环境 | `--target`、`--jobs`、`--rebuild`、`--fresh`；不自动运行测试 |
| `prepareRun` | 调用 CMake 部署脚本，补齐已有主程序的 Qt、其他 DLL、插件及资源 | 不编译；不再需要 `--with-qt` |
| `package` | 调用 CPack，按 CMake 安装规则收集已有产物及运行依赖并生成 ZIP | 不编译；不要求先对开发输出执行 prepareRun；默认只打主程序 |
| `packageAll` | 编译主程序并自动部署 → CPack 生成 ZIP | 默认增量编译；`--rebuild` 明确请求重编译 |

入口和全部子命令均支持 `-h`、`--help`，两种写法等价。未传命令时显示总帮助，命令或参数输错时显示错误及相应帮助，具体规则见下节。

有副作用的命令支持 `--dry-run`，只输出拟执行的命令、路径及阶段，不创建或删除文件、不启动程序。无法在未构建时确定的 DLL 清单应显示“待构建后解析”，不伪造完整列表。

### 4.1.1 帮助入口、空命令和输入错误

| 输入情形 | 示例 | 输出与行为 | 退出码 |
|---|---|---|---|
| 未传命令 | `python envBuild.py` | 显示总帮助：全部命令、作用、常用语法和示例；不执行默认任务 | `0` |
| 主动查看总帮助 | `python envBuild.py -h` 或 `python envBuild.py --help` | 与未传命令时一致 | `0` |
| 查看某个命令帮助 | `python envBuild.py compile -h` 或 `python envBuild.py compile --help` | 显示该命令作用、参数、取值、默认行为和使用示例 | `0` |
| 命令名拼错 | `python envBuild.py complie` | 指出未知命令，给出可能的正确命令（如 `compile`），随后显示总帮助 | `2` |
| 模式/参数值错误 | `python envBuild.py compile releas` | 指出错误值及合法取值，显示 `compile` 的详细帮助 | `2` |
| 未知选项 | `python envBuild.py compile --rebuid` | 指出未知选项，可提示 `--rebuild`，显示 `compile` 的详细帮助 | `2` |
| 参数缺少值 | `python envBuild.py compile --target` | 指出 `--target` 缺少目标名，显示该命令详细帮助 | `2` |
| 参数组合不支持 | `python envBuild.py clean build debug` | 说明共享构建目录不能按单配置删除，显示 `clean` 的详细帮助 | `2` |

这里的“空命令”指未传子命令；`compile`、`clean` 等有效命令省略可选参数时，仍按各自默认行为执行，不转为显示帮助。

总帮助采用“完整命令语法 + 可选参数及取值 + 作用”形式，仅列出本项目支持的命令，由 `pythonProject/cli_help.py` 根据 argparse 参数定义生成。窄终端折行显示但不省略参数；清理共享目录与按模式清理分两种语法展示。完整速查见 [命令使用指南](envBuild命令使用指南.md#2-命令速查与帮助)，下列仅保留语义摘要：

```text
ReverseModeler 构建、运行与打包工具
用法：python envBuild.py [-h | --help]
      python envBuild.py <命令> [参数]

命令：
  info                         显示环境、配置、工具和产物状态（只读）
  init                         生成缺失的本地配置并检查环境
  debug                        用 CDB 调试 Debug 主程序，不编译
  release                      运行 Release 主程序，不编译
  clean [类别] [模式]          清理指定产物，默认只清理默认配置的编译产物
  compile [模式] [选项]        编译 CMake 工程，并自动部署主程序运行环境
  prepareRun [模式]            补齐已有主程序的 DLL、Qt 插件和资源，不编译
  package [模式]               用 CPack 收集已有产物及运行依赖并生成 ZIP，不编译
  packageAll [模式] [选项]     编译、自动部署并调用 CPack 生成 ZIP

模式：release、debug、release_debug（分别处理两个配置）。
默认模式读取本地配置，初始为 release；debug/release 启动命令固定配置。
clean 类别：out、3rd、out_3rd、build、package、all。
build/package/all 类别不接受模式；详细清理范围见 clean --help。

查看命令详情：python envBuild.py <命令> -h
预览操作：    python envBuild.py <命令> [参数] --dry-run（info 除外）

示例：
  python envBuild.py init
  python envBuild.py compile debug
  python envBuild.py prepareRun release
  python envBuild.py packageAll release
```

单命令帮助不能只有参数名，需说明实际效果。例如 `compile --help`：

```text
作用：使用 CMake 配置并编译工程，自动部署主程序运行环境。
      默认编译主程序和单元测试目标，不运行测试。
用法：python envBuild.py compile [模式] [选项]

模式：
  release / debug / release_debug
  省略时读取本地配置的 default_mode，初始为 release。

选项：
  --target <目标>   只编译指定目标，例如 ReverseModeler、ReverseServiceTests
  --jobs <数量>     指定构建并行数，须为正整数
  --rebuild         清理所选配置的编译产物后重新编译
  --fresh           重建 CMake 配置缓存；共享构建目录的两配置均受影响
  --dry-run         只显示计划执行的命令，不配置、不编译、不部署、不清理
  -h, --help        显示本帮助并退出

示例：
  python envBuild.py compile
  python envBuild.py compile release --target ReverseModeler
  python envBuild.py compile debug --jobs 8
```

其他命令采用相同格式，其中 `clean --help` 须解释各类别删除与保留的内容，`package/packageAll --help` 须说明是否编译和包输出位置，`debug/release --help` 须说明启动配置和前置条件。

实现约定：

- 使用标准库 `argparse` 的主解析器和子解析器统一定义命令及参数，帮助和校验共用定义，避免维护两套参数清单。
- 参数解析、帮助和用法错误处理先于配置加载及工具检查；即使尚未 init、本地配置损坏、SDK/CDB 缺失，也能查看帮助和得到输入错误提示。
- 关闭长选项的自动缩写；拼写建议只作提示，不自动更正后执行，尤其不能把错误输入变成清理或打包操作。
- 正常帮助输出到标准输出；用法错误及随附帮助输出到标准错误，不输出 Python 异常堆栈。依赖缺失、文件占用等执行错误沿用第 8 节规则，不伪装成参数错误。
- 保留 `prepareRun`、`packageAll` 的既定写法；大小写不匹配时提示正确名称，不静默执行其他命令。
- 帮助文字采用中文说明、可复制的命令示例，并按终端宽度换行，避免截图中长行难以阅读的问题。

### 4.2 编译

已增加 `windows-debug` 构建和测试预设，保留 `windows-release`。两个配置共用 `build/vc143-cpp20/`，不用 `CMAKE_BUILD_TYPE` 切换 Visual Studio 的 Debug/Release。

```powershell
python envBuild.py compile
python envBuild.py compile debug
python envBuild.py compile release --target ReverseModeler
python envBuild.py compile debug --target ReverseServiceTests
python envBuild.py compile release_debug
python envBuild.py compile release --rebuild
python envBuild.py compile release --fresh
```

- 默认不指定 target，保持当前 `cmake --build` 的行为：编译主程序和单元测试目标，**不自动运行测试**。
- 主程序构建成功后，由 CMake 自动部署 Qt、其他 DLL、插件和资源；该步骤失败时整个命令返回失败，不能只凭链接成功就报告“可运行”。
- CMake 挂接编译后部署步骤，并提供统一的 `prepare_runtime` 目标，用于主程序未重新链接、但运行文件缺失或规则更新时补部署。Python 和 Qt Creator 都调用同一套规则；具体触发与增量判断见第 5 节。
- `--target ReverseServiceTests` 只处理单元测试目标及其必要运行库，不启动或顺带构建 GUI；其余包含主程序的构建入口均执行主程序运行部署。
- `packageAll` 显式选择 `ReverseModeler`，不会为打包顺带编译测试程序。当前工程配置仍要求 GTest 已安装；本方案不额外引入关闭测试子项目的构建选项。
- `--rebuild` 使用所选配置的清理后编译，不清空 SDK 或已生成的发布 ZIP。
- `--fresh` 重新配置 CMake 缓存；该构建目录由两个配置共享，输出需说明影响范围。它不等于删除所有编译产物。
- 构建信息由 CMake 生成，至少包含目标文件、项目版本、编译器、架构、配置、Qt/OCCT 路径；脚本补充本次构建的 Git 提交和是否有未提交改动。

### 4.3 调试与运行

```powershell
python envBuild.py debug
python envBuild.py release
python envBuild.py release --dry-run
```

- 参考灵控实现：`debug` 开启可交互 CDB 控制台，符号路径使用本配置产物目录，源码路径使用本仓库；`release` 直接启动 GUI。
- 启动前检查主程序和运行依赖是否就绪，缺少程序时提示 `compile`，缺少运行文件时提示相应配置的 `prepareRun`。
- `cwd` 使用主程序目录。运行所需 DLL、Qt 插件和 OCCT 资源应已部署到产物目录并相对定位；启动器不通过额外注入 SDK PATH 掩盖部署遗漏，避免命令行能运行而双击失败。
- 首期只启动本项目主程序，不迁移参考工程“任意插件/演示程序启动”的参数体系。
- 成功时报告 PID，表示启动请求已提交，不表示程序之后不会发生运行错误；不自动修改调试器的 C++ 异常过滤策略。

### 4.4 清理

| 用法 | 清理范围 | 保留内容 |
|---|---|---|
| `clean` / `clean out release` | 通过 CMake 清理所选配置的编译产物 | 已部署第三方依赖、另一配置、配置文件、发布 ZIP |
| `clean out debug` | Debug 编译产物 | Release 编译产物及运行依赖 |
| `clean 3rd release` | 部署清单登记的第三方 DLL、Qt 插件、资源和附带材料 | 本项目编译产物、用户放入的其他文件 |
| `clean out_3rd release` | 所选配置的上述两类产物 | 另一配置、发布 ZIP |
| `clean build` | 当前预设的整个 CMake 构建目录，含两配置的中间文件和缓存 | output 下已生成文件和发布 ZIP；再次编译前需重新配置 |
| `clean package` | 脚本管理的打包暂存目录 | 已完成的发布 ZIP |
| `clean all` | 两配置编译产物、已登记部署文件、当前构建目录及打包暂存目录 | 源码、本地配置、用户数据、SDK 和已完成的发布 ZIP |

`out/3rd/out_3rd` 可使用 `release_debug`；`build/package/all` 不接受单配置参数，避免把共享目录误称为只清理 Debug。

执行规则：

- 根据 CMake 产物信息或脚本部署清单清理，不使用 `git clean`，不因目录中存在 `.exe/.dll/.pcd/.step` 就批量删除。
- 删除前解析实际绝对路径，确保目标位于本仓库指定的 build/output 范围；遇到越界路径、目录联接或符号链接时拒绝递归越界。
- 缺少清单时不猜测第三方文件归属；提示重新部署生成清单或手工处理旧文件。
- 默认不弹确认窗口；`--dry-run` 可用于预览。文件被程序、IDE 或调试器占用时报告具体文件并失败，不强杀进程、不自动改名旧 EXE。

## 5. 运行依赖部署

### 5.1 CMake 可以直接负责的工作

Python 不实现依赖扫描、运行文件复制和 ZIP 压缩逻辑。下表保留 CMake 能力调研；当前实现使用 `install(TARGETS)`、`install(CODE)` 和公共脚本内的 `file(GET_RUNTIME_DEPENDENCIES)`，避免构建部署与安装重复维护依赖收集规则。其余安装接口作为扩展能力：

| CMake 能力 | 本项目用途 |
|---|---|
| `install(TARGETS ...)` | 安装明确指定的主程序及本项目库；主程序放入 `bin/` |
| `install(TARGETS ... RUNTIME_DEPENDENCIES ...)` | 安装目标时一并递归收集其动态运行依赖 |
| `RUNTIME_DEPENDENCY_SET` + `install(RUNTIME_DEPENDENCY_SET ...)` | 将多个目标的依赖统一收集，再按相同搜索目录、过滤及配置规则安装 |
| `install(IMPORTED_RUNTIME_ARTIFACTS ...)` | 安装明确指定的第三方导入目标的运行文件，可用于补充动态加载模块 |
| `install(FILES ...)`、`install(DIRECTORY ...)` | 安装配置文件、许可材料、OCCT 资源目录等指定内容 |
| `install(SCRIPT ...)` | 在安装阶段执行 Qt 部署和项目资源检查等脚本 |
| `COMPONENT`、`CONFIGURATIONS` | 区分应用、运行文件和调试符号，选择 Release/Debug 内容 |

这些安装能力可在项目现有的 CMake 3.25 基线上使用；运行依赖安装功能自 3.21 提供。本机工具为 CMake 4.2.3，但实现不因此引入高于工程最低版本的必要语法。安装目标使用相对 `DESTINATION`，便于改变安装目录并交给 CPack。[CMake install 文档](https://cmake.org/cmake/help/v3.25/command/install.html)

`file(GET_RUNTIME_DEPENDENCIES)` 用作部署脚本内的底层扫描，能够递归解析 EXE/DLL，并报告缺失或同名冲突；不在 CMake 配置阶段扫描尚未生成的程序。[运行依赖扫描文档](https://cmake.org/cmake/help/v3.25/command/file.html#get-runtime-dependencies)

### 5.2 统一运行环境规则

| 内容 | 处理方式 |
|---|---|
| PCL、VTK、OCCT、OpenSSL、SARibbon、ADS 及其传递依赖 | 按实际二进制依赖递归收集，从本次配置的 SDK 定位并复制 |
| Qt DLL 和平台、图像等插件 | 使用 Qt 的 CMake 部署接口；Windows 下由匹配的 windeployqt 执行具体部署 |
| MSVC 运行库 | 通过 `InstallRequiredSystemLibraries` 等工具链规则取得匹配配置的运行文件，补齐应用运行所需内容 |
| OCCT Shaders 等资源 | 通过明确的文件/目录安装规则复制，并相对 EXE 定位 |
| Qt 配置及其他配置文件 | 部署 `qt.conf` 等必要文件，使插件路径不依赖开发机 SDK |
| 许可材料 | 保留所选 SDK 的 `share/*/copyright` 许可集合到 `bin/licenses/`（开发目录为 `licenses/`）；是许可集合，不会复制无关 SDK 二进制 |

当前通过 `qt6_generate_deploy_script()` 生成脚本，统一调用 `qt_deploy_runtime_dependencies()` 并明确暂存目录，开发部署与安装复用。扫描和 Qt 部署均使用独立暂存的 EXE，避免从开发输出混入遗留 DLL。Windows 下这部分主要负责 Qt，自身不能替代 PCL、OCCT 等通用依赖收集。[Qt 应用部署脚本](https://doc.qt.io/qt-6/qt-generate-deploy-app-script.html)、[Qt 6.11 运行部署接口](https://doc.qt.io/qt-6.11/qt-deploy-runtime-dependencies.html)

编译器运行库可由 CMake 的 `InstallRequiredSystemLibraries` 模块发现，再按配置安装。系统自带 DLL 和 API-set 不进行无差别复制；MSVC 运行库不能仅因扫描时解析到系统目录就忽略，应单独从匹配工具链的运行库目录准备。[编译器运行库部署文档](https://cmake.org/cmake/help/v3.25/module/InstallRequiredSystemLibraries.html)

统一约束：

- 搜索目录、架构和 Debug/Release 来自本次 CMake 配置，不扫描全电脑寻找同名 DLL，不借用另一配置目录中的文件。
- Qt 库及插件由 Qt 部署规则管理，普通依赖收集避免重复覆盖；对已部署插件继续核对其第三方依赖，补齐动态插件带来的依赖链。
- 缺少必要 DLL、插件或资源，或发现同名 DLL 来源冲突时返回失败，不将产物标记为可运行。
- 记录本次实际部署文件及摘要，清理时只处理已登记的旧文件；普通用户文件不属于部署范围。
- OCCT 资源放到主程序旁的 `resources/opencascade/`。主程序已补充启动初始化，使直接双击时也能找到资源。
- 开发输出和发布安装共用 CMake 部署模块与资源规则，只传入不同目的目录；Python 不维护第二套 DLL 名单和资源复制规则。

### 5.3 自动部署与 `prepareRun` 的补充职责

正常使用流程：

```text
编译主程序 → CMake 自动部署完整运行环境 → 双击 ReverseModeler.exe
```

开发输出仍为 `output/vc143-cpp20/<mode>/`，EXE 和所需 DLL 位于该目录，Qt 插件及 OCCT 资源放入对应子目录。

`prepareRun` 是**手动补部署入口，通常不需要在正常编译后再执行**。适用情况包括：运行库被删除、资源缺失、部署规则更新但主程序没有重新编译。

```powershell
python envBuild.py prepareRun release
python envBuild.py prepareRun debug
```

该命令调用本次 CMake 生成的部署脚本，处理 Qt、其他 DLL、插件和资源，只补充或更新必要文件；不重新编译、不启动程序。主程序或 CMake 生成信息不存在时，明确提示先 compile。

实现遵循以下约束：

- 主程序编译后自动部署，并在默认构建流程中核对运行文件；不能只依赖一次 `POST_BUILD`，导致增量构建不重新链接时遗漏已删除的 DLL。
- `prepare_runtime` 作为可单独调用的补部署目标，复用相同生成脚本；它本身不依赖需要重新编译的目标，缺少 EXE 时失败，保证 prepareRun 不暗中触发编译。
- 主程序构建流程在正确的依赖顺序中调用部署，避免 EXE 尚未链接完成就复制；同一配置的部署避免并发写入。
- 部署完整且产物、配置和规则未变化时跳过重复复制；缺失文件或摘要变化时重新核对依赖。
- 取消独立 `deployqt` 及 `--with-qt` 选项。用户误输入 `deployqt` 时提示“Qt 已并入 prepareRun，编译后也会自动部署”，显示帮助并退出，不自动执行其他命令。

## 6. 打包

### 6.1 `package` 的输入、目录和输出

`package` 调用 **CPack**：使用已有编译产物，在独立暂存目录执行 CMake 安装与部署规则，再生成 ZIP。它不编译，也不要求开发输出目录已先执行 prepareRun；所需 SDK 运行文件仍必须可访问。CPack 的二进制包内容由安装规则确定，不直接压缩整个 output 目录。[CPack 文档](https://cmake.org/cmake/help/v3.25/module/CPack.html)

与最初草案相比，已取消“Python 根据开发输出清单自行收集并压缩 ZIP”的设计；开发运行部署与发布安装由同一套 CMake 规则提供。

目录约定：

```text
output/
├── vc143-cpp20/
│   ├── debug/                       # 开发产物、Debug 依赖及部署清单
│   └── release/                     # 开发产物、Release 依赖及部署清单
├── package-staging/
│   └── vc143-cpp20/<mode>/<本次ID>/  # 每次独立的 CPack 工作目录
│       └── _CPack_Packages/.../     # CPack 管理的安装树，内部层级由其生成器决定
└── packages/
    └── ReverseModeler-<版本>-win-x64-vc143-cpp20-<模式>-<提交>-<时间>.zip
```

ZIP 解压后的产品布局：

```text
ReverseModeler/
├── bin/
│   ├── ReverseModeler.exe
│   ├── *.dll
│   ├── platforms/                 # 以及实际需要的其他 Qt 插件子目录
│   ├── qt.conf
│   ├── resources/opencascade/
│   └── licenses/
└── manifest.json
```

- 使用 `install(TARGETS ...)` 安装主程序，安装阶段收集运行依赖、执行 Qt 部署并复制资源。保留 `bin/` 布局；开发目录和安装目录中的 EXE 层级不同，由公共模块接收目的目录，不通过硬编码路径拼接适配。
- `include(CPack)` 生成 `CPackConfig.cmake`，首期选择 ZIP 生成器；Python 调用匹配 CMake 安装中的 `cpack`，不再使用 `zipfile` 或自有压缩流程。[CPack ZIP 生成器](https://cmake.org/cmake/help/v3.25/cpack_gen/archive.html)
- 产品包排除 `ReverseServiceTests.exe`、测试数据、旧版 EXE、临时修复程序、日志、PCD/STEP/CWRM 用户文件、本地配置、SDK 头文件和静态/导入库。
- Release 默认不带 PDB；Debug 包保留本项目调试符号，用于内部开发，并明确依赖匹配的调试运行环境，不按 Release 发布包交付。
- 版本取自根 CMake 项目版本；Git 信息记录最近一次 CMake 配置时的提交与未提交状态，有未提交改动加 `dirty` 标记，不要求自动提交或切换分支。
- `manifest.json` 记录产品版本、构建配置、Git 信息、EXE 摘要及运行文件相对路径和 SHA256；不包含开发机完整 PATH、本地配置原文或 SDK 绝对路径。
- CPack 内部包名固定为短名称 `ReverseModeler`，避免长暂存路径；Python 归档时改为含配置、提交、UTC 时间和随机后缀的完整文件名，并校验、更新 SHA256 文件中的文件名。CPack 在本次工作目录完成安装及 ZIP 生成，成功后 Python 只将最终包及必要的校验文件归档到 `output/packages/`，不覆盖上一个成功的 ZIP。
- 缺少 Git 信息时记录 `unknown`，不阻止源码压缩包形式交付的工程构建；不得写成另一个提交的产物。

### 6.2 CMake 原生入口与 `packageAll`

已接入以下 CMake 原生调用方式：

```powershell
# 安装到明确目录，准备完整的可运行程序树（不重新编译）
cmake --install build/vc143-cpp20 --config Release --prefix output/install/release

# 按安装规则生成 ZIP；配置必须已经编译
cpack --config build/vc143-cpp20/CPackConfig.cmake -C Release -G ZIP -B output/package-staging/manual-release
```

Visual Studio 是多配置生成器，打包必须明确 `-C Release` 或 `-C Debug`。`cpack` 不负责提前编译；启用 CPack 后 VS 还会提供大写 `PACKAGE` 构建目标，但 Python 的 package 使用直接 cpack 调用，以保持“不编译”的明确语义。[CPack 命令说明](https://cmake.org/cmake/help/v3.25/manual/cpack.1.html)、[CPack 构建目标](https://cmake.org/cmake/help/v3.25/module/CPack.html#targets-package-and-package-source)

`packageAll` 流程：

```text
检查配置和工具
    ↓
CMake 配置并编译 ReverseModeler，自动完成开发运行部署
    ↓
CPack：独立暂存目录 → 执行安装/部署规则 → 核对运行依赖 → ZIP
    ↓
将成功的发布包归档到 output/packages/
```

任一步失败立即停止并返回非零，不继续产生“看似成功”的发布包。默认不调用全量 `clean`；开发输出和发布安装目录不同，复用规则而不直接复制开发目录中的所有文件。

```powershell
python envBuild.py package
python envBuild.py package debug
python envBuild.py packageAll
python envBuild.py packageAll release --rebuild
python envBuild.py packageAll release_debug
```

本机 SDK 的 windeployqt 可能报告未提供翻译目录或可选 Direct3D 12 编译器；当前界面采用内置中文和 OCCT OpenGL，不依赖这些可选功能。必要 DLL、Qt Windows 插件或 OCCT Shaders 缺失仍会使部署失败。

首期只提供 ZIP，暂不增加 `package exe`、安装向导、代码签名、在线升级和自动上传。

## 7. 实现文件划分

参考灵控的入口和模块分离方式，但仅保留本项目需要的职责：

| 实现文件 | 职责 |
|---|---|
| `envBuild.py` | 主/子命令参数解析、`-h/--help`、空命令和输入错误提示、命令分发、统一退出码，不在导入时执行命令 |
| `pythonProject/__init__.py` | 明确仓库内 Python 包，不修改 sys.path 导入其他工程脚本 |
| `pythonProject/cli_help.py` | 根据参数定义生成中文总帮助和子命令帮助，展示完整取值、示例及独立指南入口 |
| `pythonProject/environment.py` | 配置读写、路径与工具发现、info/init、子进程执行公共规则 |
| `pythonProject/build.py` | 调用 CMake 配置/编译及运行就绪检查、构建产物清理 |
| `pythonProject/program_launcher.py` | Debug/CDB 和 Release 启动 |
| `pythonProject/deploy.py` | prepareRun 的薄封装，调用 CMake 生成脚本；读取部署清单执行受控清理，不扫描或复制 DLL |
| `pythonProject/package.py` | 调用 CPack、选择配置/工作目录、归档成功包及 packageAll 编排，不自行压缩 |
| `envBuild.example.ini`、`.gitignore` | 配置示例及本机配置、Python 缓存的忽略规则 |
| `CMakePresets.json` | 增加 Debug 预设，迁出本机 SDK 路径 |
| `cmake/` 中的构建信息与部署规则文件 | 输出目标信息；统一运行依赖、Qt 部署、资源及清单；挂接自动部署与手动补部署入口 |
| `cmake/` 中的安装与 CPack 配置文件 | 主程序和指定文件的 install 规则、配置筛选、版本、manifest、CPack ZIP 设置；不重复 common.cmake 的目录规则 |
| `ReverseModeling/main.cpp` 或独立启动辅助文件 | 随包 OCCT 资源的相对定位，具体拆分遵循现有 C++ 文件清单规范 |
| `tests/python/` | Python 标准库 unittest 单元测试 |
| 根 README、Python 脚本编码规范 | 实施后补充命令入口和外部开发工具的子进程环境约定 |

尽量不改业务算法和界面。Python 文件使用 UTF-8 无 BOM、类型标注和明确入口，遵循现有脚本规范。

## 8. 错误输出与必要验证

### 8.1 输出约定

- 统一显示当前阶段、配置、目标路径和失败原因，保留 CMake/MSBuild/Qt 部署工具/CPack 的关键原始错误。
- 返回码：`0` 表示命令完成或正常显示帮助（含未传命令），`1` 表示配置检查或执行失败，`2` 表示参数用法错误并已显示错误及帮助。启动命令的 `0` 只表示进程已创建；`info` 成功完成只读报告可返回 `0`，报告内单独标出缺失项。
- 所有子进程采用参数列表，中文路径和空格路径直接传递，不拼接未经转义的 shell 命令字符串。
- 文件占用、版本冲突、缺少依赖都应明确指出对象；不自动结束用户程序、修改系统 PATH 或调用其他仓库的初始化脚本。

### 8.2 验证方式

已补充 **unittest 单元测试**，主要覆盖配置优先级、命令参数、中文路径、配置隔离、部署清单读取、清理边界，以及 CMake/CPack 调用和失败中止。外部工具和文件占用使用必要的模拟，临时文件仅在测试临时目录创建；不在 Python 中重复实现部署逻辑以便测试。

命令帮助的单元测试覆盖：空命令、两种总帮助写法、全部九个命令的两种帮助写法、未知命令/选项、非法值和缺失参数，以及旧 deployqt 输入的替代用法提示；检查退出码、提示内容及输入错误时没有调用构建/清理/启动函数，并验证缺少配置或 SDK 时仍可显示帮助。

另在实际环境中定向验证 Release 自动部署、手动补部署、CMake 安装和 CPack 解压运行，确认从资源管理器直接双击开发输出及解压目录的 EXE，均不依赖 SDK PATH，不出现缺库/缺插件提示，并能显示 3D 视口、使用 PCD/STEP/CWRM 导入功能。还需验证删除一个已登记的运行文件后，增量构建或 prepareRun 能补回；补部署不触发编译。Debug 在存在匹配 SDK 和 CDB 时验证。对未完成的配置明确说明，不把单元测试通过写成发布包已验证。

不新增独立的端到端自动操作框架，不默认执行全部测试，不在测试 README 中保存验证记录或待验收清单。

## 9. 实施顺序

1. **配置与构建**：先统一总帮助、子命令帮助、空命令和输入错误行为，再增加本地配置、info/init/compile/clean，补齐 Debug 预设和 CMake 构建信息。
2. **部署与运行**：实现 CMake 统一部署、编译后自动部署及 prepareRun 补部署入口，完成依赖清单、资源相对定位和 debug/release；先使开发输出可直接双击运行。
3. **安装与打包**：补齐 install 及 CPack 规则，Python 接入 package/packageAll，配置独立暂存目录、版本与清单；核对解压后运行行为。
4. **收尾**：补充上述单元测试和 README 使用入口，移除仅为实现过程生成的临时文件。

以上阶段已落实为当前模块划分；修改时继续围绕实际变更运行相应单元测试与部署验证。

## 10. 已采用的决策表

| 决策项 | 当前约定 | 备注 |
|---|---|---|
| 首期平台 | Windows v143/x64/Qt6，沿用 Visual Studio CMake 预设 | |
| Python 基线 | 遵循现有规范，使用 SDK Python 3.15t；若希望支持普通系统 Python，先调整脚本规范 | |
| 本地配置命名 | `envBuild.local.ini`；示例文件提交，本地文件忽略 | |
| 帮助入口 | 总入口和全部命令支持 `-h/--help`，说明作用、参数、默认行为和示例 | |
| 空命令/输错命令 | 空命令显示总帮助并返回 0；输入错误显示原因与相应帮助并返回 2，不执行操作 | |
| `init` 的深度 | 仅生成配置并检查环境，不自动编译或下载依赖 | |
| `debug` 含义 | 启动 CDB 调试 Debug 程序；不表示 Debug 编译 | |
| `compile` 默认范围 | 编译当前工程目标，包含单元测试目标但不运行测试；自动完成主程序运行部署 | |
| 默认模式 | Release；需要两配置时显式写 `release_debug` | |
| `clean` 默认范围 | 当前默认配置的编译产物，依赖及发布包保留 | |
| `deployqt` 命令 | 取消独立命令，Qt 部署统一由 CMake 规则执行 | 已确认 |
| `prepareRun` 定位 | 手动补部署，覆盖 Qt、其他 DLL、插件和资源；正常编译后通常无需执行，不重新编译 | |
| 双击运行目标 | 主程序正常编译后与发布包解压后均具备完整运行文件，不靠启动器注入 SDK PATH | 已明确 |
| 安装与打包职责 | CMake install 收集安装内容，CPack 生成 ZIP；Python 仅调用并归档 | |
| `packageAll` 是否编译 | 始终先增量编译主程序，`--rebuild` 请求重编译 | |
| 首期包类型 | ZIP，解压后从 `bin/ReverseModeler.exe` 运行 | |
| Release 调试符号 | 默认不放入产品 ZIP | |

## 11. 本次方案的代码依据

设计基于本地源码、所选 SDK 的 Qt CMake 部署脚本及官方文档。工程实现及可重复的验证入口以仓库代码和根 README 为准；本文不保存逐次验证记录。

本项目：

- [根 README](../README.md)、[CMakePresets.json](../CMakePresets.json)：现有工具链、配置和启动约定。
- [common.cmake](../cmake/common.cmake)、[Dependencies.cmake](../cmake/Dependencies.cmake)：输出目录、运行库、依赖来源。
- [主程序 CMakeLists.txt](../ReverseModeling/CMakeLists.txt)、[main.cpp](../ReverseModeling/main.cpp)：主目标、安装规则和启动逻辑。
- [Python 脚本编码规范](Python脚本编码规范.md)、[C++ 编码规范](cpp编码规范.md)：脚本环境、编码与变更约定。

参考工程（与本项目同级放置时链接有效）：

- [envBuild.py](../../ProjectManager/envBuild.py)：命令分发、清理类别和 packageAll 编排。
- [common.py](../../ProjectManager/pythonProject/common.py)：本机配置及平台配置组织。
- [compile.py](../../ProjectManager/pythonProject/compile.py)：qmake/CMake 编译分支和模式解析。
- [program_launcher.py](../../ProjectManager/pythonProject/program_launcher.py)：debug/release 的实际启动语义。
- [package.py](../../ProjectManager/pythonProject/package.py)：Qt 部署、prepareRun 和 ZIP/安装包分工。
- [3rdPartyCopy.py](../../ProjectManager/project/ThirdPartyManager/common/3rdPartyCopy.py)：第三方运行文件和附带材料的复制组织。
