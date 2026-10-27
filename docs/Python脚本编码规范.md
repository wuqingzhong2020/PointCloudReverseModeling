# 逆向建模软件嵌入式 Python 脚本编码规范

更新日期：2026-10-26。适用运行时：LibPackByVcpkgWindows 的 `python3` overlay 3.15.0 free-threaded；当前清单外部后端为 libffi 3.8.0、mpdecimal 4.0.1、zlib 1.3.2#2，按公共动态配置消费。

Python 用于宏、命令编排、参数校验和结果报告；点云、曲面、拓扑与模型修改通过宿主提供的业务接口执行。缺少某些 C/C++ 后端限制的是相应标准库功能，不限制 Python 3.15 的语言语法，也不表示只能使用三个 Python 模块。

本规范中的“必须、禁止”是项目脚本约束，“当前不可用”是当前 SDK 的能力边界。宿主 `rm` 接口仍按设计文档逐步实现；下文接口示例不表示对应业务命令已经可调用。构建依据见 [CPython 介绍与编译探索](../reference/CPython介绍与编译探索.md)、[当前 vcpkg port](../../../../LibPackByVcpkgWindows/ports/python3/portfile.cmake)；绑定与业务合同见 [pybind11 文档](../reference/pybind11介绍与编译探索.md)、[Python 嵌入与命令化回归测试方案](../reference/Python嵌入与命令化回归测试方案.md)。

## 1 运行环境与依赖边界

下表表示当前 port 选择的功能合同；新安装包须分别完成导入和产品宿主验收，本轮文档核对不等于运行验证。完整 SDK 中的 OpenSSL、SQLite、独立 Tcl/Tk 不会自动给裁剪的 CPython 增加对应模块。

脚本只依赖产品交付的标准库、自有脚本包和已公布的宿主接口。安装在开发机上的 Python、pip 包及 PATH 中的 DLL，不属于产品运行环境。开发、检查和运行均以自编译的 3.15t SDK 为基准；普通安装版只作对照。

| 能力 | 当前状态 | 编写规则 |
|---|---|---|
| `decimal`、`_decimal` | mpdecimal 4.0.1 后端已选 | 十进制参数、计价及十进制舍入可使用；不替代几何内核的浮点数与容差 |
| `ctypes` | libffi 3.8.0 后端已选 | 业务脚本通过宿主 API 操作模型；不自行用 ctypes 加载 PCL、OCCT、Qt 或操纵宿主指针 |
| `zlib`、`gzip`、`zipfile`、`tarfile` | zlib 1.3.2 动态后端已选 | 支持 DEFLATE ZIP、GZIP、`.tar.gz`、压缩 ZIP 导入；ZIP 明确指定压缩方法 |
| `binascii`、`base64` | 可用 | 用于编码与校验；Base64 不是压缩，CRC32 不是安全签名 |
| `json`、`csv`、`tomllib`、`pathlib`、`dataclasses`、`typing`、`annotationlib` | 可用 | 参数与小型报告优先使用这些标准库；`tomllib` 只负责读取 TOML |
| `math`、`statistics`、`array`、`memoryview`、`struct` | 可用 | 可处理小型数值与二进制数据；大点云及几何批运算交给 C++ |
| `hashlib`、`hmac`、`secrets` | 基础功能可用 | 文件摘要优先 SHA-256；不要假定 OpenSSL 扩展算法存在 |
| `threading`、`queue`、`concurrent.futures`、`asyncio` | 模块可用 | 调度、取消、线程数量和事件循环必须服从宿主约定 |
| `xml.etree.ElementTree`、`pyexpat`、`uuid` | 可用 | 无须为了普通 XML 或 UUID 再加入第三方 Python 包 |

三个外部 C 库不是 pip 包：脚本通常导入 `ctypes`、`decimal`、`zlib`，不写 `import libffi` 或 `import mpdecimal`。动态后端由产品部署并验证，脚本仍使用 `import zlib` 等标准接口；缺失后端 DLL/SO 属于部署问题，不能从其他 Python 安装目录补取。

### 1.1 当前不可依赖的功能

| 避免写入交付脚本的内容 | 原因与替代方式 |
|---|---|
| `import ssl`，以及要求 TLS 的 HTTPS、WSS、SMTP_SSL 等流程 | 未编入 `_ssl`。需要网络下载或认证时交给宿主已提供的网络接口；不能用关闭证书验证或降级明文 HTTP 代替 TLS |
| `hashlib.pbkdf2_hmac()`、`hashlib.scrypt()`、假定 `hashlib.new()` 支持任意算法 | 未编入 OpenSSL `_hashlib`，当前两项函数不存在。普通文件校验可用 `hashlib.sha256()`；密码派生交给批准的宿主服务 |
| `sqlite3.connect()`，或依赖 SQLite 的 ORM/缓存 | 未编入 `_sqlite3`。小参数与报告用 JSON/CSV；模型持久化通过宿主工程文件接口 |
| `bz2`、`lzma`、`compression.zstd` | `_bz2`、`_lzma`、`_zstd` 未编入。不要生成或读取 `.bz2`、`.xz`、`.zst`，也不要选 ZIP_BZIP2、ZIP_LZMA、ZIP_ZSTANDARD |
| `tarfile.open(..., "w:bz2")`、`"r:xz"`、`"w:zst"` 等 | 对应后端缺失。统一选择普通 TAR 或 GZIP TAR |
| `tkinter`、`turtle` 图形窗口 | 未交付 Tk 后端。GUI 由 Qt 宿主提供，不能因宿主使用 C++ Qt 就假定存在 PySide/PyQt |
| 依赖 `readline`、`curses`、GNU DBM 等的平台脚本 | 不属于两平台共同功能合同。使用宿主控制台、日志及存储接口 |
| `numpy`、`scipy`、`pandas`、`requests`、`lxml`、PySide/PyQt 等未经纳入发布清单的包 | 当前没有交付这些 Python 包；安装了 C++ Eigen/PCL/Qt 不等于安装了对应 Python 包 |
| 脚本内 `pip install`、启动 `ensurepip`、创建 venv 后自动装依赖 | 当前不交付 pip 引导流程；zlib 可解压 wheel 不等于具备完整包管理和 HTTPS 能力。依赖由产品构建与发布管理 |

没有 `_ssl` 不会取消普通 `socket`，没有 `_hashlib` 也不会取消所有散列算法。`dbm.dumb` 是可用的纯 Python 后端，但不把 `dbm.open()` 自动选择的文件格式或 `shelve` 作为跨平台工程文件格式。`zoneinfo.ZoneInfo("Asia/Shanghai")` 另依赖时区数据库，Windows 发布包不能默认拥有；不需要地区规则时用 `datetime.UTC`。这些是具体依赖条件，不扩大成“所有网络、散列、时间和存储功能都不可用”。[hashlib](https://docs.python.org/3.15/library/hashlib.html)、[zoneinfo 数据来源](https://docs.python.org/3.15/library/zoneinfo.html#data-sources)

## 2 文件组织与基础风格

- 使用 UTF-8 源文件、4 个空格缩进；不混入 Tab。BOM 等文件编码细节遵守所在仓库规则。代码行建议不超过 100 个字符。
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

`lazy` 不能写在函数、类体或 `try/except/finally` 内，也不能用于星号导入或 future 导入。缺少模块的错误发生在首次使用时；不能在一条 lazy 导入外包 `try/except` 来实现可选依赖判断。必须立即完成的 `rm` 注册、日志初始化、依赖自检由宿主显式执行；需要动态立即导入时用 `importlib.import_module()`。脚本不得修改全局 lazy 模式或通过 lazy 绕过未交付依赖。[Python 3.15 延迟导入](https://docs.python.org/3.15/whatsnew/3.15.html#pep-810-explicit-lazy-imports)

## 4 业务入口与模型数据

会话、对象 ID、单位、容差、算法选项和随机种子必须显式传入。不读取“当前选中对象”作为隐藏输入；不通过 Python 指针、`id()` 或对象地址持久化模型引用。点云和 Shape 保留在 C++ 会话中，以稳定 ID 和小型参数传递，避免将百万点复制成嵌套 Python 列表。

下面是未来业务接口的形状示例；命令名及参数最终以宿主已公布 API 为准。它只定义函数，不会在导入时修改模型。

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

模型修改必须遵守宿主事务、撤销和取消协议；不要自行把异常吞掉后继续提交部分模型。Python 函数返回后，不能留下访问已关闭会话的回调、后台线程或裸内存视图。

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

跨平台交换优先选择普通 JSON/CSV、GZIP 或 DEFLATE ZIP。读取外部 ZIP 时需要验证成员压缩方法、路径及大小；扩展名为 `.zip` 不保证所有成员均能解压。不要直接把外部归档全部解压进工程目录；文件导入与覆盖由宿主管理。[ZIP 压缩后端](https://docs.python.org/3.15/library/zipfile.html)

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
- `asyncio` 模块可用，但事件循环由宿主决定；宏不得自行调用 `asyncio.run()`、修改事件循环策略或安装 Qt 适配器。异步语法不保证 Qt 事件循环已经集成，TLS 异步连接仍不可用。
- `multiprocessing`、`ProcessPoolExecutor`、子解释器不是缺库而被统一禁用；它们涉及宿主入口、原生对象和解释器隔离，必须先有产品级运行方案。尤其不能假定 `sys.executable` 是可启动的 python.exe，它可能是 GUI 宿主。
- 原生扩展必须匹配产品 Python 版本、平台、架构及 free-threaded/Debug 合同。不能把普通 `cp315`、旧版本或系统 `.pyd/.so` 拷入后强制关闭 GIL；没有声明 free-threading 支持的扩展可能使 GIL 自动启用。

脚本禁止接管宿主生命周期：不调用 `sys.exit()`、`os._exit()`、解释器初始化/终止接口，不安装进程级信号处理器，不调用 `os.chdir()`，不清空 `sys.modules`。独立命令行诊断工具可以有退出码；产品宏用返回结果或异常交还宿主。

## 8 异常与日志

可预期的输入问题抛出具体异常，例如 `ValueError`、`TypeError`、`FileNotFoundError`；补充业务上下文时使用 `raise ... from exc`。不使用裸 `except:`、`except Exception: pass` 或 `return` 放在 `finally` 中吞掉错误；不以 `assert` 验证用户参数，因为优化运行可删除断言。

日志用 `logging.getLogger(__name__)`，处理器和输出目标由宿主配置。推荐 `logger.info("滤波完成，点数=%d", count)`；不要在每个脚本调用 `basicConfig(force=True)`、覆盖全局 stdout/stderr 或给根 logger 重复添加处理器。错误通常由脚本补充上下文、宿主统一记录，避免同一堆栈重复打印。

## 9 交付前检查

1. 使用产品自编译的 3.15t 解释器做语法与定向运行检查，不能只在用户安装版上测试。普通语法编译不会发现 lazy 首次使用错误，关键路径必须实际调用。
2. 检查导入清单和实际调用路径：无未交付 Python 包、SSL/SQLite/BZIP2/XZ/Zstandard 依赖，无运行时自动安装或系统路径补救。
3. 用中文、空格路径及只读安装目录验证读写；检查输入错误、空数据、NaN/Infinity、重复输出和取消行为。
4. 对使用压缩的流程验证实际成员内容；对 no GIL 流程在导入及首次使用原生模块后检查 `sys._is_gil_enabled()`。该检查属于宿主验收，不应靠业务宏强行切换 GIL。
5. 有并发时检查共享状态、Qt 线程归属、任务结束与回调清理；有模型修改时检查事务与失败后的状态。
6. 当前跨平台基线为 Windows vc143 与 Ubuntu 26.04/GCC 16.2.0/Qt6；Linux 应用及脚本行为需在该组合下验收。宿主 `rm` 行为应在实际产品宿主中测试，不能用独立解释器测试代替。

开发期格式化、静态类型检查等工具可安装在独立开发环境，目标版本设置为 Python 3.15；这些开发工具不自动成为产品运行依赖。单元测试可使用标准库 `unittest`，具体测试文件放置遵守项目规则。
