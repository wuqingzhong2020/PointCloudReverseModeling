# envBuild 命令使用指南

`envBuild.py` 用于配置、编译、运行、补齐运行依赖、清理和打包 ReverseModeler。**在工程根目录执行即可，不需要进入 `pythonProject`。** 本文统一说明各命令的用途、前提、参数、操作示例和输出位置。

第一次使用，按 [首次配置与运行](#3-第一次使用) 操作；日常查阅见 [命令速查](#2-命令速查与帮助) 和 [各命令示例](#5-各命令的用法和示例)；交付程序见 [产物与发布包](#6-产物在哪怎样交付)。

## 1. 从哪里运行

**工程根目录**是同时包含 `envBuild.py`、`CMakeLists.txt` 和 `CMakePresets.json` 的目录，即你克隆或解压工程的位置。工程可以放在任意盘符和目录下。

在工程根目录打开终端（PowerShell 或 Git Bash），即可执行后续命令：

```powershell
python envBuild.py --help
```

如果终端当前位于其他目录，先切换到工程根目录。**本文所有 `<...>` 都是占位符，使用前须连同尖括号替换为本机实际路径或参数值；它们不是环境变量，也不会自动展开。**

PowerShell：

```powershell
Set-Location -LiteralPath '<工程根目录>'
```

Git Bash（路径使用 `/`，Windows 盘符可写成 `/c/`、`/d/` 等形式）：

```bash
cd "<工程根目录>"
```

也可以在其他目录传入入口脚本的完整路径；工具按脚本位置定位工程：

```powershell
python "<工程根目录>/envBuild.py" info
```

本文的 `python` 指所选 SDK 的 **Python 3.15t**，只使用标准库，不需要安装 pip 包。若终端中的 `python` 未指向该解释器，可在工程根目录使用其完整路径；PowerShell 写法如下，`<Python解释器完整路径>` 应替换为实际的 `python.exe` 路径：

```powershell
& '<Python解释器完整路径>' envBuild.py info
```

本文命令面向 Windows、Visual Studio 2022 / MSVC v143、x64、C++20、CMake 3.25+ 和匹配的 Qt6 / PCL / OCCT 等 SDK。请先安装这些工具和依赖，`init` 不负责下载或安装。完整依赖清单见 [根 README](../README.md#环境与依赖)。CDB 仅启动 Debug 调试时需要。

## 2. 命令速查与帮助

下面是语法说明，`[]`、`{}`、`<>` 和 `|` 不需要原样输入。`[]` 表示可选，`{}` 表示必须选择一个，`|` 表示任选其一，`<数量>` 替换为正整数。长命令显示时可能折行，实际输入写在一行即可。

```text
python envBuild.py [-h | --help]
python envBuild.py info [-h | --help]
python envBuild.py init [--dry-run] [-h | --help]
python envBuild.py debug [--dry-run] [-h | --help]
python envBuild.py release [--dry-run] [-h | --help]
python envBuild.py clean [out | 3rd | out_3rd] [release | debug | release_debug] [--dry-run] [-h | --help]
python envBuild.py clean {build | package | all} [--dry-run] [-h | --help]
python envBuild.py compile [release | debug | release_debug] [--target {ReverseModeler | ReverseServiceTests}] [--jobs <数量>] [--rebuild] [--fresh] [--dry-run] [-h | --help]
python envBuild.py prepareRun [release | debug | release_debug] [--dry-run] [-h | --help]
python envBuild.py package [release | debug | release_debug] [--dry-run] [-h | --help]
python envBuild.py packageAll [release | debug | release_debug] [--jobs <数量>] [--rebuild] [--fresh] [--dry-run] [-h | --help]
```

| 命令 | 用途 | 是否编译 |
|---|---|---|
| `info` | 查看环境、工具和已有产物 | 否 |
| `init` | 创建缺失的本地配置，再显示环境 | 否 |
| `debug` | 用 CDB 调试已有 Debug 主程序 | 否 |
| `release` | 运行已有 Release 主程序 | 否 |
| `clean` | 按类别清理产物 | 否 |
| `compile` | 配置、编译，自动部署运行依赖 | 是，默认增量 |
| `prepareRun` | 为已有程序补齐完整运行环境 | 否 |
| `package` | 将已有主程序及依赖打成 ZIP | 否 |
| `packageAll` | 编译主程序、部署、打包 | 是，默认增量 |

这些命令区分大小写，例如 `prepareRun`、`packageAll`。模式使用小写；`release_debug` 表示分别处理 Release 和 Debug。省略模式时读取 `default_mode`，初始为 `release`。启动命令 `debug` / `release` 始终使用各自固定配置。

帮助示例：

```powershell
python envBuild.py
python envBuild.py -h
python envBuild.py --help
python envBuild.py compile -h
python envBuild.py clean --help
```

前三行显示相同的总帮助。各子命令帮助还会解释参数和给出示例；帮助不要求先配置 SDK。命令名或参数输错时，显示错误和相关帮助，不执行操作。这里的“空命令”仅指 `python envBuild.py`；`python envBuild.py compile` 是有效的编译请求。

除只读的 `info` 外，命令都支持 `--dry-run`：显示拟执行操作，不配置、不编译、不部署、不清理、不打包、不启动程序。预览仍会读取已有配置并检查必要条件；它不能保证实际编译或部署成功。

## 3. 第一次使用

在根目录执行：

```powershell
python envBuild.py init
```

然后编辑根目录生成的 `envBuild.local.ini`，将 `<SDK安装前缀>` 替换为本机依赖库的实际安装根目录。这里填写安装前缀，不是工程目录，也不是 SDK 的 `bin` 子目录：

```ini
[dependencies]
prefix_paths =
    <SDK安装前缀>
```

保存后检查环境、编译并运行：

```powershell
python envBuild.py info
python envBuild.py compile release --target ReverseModeler --jobs 8
python envBuild.py release
```

编译成功且运行部署完成后，也可直接双击 `output/vc143-cpp20/release/ReverseModeler.exe`。如需生成发布包：

```powershell
python envBuild.py packageAll release
```

后续修改源码后，重新执行 `compile release --target ReverseModeler`，再执行 `release`。正常编译已包含运行依赖部署，无需额外执行 `prepareRun` 或手工配置 SDK PATH。

## 4. 配置文件怎么填写

### 4.1 文件分工和优先级

| 文件 | 用途 |
|---|---|
| `envBuild.example.ini` | 仓库内的字段模板和默认值 |
| `envBuild.local.ini` | 本机配置，由 `init` 首次生成，已被 Git 忽略；日常修改此文件 |
| `CMakePresets.json` | CMake 生成器、工具链、架构及构建目录；一般保持现有预设 |
| `build/vc143-cpp20/CMakeCache.txt` | CMake 实际配置结果，由 CMake 管理 |

配置读取时，先读模板，再用本地文件覆盖同名字段。因此本地文件可以只保留需要修改的项。`init` 不覆盖已有本地配置，也不下载 SDK。

- **模式**：命令行显式模式 → 本地配置的 `default_mode` → 模板初始值 `release`。
- **并行数**：`--jobs` → 本地配置的 `parallel_jobs` → 初始值 `0`，即构建工具默认策略。
- **SDK 前缀**：配置中的非空 `prefix_paths` → 已有 CMake 缓存的 `CMAKE_PREFIX_PATH` → 当前进程环境变量 `CMAKE_PREFIX_PATH`。
- **CMake**：配置中的非空 `tools.cmake` → PATH → 已有缓存记录的 CMake 路径。
- **CDB**：配置中的非空 `tools.cdb` → PATH 中的 `cdb.exe` → Windows Kits 常见的 x64 调试器目录。

显式配置了一个不存在的工具路径时会报错，不会悄悄换成别的版本。CPack 默认从所选 CMake 的同目录查找，Qt 部署工具由 CMake 所选的 Qt 包定位。

### 4.2 一份通常够用的完整配置

以下内容可以作为 `envBuild.local.ini` 的起点，使用前将 `<SDK安装前缀>` 替换为本机实际路径。工程目录和 SDK 目录不必位于同一盘符或具有固定的相对位置：

```ini
[build]
configure_preset = vc143-cpp20
default_mode = release
parallel_jobs = 8

[dependencies]
prefix_paths =
    <SDK安装前缀>

[tools]
cmake =
cdb =

[runtime]
release_dll_dirs =
debug_dll_dirs =
occt_resource_dir =
```

| 配置项 | 填法与作用 |
|---|---|
| `build.configure_preset` | 根 `CMakePresets.json` 中的配置预设名，目前使用 `vc143-cpp20` |
| `build.default_mode` | `release`、`debug` 或 `release_debug`；影响支持模式但未显式指定模式的命令 |
| `build.parallel_jobs` | 非负整数；`0` 交给构建工具决定，`8` 表示最多 8 个并行构建任务 |
| `dependencies.prefix_paths` | SDK **安装前缀**，不是 `bin` 或某个 DLL 文件；多个前缀每行一个 |
| `tools.cmake` | 可留空，或填写 `cmake.exe` 的完整路径 |
| `tools.cdb` | 可留空，或填写原生 `cdb.exe` 的完整路径；不支持 `.bat/.cmd` 包装器 |
| `runtime.release_dll_dirs` | Release 部署时额外搜索的 DLL 目录，通常留空；可多个目录 |
| `runtime.debug_dll_dirs` | Debug 部署时额外搜索的 DLL 目录，通常留空；可多个目录 |
| `runtime.occt_resource_dir` | 自定义 OCCT 资源根目录，通常留空；应直接包含 `Shaders` 等子目录 |

`parallel_jobs = 0` 是配置文件的特殊值；命令行 `--jobs 0` 无效，命令行须填正整数。Release 和 Debug 的 DLL 目录应分别填写，不能用 Release DLL 替代 Debug DLL。

### 4.3 多个 SDK 前缀、自定义工具和资源

依赖分散安装或自动发现不足时，可参考下面的配置。将各占位符替换为真实存在的目录或文件；不需要的工具和运行资源覆盖项保持留空：

```ini
[dependencies]
prefix_paths =
    <基础库安装前缀>
    <Qt安装前缀>
    <OCCT安装前缀>

[tools]
cmake = <cmake.exe完整路径>
cdb = <cdb.exe完整路径>

[runtime]
release_dll_dirs =
    <Release额外DLL目录1>
    <Release额外DLL目录2>
debug_dll_dirs =
    <Debug额外DLL目录1>
    <Debug额外DLL目录2>
occt_resource_dir = <OCCT资源根目录>
```

路径书写规则：

1. INI 使用 UTF-8，建议使用 `/`；中文和空格可以直接写，**不要给 INI 值加引号**。命令行中的带空格路径则需要引号。
2. 多行路径的续行必须缩进，如示例中的四个空格；也可用分号分隔路径，但不要在路径值末尾追加注释。注释单独以 `;` 开头。
3. 不展开 `%SDK_ROOT%`、`$env:SDK_ROOT`、`${SDK_ROOT}` 等环境变量表达式；直接填写路径。
4. `prefix_paths` 和工具路径可相对工程根目录填写，与当前终端目录无关；运行依赖和资源覆盖路径建议始终使用绝对路径。
5. 通常不需要填写 `[runtime]`；它用于补充自动发现不足的路径，不是完整 DLL 复制清单。

### 4.4 修改配置后怎样生效

修改模式、并行数后，下次对应命令直接使用新值。修改 SDK 或运行资源配置后，执行 `compile` 更新 CMake 配置和部署脚本。`prepareRun`、`package` 和启动命令使用已有 CMake 生成信息，不会替你重新配置。

更换 SDK 安装前缀时，先修改 `prefix_paths`，然后：

```powershell
python envBuild.py info
python envBuild.py compile release --fresh --target ReverseModeler
```

`--fresh` 重建共享 CMake 配置缓存，Debug 和 Release 共用的配置都会更新。它与 `--rebuild` 不同：后者先清理所选模式的编译产物，再重新编译。需要两者时可以组合：

```powershell
python envBuild.py compile release_debug --fresh --rebuild --jobs 8
```

之前填写过 `[runtime]` 的覆盖项、现在又将其清空时，也要使用 `--fresh`；否则旧值可能仍保存在 CMake 缓存中。Qt Creator 直接调用 CMake 时，不读取 `envBuild.local.ini`，需要在其 CMake 设置中填写相同的 `CMAKE_PREFIX_PATH`。

## 5. 各命令的用法和示例

### 5.1 info：查看当前环境

显示工程位置、预设、构建目录、请求的 SDK、缓存中的 SDK、CMake/CPack/CDB 位置，以及两个模式的程序与部署清单状态。它不修改文件，也不修复环境。

```powershell
python envBuild.py info
```

工具缺失会逐项显示。`info` 的正常返回不表示所有工具都可用；输出中的“已生成”表示部署清单存在，启动命令才会进一步核对清单内文件。`info` 不接受模式或 `--dry-run`。

### 5.2 init：创建本机配置

首次从模板生成 `envBuild.local.ini`，优先采用已有 CMake 缓存中的 SDK 前缀，再显示环境。已有本地文件时保持原样。不会生成 CMake 工程，也不会编译或安装第三方库。

```powershell
python envBuild.py init --dry-run
python envBuild.py init
```

### 5.3 compile：编译与自动部署

先运行 CMake 配置，再编译所选模式。默认编译主程序 `ReverseModeler` 和单元测试程序 `ReverseServiceTests`，**不会运行单元测试**。编译主程序后会自动部署其 DLL、Qt 插件、OCCT 资源等；部署失败时整个命令返回失败。

| 参数 | 什么时候用 | 效果 |
|---|---|---|
| `--target ReverseModeler` | 只需要桌面主程序 | 仅编译主程序并部署其运行环境 |
| `--target ReverseServiceTests` | 只需要单元测试程序 | 仅编译测试程序并部署其运行依赖，不构建或启动 GUI |
| `--jobs 8` | 希望指定并行任务数 | 本次使用 8 个并行任务，覆盖配置文件；必须是正整数 |
| `--rebuild` | 希望清理旧编译产物后重新编译 | 清理所选模式后编译，保留 SDK 和已完成的发布 ZIP |
| `--fresh` | 更换 SDK，或需要重建 CMake 配置缓存 | 更新 Debug/Release 共用的配置缓存，不等于清理全部编译产物 |
| `--dry-run` | 先确认本次操作 | 仅预览，不配置、编译、部署或清理 |

```powershell
# 默认模式、默认目标、默认并行数
python envBuild.py compile

# 仅编译 Release 主程序，8 个并行任务
python envBuild.py compile release --target ReverseModeler --jobs 8

# 编译 Debug 主程序
python envBuild.py compile debug --target ReverseModeler

# 仅编译 Debug 单元测试程序
python envBuild.py compile debug --target ReverseServiceTests

# 两个模式分别编译
python envBuild.py compile release_debug

# 清理 Release 的旧编译产物后再编译
python envBuild.py compile release --rebuild

# 仅预览重建配置及编译操作
python envBuild.py compile release --fresh --dry-run
```

`--target` 一次选择一个目标；省略时编译默认全部目标。`--jobs`、`--rebuild`、`--fresh` 可组合。单元测试目标会部署自身所需运行依赖，具体测试运行方法见 [测试程序说明](../tests/README.md)。

即使只编译主程序，当前工程在 CMake 配置阶段仍会查找 Google Test，因此 SDK 也需要包含它。

### 5.4 release：运行 Release 程序

先确保 Release 已编译、部署完成，再启动。该命令不编译、不补 DLL，也不接受程序文件名或模式参数。

```powershell
python envBuild.py compile release --target ReverseModeler
python envBuild.py release --dry-run
python envBuild.py release
```

程序以 EXE 所在目录为工作目录启动。终端输出 PID 表示启动请求已提交，不代表程序后续运行一定正常；也可直接双击同一 EXE。

### 5.5 debug：通过 CDB 调试 Debug 程序

需已有 Debug 主程序及运行环境，并能找到原生 `cdb.exe`。配置位置是 `[tools]` 的 `cdb` 字段；只编译 Debug 并不需要 CDB。

```powershell
python envBuild.py compile debug --target ReverseModeler
python envBuild.py debug --dry-run
python envBuild.py debug
```

`debug` 不是“编译 Debug”的缩写。启动前会核对程序及部署清单；缺少运行文件时，先执行 `prepareRun debug`。

命令会打开可交互的 CDB 控制台，符号路径使用 Debug 产物目录，源码路径使用当前仓库。请保留 EXE 旁的 `ReverseModeler.pdb`，缺失时源码调试信息可能受限。

### 5.6 prepareRun：补齐已有程序的完整运行环境

正常编译已自动执行运行部署。误删 DLL、Qt 插件，或需要刷新运行依赖时使用此命令：

```powershell
python envBuild.py prepareRun release
python envBuild.py prepareRun debug
python envBuild.py prepareRun release_debug --dry-run
```

它补齐 Qt 和其他 DLL、平台插件、`qt.conf`、OCCT 资源及相关许可材料，不编译也不启动程序。需要对应模式的 EXE、CMake 生成信息和可访问的 SDK 运行文件；缺少程序或构建信息时先 `compile`。独立的 `deployqt` 和 `--with-qt` 均不再提供，Qt 已包含在完整部署流程中。

### 5.7 package：打包已有程序

通过 CPack 在独立暂存目录执行 CMake 安装和运行依赖收集，再生成 ZIP。**不会编译源码**，请确认已有 EXE 是想发布的版本；需要源码变更一并编译时使用 `packageAll`。

```powershell
python envBuild.py package release
python envBuild.py package debug
python envBuild.py package release_debug
python envBuild.py package release --dry-run
```

要求已有所选模式的主程序、CMake/CPack 生成信息和可访问的 SDK 运行文件。开发输出目录缺少 DLL 时，不要求先 `prepareRun`，安装阶段会独立收集运行依赖。这里只支持 ZIP，不接受 `zip`、`exe` 等额外格式参数。

### 5.8 packageAll：编译、部署、打包

先增量编译主程序并完成运行部署，再执行打包。`All` 表示完整流程，不表示默认同时打包 Debug 和 Release，也不表示总是全量重编译。

```powershell
# 日常发布 Release：先编译最新源码，再打包
python envBuild.py packageAll release --jobs 8

# 分别生成两个模式的包
python envBuild.py packageAll release_debug

# 清理所选模式的旧产物，再编译和打包
python envBuild.py packageAll release --rebuild

# 更换 SDK 后预览完整操作
python envBuild.py packageAll release --fresh --dry-run
```

该命令固定编译主程序，不接受 `--target`，也不编译或运行单元测试。任一阶段失败，停止后续操作。

### 5.9 clean：按类别清理

无参数时等同于 `clean out`，使用默认模式。只有 `out`、`3rd`、`out_3rd` 支持模式参数。

| 类别 | 清理内容 | 保留内容与影响 |
|---|---|---|
| `out` | CMake 管理的所选模式编译产物 | 保留已部署依赖、其他模式及发布 ZIP |
| `3rd` | 所选模式清单内已登记且未被修改的运行依赖 | 保留 EXE、未登记或已被用户修改的运行文件；再次运行前须补部署 |
| `out_3rd` | 同时执行 `out` 和 `3rd` | 保留其他模式及发布 ZIP |
| `build` | 当前预设的整个 `build/vc143-cpp20` 目录 | Debug/Release 共用此目录；保留 output，但需重新 compile 生成构建信息 |
| `package` | 整个 `output/package-staging` 暂存目录 | 保留 `output/packages` 下已完成的 ZIP 和校验文件 |
| `all` | 两模式的编译产物、已登记依赖，以及上述构建目录和打包暂存目录 | 保留源码、本地配置、外部 SDK、发布 ZIP，以及 output 内未纳入清理范围的文件 |

`build` / `package` / `all` 不接受模式。构建目录和打包暂存目录会整体删除，个人数据应放在这些目录之外。工具不会自动关闭正在运行的程序。

```powershell
# 先查看默认清理范围
python envBuild.py clean --dry-run

# 仅清理 Debug 编译产物
python envBuild.py clean out debug

# 清理 Release 已部署依赖，然后补回
python envBuild.py clean 3rd release
python envBuild.py prepareRun release

# 清理两个模式的编译产物和已登记依赖
python envBuild.py clean out_3rd release_debug

# 查看删除共享构建目录的操作
python envBuild.py clean build --dry-run

# 删除打包暂存，保留成品包
python envBuild.py clean package

# 查看全部受管产物的清理操作
python envBuild.py clean all --dry-run
```

## 6. 产物在哪，怎样交付

当前预设的常用路径：

| 路径 | 内容 |
|---|---|
| `build/vc143-cpp20/` | CMake 工程、缓存、中间文件、部署脚本和构建信息 |
| `output/vc143-cpp20/release/ReverseModeler.exe` | 开发目录中的 Release 主程序 |
| `output/vc143-cpp20/debug/ReverseModeler.exe` | 开发目录中的 Debug 主程序 |
| `output/packages/` | 发布 ZIP 和对应 `.zip.sha256` 校验文件 |
| `output/package-staging/` | CPack 临时安装和打包工作目录 |
| `output/log/` | 构建、测试及手工验证日志，以及配套的 JSON 等诊断记录 |

日志和配套诊断记录统一保存在 `output/log/`，不放在 `output/` 根目录，不提交 Git、不进入发布包；现有 `clean` 命令保留该目录。命令默认输出到终端，如需同时保存日志，可在工程根目录使用 PowerShell：

```powershell
New-Item -ItemType Directory -Path output/log -Force | Out-Null
python envBuild.py compile release 2>&1 | Tee-Object -FilePath output/log/envbuild-compile-release.log
```

该示例会覆盖同名日志；需要保留多次记录时，在文件名中增加日期或本次操作名称。

包名包含版本、平台、工具链、模式、Git 提交标识和时间等信息；最近一次 CMake 配置时工作区有未提交修改，包名会包含 `dirty`。`package` 使用最近一次 CMake 配置生成的版本信息；发布最新源码使用 `packageAll`。每次成功打包生成新的 ZIP 和 `.zip.sha256`，保留之前的成品包。

将 ZIP 完整解压，再运行其中的 **`ReverseModeler/bin/ReverseModeler.exe`**。需要保留整个解压目录，不要只复制 EXE：

```text
ReverseModeler/
├── bin/
│   ├── ReverseModeler.exe
│   ├── *.dll
│   ├── platforms/                 # Qt 平台插件，以及其他实际需要的插件目录
│   ├── qt.conf
│   ├── resources/opencascade/     # OCCT 运行资源
│   └── licenses/                  # 随包许可材料
└── manifest.json                 # 版本、构建模式、Git 信息及运行文件校验信息
```

发布包包含运行依赖，不包含测试程序、用户点云/模型、日志、本地配置或 SDK 的开发头文件、静态库与导入库。Release 用于常规交付，默认不带 PDB；Debug 包带主程序 PDB 和调试运行库，用于内部调试。

传输后如需校验 ZIP，在 PowerShell 中将下列文件名替换为实际包名，比较输出的 SHA256 与对应校验文件的第一项：

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath 'output/packages/实际包名.zip'
Get-Content -LiteralPath 'output/packages/实际包名.zip.sha256'
```

需要直接调用 CMake 安装、CPack 打包或使用 Qt Creator 时，见 [根 README 的构建与运行](../README.md#构建与运行)。

## 7. 常见问题

| 现象 | 处理方式 |
|---|---|
| `python envBuild.py` 只显示帮助 | 正常。追加实际命令，如 `compile release` |
| 命令或参数拼错 | 查看错误后的完整用法；脚本不会自动执行猜测的命令 |
| `clean debug` 报错 | 第一个位置参数是类别，应写 `clean out debug` |
| `clean build debug` 报错 | 构建目录由两模式共用，应写 `clean build` |
| `--jobs 0` 报错 | 命令行填正整数；希望用工具默认值时省略选项，并设 `parallel_jobs = 0` |
| 找不到 SDK / `find_package` 失败 | 核对 `prefix_paths` 是否为正确安装前缀、库是否齐全、架构及配置是否匹配；用 `info` 对照当前请求与缓存 |
| “缓存 SDK 与本地配置不符” | 确认新 SDK 路径后执行 `compile release --fresh` |
| `debug` 找不到 CDB | 安装 Windows 调试工具，或在 `[tools]` 中填写本机原生 `cdb.exe` 路径；编译和 Release 启动不依赖 CDB |
| 缺少构建信息或主程序 | 先执行相应模式的 `compile`；`clean build` 后即使 EXE 还在，也要重新生成构建信息 |
| 程序或 `qwindows.dll` 等文件缺失 | EXE 缺失执行 `compile`；运行依赖缺失执行 `prepareRun`。修改过 SDK 配置则先 compile 更新部署脚本 |
| 编译链接成功，但运行部署失败 | 根据终端错误检查匹配的 Qt 部署工具、DLL 和 OCCT 资源；修正后重新 `compile` 或 `prepareRun`，部署成功后再运行 |
| 配置里的路径含中文或空格 | INI 直接填写、不加引号；终端参数中的带空格路径加引号；PowerShell 调用带引号的 EXE 路径时前加 `&` |
| `LNK1104` / `LNK1168`，提示无法写入 ReverseModeler.exe | 关闭正在运行的程序或占用它的调试会话后重试；脚本不自动结束进程 |
| `package` 后发现程序还是旧版本 | `package` 不编译，请使用 `packageAll release` |
| 修改了 `[runtime]`，`prepareRun` 仍用旧路径 | 先 `compile` 更新生成脚本；清空旧覆盖值时加 `--fresh` |
| 输入 `deployqt` 或 `--with-qt` 报错 | Qt 已并入自动部署；需要手动补齐时使用 `prepareRun release` 或 `prepareRun debug` |

退出码为 `0`（成功或显示帮助）、`1`（执行失败或中止）、`2`（输入用法错误）。遇到问题可先运行 `info`，再查看对应命令的 `--help`；无需修改 `pythonProject/` 源码来配置本机路径。
