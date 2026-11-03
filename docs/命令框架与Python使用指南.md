# 命令框架与 Python 使用指南

`ReverseModeler` 的按钮、命令窗口和内建 Python 共用命令服务。入口为 **窗口 → 命令窗口**。窗口采用终端式交互，统一以 `rm>` 提示符输入和显示建模命令，没有模式切换。同一输入框自动识别建模命令和 Python 语句：`cloud.import 参数` 仍直接使用命令解析器，`print("aaaa")`、赋值、`import`、循环等交给 CPython。纯建模命令无需初始化解释器，两类输入都在同一常驻工作线程执行。

## 1. 在界面中使用

在提示符后输入，按 **Enter** 提交。上下键浏览命令历史，**Tab** 提供命令及参数的前缀补全，**Ctrl+L** 清屏。历史输出不可修改，可以选择复制；Ctrl+A 只选择当前输入。在 `rm>` 后可直接输入：

```text
help
help cloud
help model.reconstruct
model.reconstruct --help
cloud.import "D:/data/中文点云.pcd"
model.reconstruct --end-tolerance 30 --mirror false
project.snapshot
model.export_step "D:/data/result.step"
cloud.export "D:/data/result.pcd"
```

点云默认单位是 mm。STEP/CWRM 仅导入模型时，没有点云可供导出 PCD。文件读取、建模、导出失败均保留错误码；具体几何行为沿用现有业务模块。

普通操作成功时仅显示一句摘要，例如“已导入点云，共 1428008 点。”；耗时记录写入日志，不在控制台附带完整工程 JSON。需要完整工程数据时显式执行 `project.snapshot`。空行回车只换一个提示符；只有 `help`、`-h/--help` 才显示帮助。

影响数据或输出结果的 UI 操作完成后，命令窗口自动显示 `[界面]` 标记、对应命令和简短结果，无需开始录制。包括文件导入、点云框选/增选/减选/清空选区、截取/删除、撤销/重做/重置、重建、偏差计算及 PCD/STEP 导出。统一显示 `cloud.crop --regions ...` 等原生命令，并加入上下键历史，可复制、修改参数后执行。窗口隐藏时仍收集回显，当前未提交的输入保留。

例如界面截取点云后会显示完整选区参数（下列矩阵和边界仅作示例，实际值由框选生成）：

```text
[界面] rm> cloud.crop --regions '[{"mode":"replace","rectangle":{"minX":-0.8,"minY":-0.4,"maxX":0.6,"maxY":0.3,"viewProjection":[0.01,0,0,0,0,0.01,0,0,0,0,0.01,0,0,0,0,1],"zeroToOneDepth":false}}]'
点云已更新，当前 12000 点。
```

界面的截取/删除命令自带 `--regions`，无需先执行 `cloud.select`。每一项保存选区位置 `(minX,minY)`、右上边界 `(maxX,maxY)`（宽高分别为两者之差）、当时的 `viewProjection` 矩阵、深度约定和 `mode`。这些是标准化屏幕坐标 NDC（范围 -1 到 1），不是窗口像素；以后旋转视图、调整窗口大小或存在其他当前选区，都不会改变命令使用的范围。多次增选/减选按数组顺序完整保存。若初始选区来自点索引，另带 `--indices`，先用这些索引初始化再应用框选序列。

复现时，先用相同单位导入同一点云，并完成该次截取之前已有的点云编辑，再复制这一条 `cloud.crop` 或 `cloud.delete` 执行；不依赖当前视口和当前选区。计算选区与编辑在一次提交内完成，参数错误、空选区或取消不会留下中间选区修改。撤销会同时恢复该次编辑前的选区及其框选参数；清空选区、导入新文件和成功编辑后不沿用旧选区参数。

为了兼容已有脚本，不带 `--regions`/`--indices` 的手输旧命令仍使用当前选区；**UI 生成的截取/删除命令始终携带显式参数**。仅按索引重放也可用 `cloud.crop --indices '[0,1,2]'`。清空选区回显 `cloud.select_indices --indices [] --mode clear`。

重建命令包含实际提交的镜像开关、端部容差或手动尺寸/坐标框架；尚未应用的表单编辑不单独发命令。视角旋转/缩放、颜色、部件高亮、窗口显隐等纯显示操作不回显，待接入的几何按钮也不会输出虚假的已执行命令。失败或取消会在对应命令下标注“未完成”，不会显示成功；在控制台手输的命令不再重复回显。

重复执行导出命令时，应将路径改为新的输出文件名；已有文件继续受到防覆盖保护。

参数输入错误只提示具体参数、收到的值和要求，例如 `model.reconstruct --mirror wrong` 提示 `参数「--mirror」的值 string "wrong" 无效；要求 bool {true|false}。`，不展开整份用法。不认识的参数、重复参数、漏填值、多余位置参数也分别定位；错误码及结构化详情仍保留给脚本。

命令参数对可以在位置参数前后混排：

```text
cloud.import --unit mm "D:/data/中文点云.pcd"
cloud.import "D:/data/中文点云.pcd" --unit=mm
```

两行含义相同。重复选项即使值相同也报错；`--` 终止选项识别。`--path=--help` 中的 `--help` 是值；独立的 `-h/--help`（且在 `--` 之前）请求帮助，不执行业务。Windows 路径的反斜杠保持原样；包含空格的路径用引号包裹。数组/对象文本采用 JSON，外层可用单引号避免内部双引号被命令词法器处理：

```text
cloud.select_indices --indices '[0,1,2]' --mode replace
cloud.crop
cloud.undo
```

命令窗口工具栏显示当前输入的排队、运行或等待回答状态，“中断”取消该任务（也可以取消尚未开始的排队任务）。队列最多容纳 64 条等待任务，所有命令在同一常驻工作线程按 FIFO 执行。GUI 不等待结果。界面带版本的请求在实际执行前校验；过期时返回 `stale_revision`。

## 2. Python 与建模命令混合使用

命令窗口只有一个 `rm>` 输入入口，无需切换模式：

```text
rm> print("aaaa")
aaaa
rm> amount = 21
rm> amount * 2
42
rm> import math
rm> math.sqrt(9)
3.0
rm> help cloud.import
rm> cloud.import "D:/data/中文 点云.pcd" --unit mm
```

`help cloud.import` 查询建模命令，`help(str)` 使用 Python 自带帮助。`cloud.*`、`model.*`、`project.*` 的命令写法和参数校验保持不变，参数错误不会退回 Python 重新解释；其他 Python 表达式和语句交给 CPython。变量在当前程序会话内保留。多行语句出现 `...` 提示符，保持 Python 缩进，输入空行结束；Ctrl+C 可取消未完成的语句。

脚本保持 **`.py` 扩展名**，可以只有建模命令，也可混合 Python：

```text
print("开始建模")
cloud.import "D:/data/中文 点云.pcd" --unit mm
model.reconstruct --mirror false --end-tolerance 30
for i in range(2):
    print("第", i + 1, "次查看工程")
    project.snapshot
model.export_step "D:/data/result.step"
print("结束")
```

建模命令不需要 `import rm`、`rm.execute()` 或版本声明。它们须单独占一行，可出现在 Python 的函数、循环或条件块中，按实际控制流程执行。程序按 Python 的词法边界处理字符串、三引号文本、括号续行和注释；其中的命令名称只是文本，不会被误执行。原生命令参数保持命令窗口的写法：`true/false`、JSON 数组/对象和带引号的文件路径；Python 语句则使用 Python 类型和语法。

此扩展语法由本程序处理，包含 `cloud.import 参数` 的文件不能直接交给外部 Python 运行。原生命令参数不会隐式读取 Python 变量；需要传递计算值时，开发接口仍可使用已内建的 `rm.execute(...)`。

文件采用 UTF-8，支持 BOM、中文文件名、CRLF/LF 换行，读取上限为 64 MiB。空行和整行 `#` 注释可用；原生命令不支持行尾注释或跨行参数。框选、截取/删除的完整矩形、投影矩阵和选择参数由录制自动写入。模块版本声明及检查保持移除。

执行脚本时，Python 的 `print()` 输出进入命令窗口；实际执行的建模命令逐条显示 `[脚本] rm> 命令及参数` 及结果，循环内多次执行会多次显示，未进入的分支不会回显。参数错误保留行号、参数名称和要求，Python 错误显示行号及异常摘要；诊断堆栈只写日志。未捕获错误停止后续执行，前序提交保留。混合脚本先由 Python 编译检查语法，语法错误时不会执行文件。

`input("问题")` 会在命令窗口等待回答，Enter 提交，Ctrl+D 结束输入，Ctrl+C 中断。读取输入和脚本执行均在常驻工作线程，GUI 继续响应；整份脚本占用一个 FIFO 作业，其他命令在其后执行。不可中断的第三方调用需等其返回后取消。Python 可访问本机资源，程序仅运行用户主动选择的脚本，不自动执行点云旁的文件。

## 启动时执行命令或脚本

`ReverseModeler.exe` 支持以下启动参数。无参数或使用 `-c/-f` 时打开 GUI；`-h/--help` 只在启动它的终端打印用法并立即退出，不创建 GUI。

| 启动方式 | 行为 |
|---|---|
| 无参数 | 正常打开工作台，不执行命令 |
| `-c 命令 [参数...]` | 执行一条产品命令，后续参数保持各自的边界 |
| `-c "完整命令或 Python 语句"` | 按命令窗口的统一输入方式执行 |
| `-f 脚本.py [参数...]` | 执行指定文件，后续参数交给脚本的 `sys.argv[1:]` |
| `-h` / `--help` | 向终端标准输出打印启动用法、参数填写方式和示例，然后以状态码 0 退出 |

在 EXE 所在目录打开 PowerShell，例如：

```powershell
.\ReverseModeler.exe -h
.\ReverseModeler.exe --help
.\ReverseModeler.exe -c help cloud.import
.\ReverseModeler.exe -c cloud.import "D:/data/中文 点云.pcd" --unit mm
.\ReverseModeler.exe -c "print('hello')"
.\ReverseModeler.exe -f "D:/scripts/建模 脚本.py" aa "bb cc" --count 2
```

最后一个例子可以使用以下 `.py` 内容：

```python
import sys
print("脚本路径：", __file__)
print("全部参数：", sys.argv[1:])
# 输出 ['aa', 'bb cc', '--count', '2']
project.snapshot
```

- `sys.argv[0]` 和 `__file__` 是本次脚本的绝对路径；后续参数均为字符串，可使用 Python 的 `argparse` 自行解析。
- 启动入口只识别最前面的 `-c` 或 `-f`，后面的 `--help`、`-c`、`-f`、负数等均属于该命令或脚本。例如 `-c cloud.import --help` 查询导入命令的帮助，`-f job.py --help` 则由脚本处理帮助参数。
- 含空格的路径或单个参数须由启动终端正确引用。`-c` 的完整 Python 语句须作为一个参数传入；产品命令可按多个参数传入，也可把整条命令用引号包起来。命令参数不会插值为 Python 变量。
- 相对路径按启动时工作目录解析，不自动切换到 EXE 或脚本目录；脚本可用 `__file__` 定位旁边的文件。
- 缺少命令、缺少文件路径或未知启动选项，会向终端标准错误打印错误和用法，以状态码 2 退出，不打开 GUI；`-f` 指定的脚本不存在时，仍在工作台报告读取失败。错误不会触发其他命令。
- Windows 下兼容 Git Bash、PowerShell/CMD 及输出重定向，例如 `./ReverseModeler.exe --help > usage.txt`。管道和文件输出为 UTF-8；原生控制台通过 Unicode 输出中文，不修改终端代码页。
- 启动任务复用常驻工作线程和 FIFO，支持正常的回显、参数检查、中断和 `input()`。成功修改工程后，点云、模型树和视图同步更新。
- 脚本结束、报错或取消后恢复先前的 `sys.argv` 和 `__file__`，避免后续交互或其他脚本读到上一份脚本的参数。普通界面“执行脚本”时 `sys.argv` 只有脚本路径。

## 3. 录制与记录位置

顶部 **脚本** 分组只有三个按钮：

1. **开始录制**：先选择 `.py` 脚本文件，未输入扩展名时自动补齐。确认后立即创建文件，再操作界面或输入命令。选择已有文件时询问是否覆盖；确认覆盖表示开始一份新录制。
2. **停止录制**：结束录制并关闭文件，不需要另外保存。
3. **执行脚本**：选择 `.py` 命令文件，后台逐行执行。录制期间也能执行，其中成功的业务命令会实时追加到当前录制文件；Python 的循环、打印和输入语句本身不录制，记录的是实际执行的建模命令。

每条成功执行的业务命令（包括显式查询）都立即以原生命令文本追加并刷新，展开默认参数；不生成 Python 包装和版本调用。帮助、参数错误、执行失败和取消的命令不录制。界面显示录制文件名及条数，悬停可查看完整路径。写入失败会停止录制并报告原因，已执行的业务不会撤销，文件中已写完的命令保留。

执行前先完整读取脚本当前内容；即使选择正在录制的同一文件，本次也只执行读取时已有的命令，新追加的内容不会被本次继续读取。

录制的是业务命令，不是鼠标事件。框选保留矩形边界、投影矩阵、深度约定及增选/减选方式；截取/删除命令保留完整 `regions`/`indices` 参数。建议**先开始录制，再导入文件**，使脚本包含初始数据来源。录制路径规范化为绝对路径，换机器时需修改文件位置。视角、颜色、窗口显隐等纯显示操作不录制。

“导入 → 框选 → 截取”生成 `cloud.import ...`、`cloud.select --rectangle ...`、`cloud.crop --regions ...` 三行，可在命令窗口直接逐行粘贴，也可执行整个文件。重复执行导出操作时，应修改输出文件名，避免覆盖已有文件。

交互历史与录制分开：历史包含错误和帮助，保存在 Qt `AppLocalDataLocation/command-history.json`。开发目录的业务日志为 `output/log/commands.log`；安装版为用户数据目录的 `log/commands.log`。路径从运行环境解析，不写入开发机固定路径。

### Python 有类型接口（开发用）

`CommandService::submitPython()` / `submitPythonLine()` 和内建 `rm.execute`、`rm.execute_line`、`rm.help` 仍可用于有类型的程序调用。用户日常输入使用同一窗口自动分流，不需要这些包装。Python 对象只在命令工作线程访问。`rm.require_version` 已移除，命令定义和帮助不包含模块版本。

内部 Python 调用仍传真实类型，严格校验参数；失败抛出 `rm.CommandError`，包含 `code`、`details`、`committed`。解释器使用部署的 `python/Lib`、`python/DLLs` 和匹配 DLL，隔离环境变量、用户 site-packages 及当前目录自动导入。构建机的 Python/Jinja 生成工具不受命令文件格式变化影响。

## 4. 构建依赖和生成

通用库 `RmCommandManager` 只有 C++20 标准库依赖。产品额外通过 `find_package(cpython CONFIG REQUIRED)`、`CPython::Python` 和 `pybind11::headers` 使用 SDK；`PYBIND11_NOPYTHON=ON` 避免绑定工具选错解释器。

生成器是构建工具，需要 Python 和 Jinja2。可在工程根目录创建独立环境，产品不会携带这个环境：

```powershell
python -m venv build/codegen-python
build/codegen-python/Scripts/python.exe -m pip install -r commandManager/generator/requirements.txt
python envBuild.py compile release
```

CMake 优先使用上述目录，也可以用 `-DRM_CODEGEN_PYTHON=<具有Jinja2的python路径>` 指定。依赖缺失会明确报错，不在配置期间自动联网安装。`envBuild.py` 自身仍只使用标准库。

```powershell
build/codegen-python/Scripts/python.exe -B commandManager/generator/rm_command_gen.py --help
build/codegen-python/Scripts/python.exe -B commandManager/generator/rm_command_gen.py check --input ReverseModeling/commandDefinitions/cloud.rmdef
build/codegen-python/Scripts/python.exe -B commandManager/generator/rm_command_gen.py docs --manifest ReverseModeling/commandDefinitions/modules.json --docs-output docs/commands --work-dir build/vc143-cpp20/commandManager/generator
build/codegen-python/Scripts/python.exe -B commandManager/generator/rm_command_gen.py --manifest ReverseModeling/commandDefinitions/modules.json --output ReverseModeling/execCommonds --docs docs/commands --build-dir build/vc143-cpp20
# 仅刷新 cloud 模块，总注册入口仍包含其他启用模块
build/codegen-python/Scripts/python.exe -B commandManager/generator/rm_command_gen.py --manifest ReverseModeling/commandDefinitions/modules.json --output ReverseModeling/execCommonds --docs docs/commands --build-dir build/vc143-cpp20 --module cloud
# 在相同参数后加 --check，只检查生成文件是否一致
```

不带动作时默认 `generate`；`check` 只检查声明，不要求安装 Jinja 或存在生成文件。`docs` 仅写 Markdown 与 `commands.help.json`，不改 C++ 源码；`generate` 可同时指定 `--docs-output`。`--templates` 可替换模板目录。`--work-dir` 直接指定构建树内临时目录，与 `--build-dir` 二选一。运行时帮助直接来自库 A 的描述数据，不读取这些外部文档。

配置阶段自动生成并编译全部启用模块；修改声明、模板或模块清单会触发重新配置。生成的 `.h/.cpp` 位于 `ReverseModeling/execCommonds/<module>`，不是配置目录副本。库输出继承主工程的 `output/<工具链>-cpp20/<配置>`；临时清单位于 `build/<预设>/commandManager/generator`。所有构建配置共用源码生成锁，内容未变化不更新时间。`.rm-generated.json` 只管理生成器拥有的文件，不递归清空目录。

`prepareRun`、构建后部署、CMake install 和 CPack 均包含 CPython DLL、标准库、配置匹配的 `.pyd` 及其扫描得到的依赖。`RM_PYTHON_HOME` 默认为所选 CPython 包的 `tools/python3`；SDK 目录结构不同时可显式配置该 CMake 路径。

## 5. 添加命令

每个模块一个 `.rmdef`，由 `ReverseModeling/commandDefinitions/modules.json` 决定启用模块与注册顺序。定义仅允许字面量声明，禁止 import、执行表达式、变量、展开和任意代码。

```python
rm_module(name="demo", summary="示例模块")
rm_define(
    name="configure",
    summary="配置示例",
    usage="demo.configure <name> --mode <aa|bb> --count <1..3 或 5..6>",
    params=[
        {"name":"name", "binding":"positional", "position":1,
         "type":"string", "required":True, "description":"对象名称"},
        {"name":"mode", "binding":"option", "option":"--mode",
         "type":"enum", "values":["aa","bb"], "required":True},
        {"name":"count", "binding":"option", "option":"--count",
         "type":"int", "ranges":[[1,3],[5,6]], "required":True},
    ],
    returns={"type":"object", "description":"处理结果"},
    examples=['demo.configure sample --mode aa --count 2'],
)
```

`usage` 是必填非空字符串。参数类型为 bool/int/uint/float/string/path/enum/array/object；int、uint 分别为 64 位有符号和无符号整数。区间是闭区间的并集，上例允许 1、2、3、5、6，拒绝 4。`required` 不可与 `default` 同时设置；默认值本身也接受校验。可选位置参数只能放在必填位置之后。对象必须声明 `properties`，每个字段都必须提供；数组用 `items` 声明元素规则，可设置 `min_size/max_size`。`nullable=True` 才允许显式 None/null。

生成文件只提供描述、Args、校验后的类型转换和定义注册；不写算法或产品服务调用。业务手写在 `ReverseModeling/commands`：

```cpp
rmcmd::registerAllCommandModules(manager); // 仅注册定义；此时即可 help/validate
manager.bindHandler<rmcmd::cloud::ImportCommand>(
    [&service](const rm::CommandContext &context,
               const rmcmd::cloud::ImportCommand::Args &args)
    {
        return service.importSynchronously(context, args.path);
    });
manager.registerExecutor([](const auto &context, const auto &command, const auto &next)
{
    // 可在这里加前置日志、后置耗时统计；next 必须同线程同步执行。
    return next();
});
manager.freeze();
```

上例 `service.importSynchronously` 是宿主函数示意。实际产品统一在 `CommandBindings.cpp` 绑定 `ProjectOperations`。没有统一 executor 时直接调用对应 handler；没有 handler 时仍可校验和查询帮助，执行返回 `handler_not_bound`。禁止 handler/executor 重入执行、异步保留 next、切换线程或重复调用 next。

## 6. 独立复用与单元测试入口

库 A 可以脱离产品配置和编译：

```powershell
cmake -S commandManager -B build/command-library-vc143 -G "Visual Studio 17 2022" -A x64 -T v143
cmake --build build/command-library-vc143 --config Release
cmake --install build/command-library-vc143 --config Release --prefix output/command-sdk
```

消费者使用安装后的 `RmCommandManager::RmCommandManager` CMake 目标。框架不要求业务绑定完整；纯校验工具只注册定义即可。

```powershell
build/codegen-python/Scripts/python.exe -B tests/commandManager/test_generator.py
cmake --build build/vc143-cpp20 --config Release --target ReverseServiceTests
ctest --test-dir build/vc143-cpp20 -C Release --output-on-failure
```

具体产品命令见生成的 [cloud](commands/cloud.md)、[model](commands/model.md)、[project](commands/project.md) 文档。
