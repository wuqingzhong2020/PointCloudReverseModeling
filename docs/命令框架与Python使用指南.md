# 命令框架与 Python 使用指南

`ReverseModeler` 的按钮、命令窗口和内建 Python 共用命令服务。入口为 **窗口 → 命令窗口**。窗口采用终端式交互，统一以 `rm>` 提示符输入和显示建模命令，没有模式切换。同一输入框自动识别建模命令和 Python 语句：`cloud.import 参数` 仍直接使用命令解析器，`print("aaaa")`、赋值、`import`、循环等交给 CPython。纯建模命令无需初始化解释器，两类输入都在同一常驻工作线程执行。

PCD 点云导入统一按毫米处理，`cloud.import` 只接受文件路径，没有 `--unit` 参数；Python 调用同样不传 `unit`。

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

点云默认单位是 mm。STEP/CWRM 仅导入模型时，`cloud.export` 不可用；可用 `model.export_pcd` 从模型表面采样生成 PCD。`model.export_cwrm` 导出当前模型；`file.convert` 独立转换模型文件，禁止 PCD 输入（包括 PCD 转 PCD），正常点云导入导出不受影响。文件读取、建模、导出失败均保留错误码；具体几何行为沿用现有业务模块。

工具面板中的预览、板件适用性检查及参数提交失败，也会在命令窗口显示操作名称和具体原因；例如“生成下底板预览（部件 imported-part-3）失败：单块水平板方式：板件偏离水平 90.000°，超过 5° 容差；请切换生成方式、重新选板或旋转板件后重试。”。圆角/倒角、开孔、修改板厚、外扩轮廓、贴板/底板生成，以及移动/旋转、基准面的本地错误使用同一日志出口。预览失败不修改模型、不加入命令历史或录制脚本；主动取消预览不报生成失败。正式应用命令的失败只报告一次。

日志功能在命令窗口内，不提供独立的“日志窗口”。每条命令依次显示命令、日志和结果摘要；GUI 操作和手动输入的建模命令都依次显示 `rm> 命令 参数`、`// 命令 参数`，随后显示日志和结果；第一行表示输入，第二行表示开始执行。`rm>` 命令行顶格，`//` 执行记录、日志和结果的每一行统一缩进 4 个空格；缩进仅影响显示，不改变命令参数和录制脚本。手输已有的输入行不重复打印，GUI 自动补齐输入行。脚本逐条显示 `// 命令 参数`，日志正文不带分类前缀。导入包含点数及识别提示，重建包含模型信息和警告，导出包含路径/点数或实体数/文件大小，偏差包含均值、RMS、P95 和最大值。界面、手输命令和脚本共用这些业务信息，完整工程 JSON 不会随普通操作自动输出；耗时诊断仍写入日志文件。需要完整工程数据时显式执行 `project.snapshot`。空行回车只换一个提示符；只有 `help`、`-h/--help` 才显示帮助。

影响数据或输出结果的 UI 操作开始执行时，命令窗口自动以 `rm> 命令 参数` 和 `// 命令 参数` 两行回显完整命令，随后按顺序追加运行日志和结果，无需开始录制。包括文件导入、点云框选/增选/减选/清空选区、截取/删除、撤销/重做/重置、重建、偏差计算及 PCD/STEP 导出。统一显示 `cloud.crop --regions ...` 等原生命令，并加入上下键历史，可复制、修改参数后执行。窗口隐藏时仍收集回显，当前未提交的输入保留。“复制所选”复制选中的命令或日志；“清空输出”及 Ctrl+L 只清理显示，保留输入草稿、上下键历史和录制文件。执行中也可复制、清空。

例如界面截取点云后会显示完整选区参数（下列矩阵和边界仅作示例，实际值由框选生成）：

```text
rm> cloud.crop --regions '[{"mode":"replace","rectangle":{"minX":-0.8,"minY":-0.4,"maxX":0.6,"maxY":0.3,"viewProjection":[0.01,0,0,0,0,0.01,0,0,0,0,0.01,0,0,0,0,1],"zeroToOneDepth":false}}]'
    // cloud.crop --regions '[{"mode":"replace","rectangle":{"minX":-0.8,"minY":-0.4,"maxX":0.6,"maxY":0.3,"viewProjection":[0.01,0,0,0,0,0.01,0,0,0,0,0.01,0,0,0,0,1],"zeroToOneDepth":false}}]'
    点云已更新，当前 12000 点。
```

界面的截取/删除命令自带 `--regions`，无需先执行 `cloud.select`。每一项保存选区位置 `(minX,minY)`、右上边界 `(maxX,maxY)`（宽高分别为两者之差）、当时的 `viewProjection` 矩阵、深度约定和 `mode`。这些是标准化屏幕坐标 NDC（范围 -1 到 1），不是窗口像素；以后旋转视图、调整窗口大小或存在其他当前选区，都不会改变命令使用的范围。多次增选/减选按数组顺序完整保存。若初始选区来自点索引，另带 `--indices`，先用这些索引初始化再应用框选序列。

复现时，先用相同单位导入同一点云，并完成该次截取之前已有的点云编辑，再复制这一条 `cloud.crop` 或 `cloud.delete` 执行；不依赖当前视口和当前选区。计算选区与编辑在一次提交内完成，参数错误、空选区或取消不会留下中间选区修改。撤销恢复工程内容并清空临时选区；需要再次编辑时重新框选，或复用原编辑命令中的完整框选参数。清空选区、导入新文件和成功编辑后不沿用旧选区参数。

为了兼容已有脚本，不带 `--regions`/`--indices` 的手输旧命令仍使用当前选区；**UI 生成的截取/删除命令始终携带显式参数**。仅按索引重放也可用 `cloud.crop --indices '[0,1,2]'`。清空选区回显 `cloud.select_indices --indices [] --mode clear`。

重建命令包含实际提交的镜像开关、端部容差或手动尺寸/坐标框架；尚未应用的表单编辑不单独发命令。视角旋转/缩放、颜色、部件高亮、窗口显隐等纯显示操作不回显，待接入的几何按钮也不会输出虚假的已执行命令。失败或取消会在对应命令下标注“未完成”，不会显示成功；每条手输命令只有一行输入和一行执行日志，空行和 Python input() 的回答不生成建模命令回显。

重复执行导出命令时，应将路径改为新的输出文件名；已有文件继续受到防覆盖保护。

参数输入错误只提示具体参数、收到的值和要求，例如 `model.reconstruct --mirror wrong` 提示 `参数「--mirror」的值 string "wrong" 无效；要求 bool {true|false}。`，不展开整份用法。不认识的参数、重复参数、漏填值、多余位置参数也分别定位；错误码及结构化详情仍保留给脚本。

命令参数对可以在位置参数前后混排：

```text
file.convert --spacing 2 "D:/data/model.step" "D:/data/model.pcd"
file.convert "D:/data/model.step" "D:/data/model.pcd" --spacing=2
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
rm> cloud.import "D:/data/中文 点云.pcd"
```

`help cloud.import` 查询建模命令，`help(str)` 使用 Python 自带帮助。`cloud.*`、`model.*`、`project.*` 的命令写法和参数校验保持不变，参数错误不会退回 Python 重新解释；其他 Python 表达式和语句交给 CPython。变量在当前程序会话内保留。多行语句出现 `...` 提示符，保持 Python 缩进，输入空行结束；Ctrl+C 可取消未完成的语句。

脚本保持 **`.py` 扩展名**，可以只有建模命令，也可混合 Python：

```text
print("开始建模")
cloud.import "D:/data/中文 点云.pcd"
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

执行脚本时，Python 的 `print()` 输出进入命令窗口；实际执行的建模命令逐条显示 `// 命令及参数` 及结果，循环内多次执行会多次显示，未进入的分支不会回显。参数错误保留行号、参数名称和要求，Python 错误显示行号及异常摘要；诊断堆栈只写日志。未捕获错误停止后续执行，前序提交保留。混合脚本先由 Python 编译检查语法，语法错误时不会执行文件。

`input("问题")` 会在命令窗口等待回答，Enter 提交，Ctrl+D 结束输入，Ctrl+C 中断。读取输入和脚本执行均在常驻工作线程，GUI 继续响应；整份脚本占用一个 FIFO 作业，其他命令在其后执行。不可中断的第三方调用需等其返回后取消。Python 可访问本机资源，程序仅运行用户主动选择的脚本，不自动执行点云旁的文件。

## 启动时执行命令或脚本

`ReverseModeler.exe` 支持以下启动参数。无参数或使用 `-c/-f` 时打开 GUI；`-h/--help` 只在启动它的终端打印用法并立即退出，不创建 GUI。

| 启动方式 | 行为 |
|---|---|
| 无参数 | 正常打开工作台，不执行命令 |
| `--input_file 点云.pcd` | 按 mm 自动导入一次点云，不自动重建 |
| `--record_file 录制.py` | 启动即录制；文件已存在时直接覆盖并打印日志，父目录自动创建 |
| `--output_dir 输出目录` | 初始化结果文件并启用固定路径“导出并退出”；可与其他选项组合 |
| `-c 命令 [参数...]` | 执行一条产品命令，后续参数保持各自的边界 |
| `-c "完整命令或 Python 语句"` | 按命令窗口的统一输入方式执行 |
| `-f 脚本.py [参数...]` | 执行指定文件，后续参数交给脚本的 `sys.argv[1:]` |
| `-h` / `--help` | 向终端标准输出打印启动用法、参数填写方式和示例，然后以状态码 0 退出 |

在 EXE 所在目录打开 PowerShell，例如：

```powershell
.\ReverseModeler.exe -h
.\ReverseModeler.exe --help
.\ReverseModeler.exe -c help cloud.import
.\ReverseModeler.exe -c cloud.import "D:/data/中文 点云.pcd"
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
- 启动入口先从全部参数中提取 **仅三个指定键** `--input_file`、`--output_dir`、`--record_file`（支持 `--键 值` 和 `--键=值`），再从剩余参数识别最前面的 `-c` 或 `-f`。不提取其他 `--` 选项：`--spacing`、`--count`、`--help`、负数等保持原始顺序和参数边界，交给命令或脚本。例如 `-c cloud.import --help` 查询导入命令的帮助，`-f job.py --help` 则由脚本处理帮助参数。
- 含空格的路径或单个参数须由启动终端正确引用。`-c` 的完整 Python 语句须作为一个参数传入；产品命令可按多个参数传入，也可把整条命令用引号包起来。命令参数不会插值为 Python 变量。
- 相对路径按启动时工作目录解析，不自动切换到 EXE 或脚本目录；脚本可用 `__file__` 定位旁边的文件。
- 缺少命令、缺少文件路径或未知启动选项，会向终端标准错误打印错误和用法，以状态码 2 退出，不打开 GUI；`-f` 指定的脚本不存在时，仍在工作台报告读取失败。错误不会触发其他命令。
- Windows 下兼容 Git Bash、PowerShell/CMD 及输出重定向，例如 `./ReverseModeler.exe --help > usage.txt`。管道和文件输出为 UTF-8；原生控制台通过 Unicode 输出中文，不修改终端代码页。
- 启动任务复用常驻工作线程和 FIFO，支持正常的回显、参数检查、中断和 `input()`。成功修改工程后，点云、模型树和视图同步更新。
- 普通脚本结束、报错或取消后恢复先前的 `sys.argv` 和 `__file__`，避免后续交互或其他脚本读到上一份脚本的参数。普通界面“执行脚本”时 `sys.argv` 只有脚本路径。

## 启动录制与交付命令

```powershell
.\ReverseModeler.exe --record_file "D:/脚本/本次录制.py" --output_dir "D:/任务/001" --input_file "D:/扫描/梁.pcd"
.\ReverseModeler.exe --record_file "D:/脚本/重新录制.py" -f "D:/脚本/本次录制.py"
```

`--record_file` 指定 `.py` 文件。启动时最先打开录制，已有文件直接覆盖并在命令窗口显示“录制文件已存在，已覆盖：路径”；不弹确认。写入或创建失败时停止后续启动操作，保留工作台。录制目标不能与 `-f` 待执行文件相同，避免覆盖脚本输入。`-h/--help` 只打印帮助，不创建或覆盖录制文件。

成功的启动操作会按顺序写成可执行命令，随后每条成功命令立即追加并刷新：

```text
project.set_output_dir "D:/任务/001"
cloud.import "D:/扫描/梁.pcd"
model.reconstruct --mirror false --end-tolerance 30
project.export_and_exit
```

`project.set_output_dir <path>` 设置固定交付目录，也可直接在命令窗口执行。相同实际目录重复设置不重写结果；不同目录报冲突。开始录制较晚时，会补写当前输出目录、最近一次成功导入及此后的完整必要操作过程，包含撤销、重做和重置；补写只写入文件，不在当前工程重新执行。既往文件导出、退出、帮助和纯查询不补写。也可使用 `--record_file` 从启动开始录制。

GUI“导出并退出”、直接输入 `project.export_and_exit`、`-c` 和脚本执行使用同一命令。完整模型导出并提交结果后，该命令写入录制文件，刷新并关闭录制，再退出程序。失败或取消不退出；交付成功但录制保存失败时保留交付结果并阻止自动退出，显示具体原因。成功退出命令终止脚本剩余内容和队列后续任务。

回放录制文件可直接使用 `-f 脚本.py`，无需重复提供输入和输出参数。文件内保存实际绝对路径，回放会向该目录交付结果；需要其他输出位置时修改 `project.set_output_dir` 一行。上述两个新命令支持 `help` 和 `-h/--help`。

## 自动导入与导出并退出

输入文件、输出目录和录制脚本三个全局路径选项相互独立，可以放在 `-c/-f` 的前面或后面：

```powershell
# 自动导入，手动重建；完成后点击“导出并退出”
.\ReverseModeler.exe --input_file "D:/扫描/梁 点云.pcd" --output_dir "D:/任务/001"

# 只自动导入：普通导出照常使用，“导出并退出”禁用
.\ReverseModeler.exe --input_file "D:/扫描/梁 点云.pcd"

# 只指定输出目录：手动导入、建模，或用命令导入已有模型
.\ReverseModeler.exe --output_dir "D:/任务/002" -c cloud.import "D:/模型/梁.step"

# 指定键先提取；脚本只收到 aa、--count、3 三个参数
.\ReverseModeler.exe -f "D:/脚本/建模.py" aa --count 3 --input_file "D:/扫描/梁.pcd" --output_dir "D:/任务/003"
```

这三个键是应用保留的全局选项，即使写在 `-f` 后面也不进入 `sys.argv`。重复指定、缺值、空字符串是启动参数错误；提示后退出。路径支持中文、空格，相对路径按启动工作目录转为绝对路径。`-h/--help` 不创建目录、不初始化结果、不自动导入。

启动顺序为 **开始录制 → 初始化输出 → 自动导入完成 → 执行 `-c/-f`**，每一步都在现有命令工作线程执行。自动导入仅接受当前支持的 PCD 点云，固定 mm，不弹选文件框、不自动重建。路径不存在、不是文件、格式不符或内容损坏时在日志报告具体原因，保留手动入口；已经初始化的结果仍为失败状态。输出初始化失败也保留工作台供手动使用，但禁用“导出并退出”。

顶部“文件”分组的 **导出并退出** 始终显示。只有通过 `--output_dir` 或 `project.set_output_dir` 成功初始化结果目录、当前有可导出的最终模型、没有运行中的任务和未应用参数时才启用。悬停显示目标路径或禁用原因。点击即确认当前模型，固定导出全部有效构件，不只是选中部件，也不需要选择文件名。

固定交付文件：

```text
<output_dir>/
├── reserveModel.step
└── result.json
```

`result.json` 为无 BOM 的 UTF-8，只包含两个键；启动时写 `false`：

```json
{
  "model_file": "reserveModel.step",
  "export_success": false
}
```

完整 STEP 先在输出目录内的独立临时目录生成，经原有回读校验和非空检查后原子替换 `reserveModel.step`，最后原子提交 `export_success: true` 的 JSON 并自动退出。旧 STEP 在新文件完成前保留。导出期间禁止重复提交和改变模型；取消、文件占用、STEP 写入失败或 JSON 更新失败都不自动退出，允许修正后重试。普通手动导出、标题栏关闭以及启动后的导入失败均不把标志设为 `true`。

每个外部任务使用一个新的输出目录。运行期间独占该目录，其他实例不能初始化或覆盖它；锁随进程结束释放，Windows 下锁文件随句柄关闭自动删除。串行复用目录时本次启动先把旧 `true` 重置为 `false`，但如果程序根本没启动或初始化失败，旧文件不能代表本次结果。外部调用方应等待本次进程结束，再检查 JSON 成功标志以及 STEP 文件存在、非空且可读取。

## 3. 录制与记录位置

顶部 **脚本** 分组只有三个按钮：

1. **开始录制**：先选择 `.py` 脚本文件，未输入扩展名时自动补齐。确认后立即创建文件，再操作界面或输入命令。选择已有文件时直接覆盖，并在命令窗口打印覆盖提醒。
2. **停止录制**：结束录制并关闭文件，不需要另外保存。
3. **执行脚本**：选择 `.py` 命令文件，后台逐行执行。录制期间也能执行，其中成功的业务命令会实时追加到当前录制文件；Python 的循环、打印和输入语句本身不录制，记录的是实际执行的建模命令。

每条成功执行的业务命令（包括显式查询）都立即以原生命令文本追加并刷新，展开默认参数；不生成 Python 包装和版本调用。帮助、参数错误、执行失败和取消的命令不录制。界面显示录制文件名及条数，悬停可查看完整路径。写入失败会停止录制并报告原因，已执行的业务不会撤销，文件中已写完的命令保留。

执行前先完整读取脚本当前内容；即使选择正在录制的同一文件，本次也只执行读取时已有的命令，新追加的内容不会被本次继续读取。

录制的是业务命令，不是鼠标事件。框选保留矩形边界、投影矩阵、深度约定及增选/减选方式；截取/删除命令保留完整 `regions`/`indices` 参数。中途开始录制会自动补写当前会话的前序命令，停止录制期间的编辑也会在下一次开始录制时补齐；日志显示补写条数。录制路径规范化为绝对路径，换机器时需修改文件位置。视角、颜色、窗口显隐等纯显示操作不录制。

“导入 → 框选 → 截取”生成 `cloud.import ...`、`cloud.select --rectangle ...`、`cloud.crop --regions ...` 三行，可在命令窗口直接逐行粘贴，也可执行整个文件。重复执行导出操作时，应修改输出文件名，避免覆盖已有文件。

交互历史与录制分开：历史包含错误和帮助，保存在 Qt `AppLocalDataLocation/command-history.json`。开发目录的业务日志为 `output/log/commands.log`；安装版为用户数据目录的 `log/commands.log`。路径从运行环境解析，不写入开发机固定路径。

### 撤销、重做、重置与录制

| 命令 | 作用 |
|---|---|
| `project.undo` | 撤销上一步工程编辑，最多保留 10 步 |
| `project.redo` | 恢复已撤销步骤保存的结果，不重新运行建模算法 |
| `project.reset` | 恢复最近一次成功导入的点云或 STEP/CWRM 模型；这次重置也可撤销 |

点云截取/删除、模型重建/参数建模、模型部件移动/旋转/删除和已确认的基准面设置共用历史。新编辑截断重做分支；选择、查询、偏差计算、剖视显示和文件导出不占撤销步骤。成功导入新文件建立新历史边界，失败导入保留原状态。`cloud.undo/redo/reset` 转发到相同工程历史；录制统一使用 `project.*`。

恢复时同步几何、部件 ID、板厚及已提交参数；临时选区、测量和旧偏差/导出缓存清空。基准面属于工程内容，剖视等显示开关不随历史回滚；基准面消失时关闭剖视，重置也会关闭剖视。源文件、已经导出的磁盘文件、相机、窗口布局和输出目录不回滚。没有导入基线时重置不可用；显式提供坐标框架的参数建模仍可撤销到空工程。

录制只追加命令：撤销追加 `project.undo`，不删除之前已写入的编辑行；重做追加 `project.redo`，不重复录入原编辑命令。例如：

```text
cloud.import "D:/data/model.step"
model.delete --parts '["imported-part-1","imported-part-3"]'
project.undo
project.redo
project.reset
project.undo
```

最终回到删除两个部件后的状态。即使在 `project.undo` 之后才开始录制，也会补写此前导入、删除和撤销，后续 `project.redo` 有正确的历史可恢复。

回放依赖相同输入文件内容和命令语义；运行期间撤销/重做/重置使用内存快照，不重新读取源文件。无历史、业务失败会停止脚本后续内容，之前成功操作仍保留。脚本大小上限 64 MiB，补写或录制超限会报告并停止；文件写入失败不撤销已经成功的业务操作。会话补写不完整时拒绝开始新的完整录制。

### Python 有类型接口（开发用）

`CommandService::submitPython()` / `submitPythonLine()` 和内建 `rm.execute`、`rm.execute_line`、`rm.help` 仍可用于有类型的程序调用。用户日常输入使用同一窗口自动分流，不需要这些包装。Python 对象只在命令工作线程访问。命令定义和帮助不包含模块版本。

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

上例 `service.importSynchronously` 是宿主函数示意。实际产品在 `CommandBindings.cpp` 绑定 `ProjectOperations`，任务输出目录与导出退出命令由 `CommandService.cpp` 绑定并协调录制和服务生命周期。没有统一 executor 时直接调用对应 handler；没有 handler 时仍可校验和查询帮助，执行返回 `handler_not_bound`。禁止 handler/executor 重入执行、异步保留 next、切换线程或重复调用 next。

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

### 移动和旋转模型部件

先通过 `project.snapshot` 或模型树属性取得部件 ID。单位固定为 mm，旋转中心为项目坐标，不是相对显示原点的坐标。多个部件绕同一中心一起旋转。下面的示例适用于实际含这些 ID 的导入模型：

```text
model.move --parts '["imported-part-1","imported-part-3"]' --offset '[-100,0,0]'
model.rotate --parts '["imported-part-3"]' --axis z --angle -60 --center '[0,0,0]'
project.undo
project.redo
help model.rotate
```

旋转只允许 `x/y/z` 单轴，角度范围为 `[-180,180]`，按右手规则；中心必须显式填写。参数缺失、数组长度不为 3、非法轴、超范围角度或任意不存在的部件 ID 会使整条命令失败，不产生局部修改。零位移/零角度不新增历史或录制行。

GUI 调整时只是预览；点击确定才回显 `rm> model.move ...` / `rm> model.rotate ...`，并缩进输出 `// ...` 及结果日志。录制只保存确定后的完整命令。新命令实际改变几何，后续 STEP 导出、拾取和测量均采用更新后的部件；再次重建会按点云/参数重新生成模型，之前的手动位置调整不会自动套用。

### 圆角与倒角

```text
model.fillet --part web --edges '[1,3]' --size 1
project.undo
model.chamfer --part web --edges '[1,3]' --size 0.5
project.undo
project.redo
```

`--part` 为部件 ID，`--edges` 为该部件当前 BRep 边编号，从 1 开始。单部件时两项一起填写；跨部件时改用 `--targets` 分组数组，不可混用。每个部件只能出现一个分组，同一部件的多条边放在该组的 `edges` 中。界面选边后应用会自动生成对应命令。操作会改变实体拓扑，后续操作应使用当前模型的边编号。

跨部件示例（各部件使用同一数值）：

```text
model.fillet --targets '[{"part":"web","edges":[1]},{"part":"flange-bottom","edges":[1]}]' --size 1
project.undo
```

整批成功后生成一条录制命令、一个撤销步骤；任一部件失败则整批不应用，错误信息指出失败部件。

处理对象是等厚板件的平面轮廓，板厚保持不变。所选直线轮廓边对应两端板角，板厚方向的短角边对应一个角；重合板角自动去重。圆角的 `--size` 是轮廓半径，等距倒角的 `--size` 是轮廓两侧距离，单位 mm，必须大于 0；数值受轮廓空间限制，不以板厚作为上限。直接执行命令会应用修改；界面回车和“预览”不会录制、不会占用撤销步骤，“应用”才录制。失败的几何计算不改变实体，也不会破坏重做分支。预览和应用均在工作线程执行，GUI 可继续响应。

已应用的圆角或倒角不保留可编辑的特征参数。如需调整，先执行 `project.undo`，再重新选边并应用新参数。录制脚本会保留这些操作，可按执行顺序回放。


### 修改板厚

在命令窗口或录制的 `.py` 脚本中直接执行：

```text
help model.set_thickness
model.set_thickness --parts '["imported-part-5"]' --thickness 20
model.set_thickness --parts '["web","rib-1-right"]' --thickness 16
project.undo
project.redo
```

`--parts` 为当前模型的稳定部件 ID 数组（可在属性面板查看），`--thickness` 为目标板厚，单位 mm，范围 0.001–1000000。示例中的 ID 需对应当前工程；导入模型和重建模型使用不同 ID。

修改保持各板件中面与平面轮廓不动，向两侧等量增减，保留贯穿孔与轮廓圆角。支持单件和多件等厚实体；不支持开放观测面、变厚板及曲面板。任何目标失败均不修改整批模型，不消耗撤销步骤。相邻板件不会自动移动，因此需要自行核对连接位置。

界面“修改板厚”的回车/预览只显示效果，点击“应用”才回显并录制上述完整命令；命令直接执行则立即提交。一个批次对应一个撤销步骤，支持中途开始录制补写、脚本回放和 STEP 导出；重新重建会以原始点云/参数为准，不自动套用手动板厚。


### 外扩板件轮廓

在命令窗口或 `.py` 录制脚本中使用：

```text
help model.expand_contour
model.expand_contour --parts '["imported-part-1"]' --distance 10
model.expand_contour --parts '["imported-part-2","imported-part-3"]' --distance 20
project.undo
project.redo
```

`--parts` 为当前工程部件 ID，可从属性框或 `project.snapshot` 查询；`--distance` 为正的外扩毫米值，表示每条外边界向外偏移的距离。例如 200 × 100 × 12 mm 的矩形板外扩 10 mm 后为 220 × 120 × 12 mm，板厚不变。

封闭内孔的位置和尺寸不变；开放缺口属于外轮廓，随轮廓一起偏移。支持等厚实体板及多板件批量处理；任一部件不支持或偏移失败时整批不修改。相邻板件不会自动避让。

界面选择“模型调整 → 外扩板件轮廓”，可 Shift 多选。输入后回车或“预览”只看效果；点击“应用”才回显并录制 `model.expand_contour`。单次预览的多次改值都以应用前模型为基准；再次应用是累计外扩，可先撤销再改值。支持撤销/重做、STEP 导出及录制回放；预览不写入录制脚本。


### 生成外扩贴板与下底板

```text
# 贴板：对部件的主面外扩 30 mm，再沿该面外法线生成 10 mm 厚独立板
model.generate_cover_plate --part imported-part-1 --face 3 --offset 30 --thickness 10 --reverse false
# 单块水平板：底面轮廓投影至最低 Z，四周外扩 20 mm，向下生成 10 mm 厚独立板
model.generate_base_plate --mode horizontal --part imported-part-1 --offset 20 --thickness 10
# 两块竖直板：用两板底部外包络及中间间隔生成一块跨接底板
model.generate_base_plate --mode vertical_pair --part imported-part-2 --second-part imported-part-3 --offset 20 --thickness 10
help model.generate_cover_plate
help model.generate_base_plate
```

`--part` 为当前工程稳定部件 ID，`--face` 为该实体 BRep 面序号（从 1 开始）。示例面号不一定是实际模型的主面；建议界面选面应用后直接复制回显/录制命令。`--reverse true` 沿选中面内法线拉伸，可能与原板重叠。贴板只接受等厚实体的两个平面主面。

下底板的 `--mode horizontal`（默认）需要一块水平板，不接受 `--second-part`；`--mode vertical_pair` 必须用 `--part` 和 `--second-part` 指定两块不同的竖直板。界面通过“生成方式”选择，竖板方式按 Shift 加选第二块。两板应相对平行（水平夹角 ≤5°）、横向分开、沿长度有重叠，底部是连续直边组成的四边形支撑面。用两板底部角点的外包络覆盖两板及中间间隔，最终生成一块底板。水平方式保留单板底面的真实外轮廓。大面允许相对指定方向偏差 ±5°（含边界），朝下支撑面也须在水平 ±5° 内。统一投影到所选板最低 Z 后外扩并向下拉伸，新板平行 XY；原板不动，高度差或微倾斜处可能有间隙。不符合数量、方向、底部形状等要求会报告原因，原模型不变。已有单竖板脚本需改成 `vertical_pair` 并补充第二块板，不能继续用水平方式运行。

贴板参数范围：外扩 -100～100 mm、厚度 1～100 mm；下底板：外扩 4～1000 mm、厚度 4～50 mm。轮廓消失、自交、分裂或厚度大于轮廓窄边时拒绝应用。两个命令均创建新部件，保留源部件，不继承封闭内孔。

下底板固定沿世界 -Z 生成，不受基准面、重建框架或相机影响。保留 `--up` 参数以读取已录制的 `[0,0,1]` 命令；其他方向报错，不会静默改变生成方向。新增部件可撤销、重做及 STEP 导出；预览不写入脚本，应用成功才录制。再次执行会另建一块板；若要重调上次参数，请先撤销。

### 镜像补全

“模型调整 → 钢结构 → 镜像补全”以底板（腹板）的真实中面补齐选中板件的另一侧。待补全板件与镜像参考是两种不同选择：视口/模型树用 Shift 多选待补全板件，在“镜像面”下拉框指定底板。重建模型默认使用 `web`；导入 STEP/CWRM 时需要指定实际底板，不按世界 XY 平面猜测。

```text
model.mirror_complete --parts '["flange-top","flange-bottom"]' --plane-part web
model.mirror_complete --parts '["imported-part-2","imported-part-5"]' --plane-part imported-part-1
project.undo
project.redo
```

原有几何保留；与原板相接/重叠并能合并为单实体的镜像保留原部件 ID，分离的镜像生成 `mirrored-part-N` 独立部件。板厚、孔洞和轮廓随反射保留。底板必须为可识别的等厚实体，不能同时选为待补全对象。已有完整对称部件时提示无需补全；镜像与其他部件部分重叠时整批失败，原因显示在命令窗口。

“预览”在后台生成临时几何，勾选“对比原模型”查看前后差异；预览不写历史或录制文件。“应用”通过上述命令提交，支持撤销、重做、录制、回放及 STEP 导出。相机、点云和基准面不改变。导入文件原本没有板厚标签时不会因镜像自动添加标签。
