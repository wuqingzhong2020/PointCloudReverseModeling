# ReverseModeling Python 脚本编码规范

本规范用于本工程的 Python 脚本以及未来的嵌入式 Python 扩展。当前 `ReverseModeler` 尚未接入 CPython、pybind11 或 Python 业务接口；SDK 具备相应能力并不表示宿主已经提供脚本入口。下文宿主接口示例仅用于说明设计方式。

运行时基线为 `LibPackByVcpkgWindows` 的 CPython **3.15.2 自由线程共享 SDK（3.15t）**。其本地 `python3` overlay 将 libffi、mpdecimal、zlib、OpenSSL、SQLite3、Zstandard、bzip2、liblzma 和 Expat 九个公共 C 库作为依赖。实际产品使用时仍须按所选平台、Debug/Release 和部署内容验证对应模块及动态库。

本规范中的“必须、禁止”约束项目脚本；SDK 已编入的模块、实际发布内容和尚未实现的宿主接口分别判断。构建依据见 [CPython 介绍与编译探索](../reference/CPython介绍与编译探索.md)和 SDK 的 [python3 配方](../../LibPackByVcpkgWindows/ports/python3/portfile.cmake)；扩展设计参考 [pybind11 文档](../reference/pybind11介绍与编译探索.md)及 [Python 嵌入与命令化回归测试方案](../reference/Python嵌入与命令化回归测试方案.md)。

## 1 运行环境与依赖边界

使用目标 SDK 的 3.15t 解释器进行开发与定向检查。脚本只能依赖产品实际交付的标准库、自有包及已公开宿主接口；开发机上另外安装的 Python、pip 包和 DLL 不自动成为产品依赖。

| 公共后端 | 对应标准库或能力 | 使用约定 |
|---|---|---|
| libffi | `ctypes` / `_ctypes` | 业务操作通过将来公开的宿主接口；不自行加载 PCL、OCCT、Qt 或操作宿主裸指针 |
| mpdecimal | `decimal` / `_decimal` | 用于精确十进制计算，不替代几何内核的浮点数和容差 |
| zlib | `zlib`、`gzip`、DEFLATE ZIP | 跨工具交换可优先使用 GZIP 或 DEFLATE，明确归档方法 |
| OpenSSL | `ssl` / `_ssl`、`hashlib` / `_hashlib` | 支持 TLS 和 OpenSSL 提供的散列、`pbkdf2_hmac`、`scrypt`；算法可用性仍以实际 OpenSSL 配置为准 |
| SQLite3 | `sqlite3` / `_sqlite3` | 可用于脚本负责的缓存和报告；使用参数化 SQL，按所有权关闭连接；当前工程持久化尚未实现 |
| bzip2 | `bz2` / `_bz2` | 可使用 BZIP2 压缩；交换时确认接收方支持 |
| liblzma | `lzma` / `_lzma` | 可使用 XZ/LZMA 压缩；交换时确认接收方支持 |
| Zstandard | `compression.zstd` / `_zstd` | 可使用 Zstandard 压缩；交换时确认接收方与格式版本支持 |
| Expat | `pyexpat`、`xml.etree.ElementTree` | 使用标准库 XML 接口；不为普通 XML 额外引入 Python 包 |

这些 C 库通过 Python 标准模块使用，不写 `import libffi` 或 `import mpdecimal`。对应动态库由 SDK 和产品部署提供；链接或导入失败应检查版本、配置和部署路径，不从系统 Python 目录临时拼接依赖。[Python SSL](https://docs.python.org/3.15/library/ssl.html)、[sqlite3](https://docs.python.org/3.15/library/sqlite3.html)、[Zstandard](https://docs.python.org/3.15/library/compression.zstd.html)。

### 1.1 未接入和平台边界

- CPython 配方不引入 Tcl/Tk 和 GNU Readline，禁用 `_tkinter` 与 `readline`；独立 Tcl/Tk SDK 的存在不使 `tkinter` 可用。界面由 Qt 宿主负责，C++ Qt 的存在也不代表已提供 PySide/PyQt。
- Linux 配方还禁用 `_curses`、`_curses_panel`、`_dbm`、`_gdbm` 和系统 `_uuid` 后端；不同平台的可选模块以实际包为准，不把 curses、GNU DBM 或 `readline` 当成共同功能。纯 Python 的 UUID 等能力应按实际解释器检查。
- 未列入产品发布清单的 NumPy、SciPy、pandas、requests、lxml、PySide/PyQt 等第三方 Python 包，不作为脚本依赖；C++ SDK 中有同类库不代表这些包已交付。
- 当前配方不交付 pip 引导流程。脚本不在运行时自动安装包或创建环境；依赖由产品构建与发布过程管理。
- `zoneinfo` 的地区时区规则依赖时区数据库；Windows 产品包需显式准备数据。不依赖地区规则时使用 `datetime.UTC`。[zoneinfo 数据来源](https://docs.python.org/3.15/library/zoneinfo.html#data-sources)

## 2 文件组织与基础风格

- 使用 UTF-8 源文件、4 个空格缩进；不混入 Tab。新增 Python 源文件使用 UTF-8 无 BOM；修改已有文件保留原编码和换行。代码行建议不超过 100 个字符。
- 模块、变量、函数用 `snake_case`，类用 `PascalCase`，常量用 `UPPER_SNAKE_CASE`。业务标识符使用明确英文，注释和用户错误信息可以使用中文。
- 文件以模块说明开头；普通导入分为标准库、自有包两组。禁止 `from ... import *`，不以标准库名称命名脚本，例如 `json.py`、`typing.py`、`zlib.py`。
- 公开入口写参数类型、返回类型和简短 docstring，说明单位、是否修改模型、失败条件。类型标注不能替代运行时输入校验。
- 模块顶层只定义类型、函数和轻量常量；不在导入时打开工程、执行算法、启动线程或写文件。不要依赖导入副作用完成命令注册。
- 可复用宏采用显式 `run(...)` 入口，输入、输出根目录和会话由宿主传入。`if __name__ == "__main__":` 只用于独立诊断入口，宿主导入模块时不能自动执行业务。
- 不使用可变默认参数，例如 `def run(options={})`；使用 `None` 后在函数内创建，或使用不可变配置对象。

## 3 Python 3.15 优先写法

以 3.15 为最低目标，不为 Python 2 或老版本 Python 保留兼容分支。新语法在语义适合时优先使用；简单循环不为了“新”而改成难读的表达式。

| 场景 | 项目优先写法 | 避免作为新代码默认写法 |
|---|---|---|
| 容器注解 | `list[str]`、`dict[str, float]`、`tuple[float, float, float]` | `typing.List`、`typing.Dict`、`typing.Tuple` |
| 联合及可空值 | `float \| None`、`str \| Path` | `typing.Optional`、`typing.Union` |
| 类型别名 | `type Point3 = tuple[float, float, float]` | 仅为旧版本兼容引入 `TypeAlias` |
| 泛型函数和类 | `def first[T](...)`、`class Result[T]: ...` | 简单新接口仍采用独立 `TypeVar` 加 `Generic` 的旧式组合 |
| 抽象容器接口 | `collections.abc.Sequence`、`Mapping`、`Iterable` | 输入只需读取却强制要求具体 `list` 或 `dict` |
| 字符串格式化 | 普通文本用 f-string；日志用参数化格式 | 拼接大量 `str(...)`；日志先构造昂贵字符串 |
| 等长数据配对 | `zip(points, labels, strict=True)` | 静默截断长度不同的数据 |
| UTC 时间戳 | `from datetime import UTC, datetime` 后调用 `datetime.now(UTC)` | 返回无时区时间的 `datetime.utcnow()` |
| 不可变参数映射 | 3.15 内建 `frozendict`；结构化参数用 `@dataclass(frozen=True, slots=True)` | 为不可变映射额外安装第三方包 |
| 区分“未传参数”和 `None` | 3.15 内建 `sentinel()`，使用 `is` 比较 | 随意选 `-1`、空字符串或自制哨兵类 |
| 展平一层小批次 | 3.15 可写 `[*batch for batch in batches]` | `sum(batches, [])`；大数据仍优先迭代器，避免完整复制 |

`frozendict` 只冻结映射本身，内部可变值不会自动冻结；`frozen=True` 的 dataclass 同理。若 C++ 绑定只接受 `dict`，在边界显式 `dict(options)`，不能假定 pybind11 自动接受新类型。[类型标注](https://docs.python.org/3.15/library/typing.html)、[frozendict](https://docs.python.org/3.15/builtins/stdtypes.html#frozendict)

```python
from collections.abc import Sequence

type Point3 = tuple[float, float, float]


def first[T](items: Sequence[T]) -> T:
    if not items:
        raise ValueError("至少需要一个元素")
    return items[0]


MISSING = sentinel("MISSING")
DEFAULT_OPTIONS = frozendict(voxel_mm=0.5, tolerance_mm=0.01)


def describe_threshold(value: float | None | MISSING = MISSING) -> str:
    if value is MISSING:
        return "沿用宿主配置"
    if value is None:
        return "关闭可选阈值"
    return f"{value:g} mm"


batches = [(1, 2), (3,)]
flat_ids = [*batch for batch in batches]
```

新代码不机械添加 `from __future__ import annotations`。3.15 默认采用延迟求值注解；只用于静态检查的导入可放在 `if TYPE_CHECKING:` 中。运行时确需解析类型时使用 `typing.get_type_hints()` 或 `annotationlib`，明确处理前向引用；不要假定 `__annotations__` 全是字符串，也不要把注解求值当作无副作用操作。[注解机制](https://docs.python.org/3.15/library/annotationlib.html)

### 3.1 延迟导入

普通业务模块默认立即导入。可选且无注册副作用的大模块，经过启动耗时测量后，允许在模块顶层使用 3.15 的 `lazy import` 或 `lazy from ... import ...`：

```python
lazy import statistics


def summarize(values: list[float]) -> float:
    if not values:
        raise ValueError("不能汇总空数据")
    return statistics.fmean(values)
```

`lazy` 不能写在函数、类体或 `try/except/finally` 内，也不能用于星号导入或 future 导入。缺少模块的错误发生在首次使用时；不能在一条 lazy 导入外包 `try/except` 来实现可选依赖判断。将来宿主需要立即完成的注册、日志初始化和依赖自检应显式执行；需要动态立即导入时用 `importlib.import_module()`。脚本不得修改全局 lazy 模式或通过 lazy 绕过未交付依赖。[Python 3.15 延迟导入](https://docs.python.org/3.15/whatsnew/3.15.html#pep-810-explicit-lazy-imports)

## 4 业务入口与模型数据

会话、对象 ID、单位、容差、算法选项和随机种子必须显式传入。不读取“当前选中对象”作为隐藏输入；不通过 Python 指针、`id()` 或对象地址持久化模型引用。点云和 Shape 保留在 C++ 会话中，以稳定 ID 和小型参数传递，避免将百万点复制成嵌套 Python 列表。

下面是未来业务接口的形状示例；`CommandSession`、`cloud.voxel_filter` 和脚本宿主均未在当前程序中实现。命令名及参数以未来实际公开的 API 为准。它只定义函数，不会在导入时修改模型。

```python
import math
from typing import Protocol


class CommandSession(Protocol):
    def execute(self, name: str, params: dict[str, object]) -> dict[str, object]: ...


def run(
    session: CommandSession,
    *,
    cloud_id: str,
    voxel_mm: float,
) -> dict[str, object]:
    """请求体素滤波；尺寸单位为毫米，模型事务由宿主处理。"""
    if not cloud_id:
        raise ValueError("cloud_id 不能为空")
    if isinstance(voxel_mm, bool) or not isinstance(voxel_mm, (int, float)):
        raise TypeError("voxel_mm 必须是数值")
    if not math.isfinite(voxel_mm) or voxel_mm <= 0:
        raise ValueError("voxel_mm 必须是有限正数")
    return session.execute(
        "cloud.voxel_filter",
        {"cloud_id": cloud_id, "voxel_mm": float(voxel_mm)},
    )
```

未来的模型修改必须遵守宿主实际提供的提交、版本和取消协议；不要自行把异常吞掉后继续提交部分模型。当前 C++ 服务使用工程快照和版本检查，撤销功能尚未实现。Python 函数返回后，不能留下访问已关闭会话的回调、后台线程或裸内存视图。

## 5 数值与可重复结果

- 几何数值使用宿主约定的单位和容差；名称写明 `voxel_mm`、`angle_rad` 等。用 `math.isfinite()` 拒绝 NaN/Infinity，不能把 `bool` 当作合法尺寸。
- 几何浮点比较按算法约定使用绝对/相对容差，例如 `math.isclose(a, b, rel_tol=..., abs_tol=...)`；接近零时尤其需要绝对容差。不用小数显示位数代替几何容差。
- Decimal 从字符串或整数构造，不用 `Decimal(0.1)` 表达精确十进制 0.1。单次操作用 `localcontext()`，不在共享脚本模块中随意修改全局精度。
- 随机任务使用独立 `random.Random(seed)` 并记录种子；同时记录算法版本、单位、参数和输入摘要。并行归约次序可能造成浮点差异，跨平台几何比较使用容差，不要求全部浮点结果逐位相同。
- 对文件枚举和集合输出做显式排序；不要用 `set` 的遍历顺序或进程随机化的 `hash()` 生成持久 ID。持久内容摘要用 SHA-256。

```python
from decimal import Decimal, ROUND_HALF_EVEN, localcontext


def rounded_decimal(text: str) -> str:
    with localcontext() as context:
        context.prec = 34
        value = Decimal(text)
        if not value.is_finite():
            raise ValueError("输入必须是有限十进制数")
        return str(value.quantize(Decimal("0.001"), rounding=ROUND_HALF_EVEN))
```

## 6 文件路径与压缩格式

路径使用 `pathlib.Path`；输入目录、输出目录由宿主传入，不硬编码 `D:/...`、`/mnt/d/...`、反斜杠或可执行文件后缀。不改变进程工作目录，也不修改 `sys.path`、`PYTHONPATH`、DLL 搜索路径以借用开发机环境。安装目录视为只读，报告写入指定输出目录。

3.15 默认启用 UTF-8 模式，但项目文本合同仍显式指定 `encoding="utf-8"`；读已知旧编码时明确写实际编码，不用 `errors="ignore"` 静默丢数据。JSON 使用 `ensure_ascii=False, allow_nan=False`；标准 JSON 不保存 NaN/Infinity。CSV 用 `newline=""` 打开文件，保证双平台一致。[UTF-8 默认行为](https://docs.python.org/3.15/library/os.html#python-utf-8-mode)

可根据数据和接收方能力选择 GZIP、BZIP2、XZ/LZMA 或 Zstandard；需要与未知接收方交换时优先普通 JSON/CSV、GZIP 或 DEFLATE ZIP。读取外部 ZIP 时验证成员压缩方法、路径及大小；扩展名为 `.zip` 不保证接收方支持所有成员算法。不要直接把外部归档全部解压进工程目录；文件导入与覆盖由宿主管理。[ZIP 压缩后端](https://docs.python.org/3.15/library/zipfile.html)

```python
import hashlib
import json
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


def write_report(output_dir: Path, report: dict[str, object]) -> Path:
    """在指定目录创建压缩报告；同名文件已存在时失败，避免静默覆盖。"""
    payload = json.dumps(
        report, ensure_ascii=False, allow_nan=False, sort_keys=True, indent=2,
    ).encode("utf-8")
    output_dir.mkdir(parents=True, exist_ok=True)
    destination = output_dir / "report.zip"
    with ZipFile(destination, "x", compression=ZIP_DEFLATED) as archive:
        archive.writestr("report.json", payload)
        archive.writestr("report.sha256", hashlib.sha256(payload).hexdigest() + "\n")
    return destination
```

这个示例不提供事务式导出；正式模型导出由宿主实现临时文件、提交和失败清理。参数或工程文件不使用 `eval()`、`exec()`、`pickle.load()` 或 `marshal` 加载外来内容；JSON 解析后仍要校验字段、类型、范围和业务约束。

## 7 no GIL 与宿主调度

free-threaded 允许真正并行，但不自动保护模型、Qt 控件或跨多步的 Python 逻辑。`if key not in cache: cache[key] = ...`、读改写计数器和跨容器状态更新必须使用锁或由宿主串行化；内建容器的内部保护不等于业务事务。不要在多个线程间同时消费同一个迭代器。[free-threading 线程安全](https://docs.python.org/3.15/howto/free-threading-python.html#thread-safety)

- Qt 控件只能由宿主 GUI 线程更新。长任务通过宿主任务队列执行；不在 GUI 线程 `sleep()`、`input()` 或同步等待耗时算法。
- 脚本优先使用宿主线程池、进度与取消接口。确需自建 Python 线程池时限制线程数，使用上下文管理并在返回前等待结束；不得遗留 daemon 线程，不按“CPU 核数 × 点云任务数”无限扩张并行。
- 每个任务使用独立参数和工作状态，必要时以 `Lock`、`Event`、`Queue` 协调。取消必须合作式检查；不能通过强杀线程回滚 C++ 几何状态。
- `asyncio` 模块可用，但事件循环由宿主决定；宏不得自行调用 `asyncio.run()`、修改事件循环策略或安装 Qt 适配器。异步语法不保证 Qt 事件循环已经集成；TLS 客户端仍须按部署环境正确配置证书与主机名验证。
- `multiprocessing`、`ProcessPoolExecutor`、子解释器不是缺库而被统一禁用；它们涉及宿主入口、原生对象和解释器隔离，必须先有产品级运行方案。尤其不能假定 `sys.executable` 是可启动的 python.exe，它可能是 GUI 宿主。
- 原生扩展必须匹配产品 Python 版本、平台、架构及 free-threaded/Debug 合同。不能把普通 `cp315`、旧版本或系统 `.pyd/.so` 拷入后强制关闭 GIL；没有声明 free-threading 支持的扩展可能使 GIL 自动启用。

脚本禁止接管宿主生命周期：不调用 `sys.exit()`、`os._exit()`、解释器初始化/终止接口，不安装进程级信号处理器，不调用 `os.chdir()`，不清空 `sys.modules`。独立命令行诊断工具可以有退出码；产品宏用返回结果或异常交还宿主。

## 8 异常与日志

可预期的输入问题抛出具体异常，例如 `ValueError`、`TypeError`、`FileNotFoundError`；补充业务上下文时使用 `raise ... from exc`。不使用裸 `except:`、`except Exception: pass` 或 `return` 放在 `finally` 中吞掉错误；不以 `assert` 验证用户参数，因为优化运行可删除断言。

日志用 `logging.getLogger(__name__)`，处理器和输出目标由宿主配置。推荐 `logger.info("滤波完成，点数=%d", count)`；不要在每个脚本调用 `basicConfig(force=True)`、覆盖全局 stdout/stderr 或给根 logger 重复添加处理器。错误通常由脚本补充上下文、宿主统一记录，避免同一堆栈重复打印。

## 9 交付前检查

1. 使用产品自编译的 3.15t 解释器做语法与定向运行检查，不能只在用户安装版上测试。普通语法编译不会发现 lazy 首次使用错误，关键路径必须实际调用。
2. 检查导入清单与实际调用路径：确认所用标准模块及对应公共动态库随产品交付；无未交付的 Python 包、运行时自动安装或系统路径补救。涉及 TLS 时验证证书和主机名；涉及 SQLite 或压缩时验证实际读写。
3. 用中文、空格路径及只读安装目录验证读写；检查输入错误、空数据、NaN/Infinity、重复输出和取消行为。
4. 对使用压缩的流程验证实际成员内容；对 no GIL 流程在导入及首次使用原生模块后检查 `sys._is_gil_enabled()`。该检查属于宿主验收，不应靠业务宏强行切换 GIL。
5. 有并发时检查共享状态、Qt 线程归属、任务结束与回调清理；有模型修改时检查事务与失败后的状态。
6. 当前应用以 Windows / MSVC v143 预设为构建入口。其他平台与部署配置分别验证，不能根据 SDK 配方或另一平台结果推断应用已支持。未来的宿主脚本行为必须在实际产品宿主中测试，不能用独立解释器测试代替。

开发期格式化、静态类型检查等工具可安装在独立开发环境，目标版本设置为 Python 3.15；这些开发工具不自动成为产品运行依赖。单元测试可使用标准库 `unittest`，具体测试文件放置遵守项目规则。
