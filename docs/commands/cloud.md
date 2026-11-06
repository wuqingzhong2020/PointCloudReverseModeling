# cloud — 点云与文件操作

由 `.rmdef` 生成，请修改声明后重新生成。

参数对可混排且顺序可调；位置参数必须保持相对顺序。`--` 之后均为位置值。
运行时输入 `help cloud` 可查询本模块，是否绑定业务以运行时帮助为准。

## cloud.import

导入 PCD 点云或 STEP/STP/CWRM 模型

```text
cloud.import <path> [--unit mm|cm|m]
cloud.import --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| path | 位置 1 | path |  | 必填 |  | 输入文件路径（支持中文） |
| unit | --unit | enum | ['mm', 'cm', 'm'] | 默认 mm |  | 仅点云使用，默认毫米 |

返回：object — 结果摘要；执行失败返回错误码。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
cloud.import "D:/data/中文点云.pcd"
```

```
rm.execute("cloud.import", "D:/data/cloud.pcd", unit="mm")
```

## cloud.select

按视图矩形贯穿深度选择点云

```text
cloud.select --rectangle <object> [--mode replace|add|subtract|clear]
cloud.select --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| rectangle | --rectangle | object |  {'viewProjection': {'type': 'array', 'items': {'type': 'float'}, 'min_size': 16, 'max_size': 16}, 'minX': {'type': 'float'}, 'minY': {'type': 'float'}, 'maxX': {'type': 'float'}, 'maxY': {'type': 'float'}, 'zeroToOneDepth': {'type': 'bool'}} | 必填 |  | 视图投影矩阵和 NDC 矩形，供视图适配器使用 |
| mode | --mode | enum | ['replace', 'add', 'subtract', 'clear'] | 默认 replace |  |  |

返回：object — 结果摘要；执行失败返回错误码。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

## cloud.select_indices

按原始点索引选择点云

```text
cloud.select_indices --indices <array> [--mode replace|add|subtract|clear]
cloud.select_indices --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| indices | --indices | array |  {'type': 'uint'} | 必填 |  | 当前点云中的零起始索引 |
| mode | --mode | enum | ['replace', 'add', 'subtract', 'clear'] | 默认 replace |  |  |

返回：object — 结果摘要；执行失败返回错误码。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
rm.execute("cloud.select_indices", indices=[0, 1, 2])
```

## cloud.crop

保留选中点云

```text
cloud.crop [--regions <array>] [--indices <array>]
cloud.crop --help
```

传入regions或indices即可独立重放选区并编辑，失败不改变当前选区或点云；均省略时兼容旧命令，使用当前选区。需使用相同输入点云和前序编辑结果。

| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| regions | --regions | array |  {'type': 'object', 'properties': {'rectangle': {'type': 'object', 'properties': {'viewProjection': {'type': 'array', 'items': {'type': 'float'}, 'min_size': 16, 'max_size': 16}, 'minX': {'type': 'float', 'ranges': [[-1, 1]]}, 'minY': {'type': 'float', 'ranges': [[-1, 1]]}, 'maxX': {'type': 'float', 'ranges': [[-1, 1]]}, 'maxY': {'type': 'float', 'ranges': [[-1, 1]]}, 'zeroToOneDepth': {'type': 'bool'}}}, 'mode': {'type': 'enum', 'values': ['replace', 'add', 'subtract']}}} | 可选 |  | 完整框选序列。矩形位置为(minX,minY)，宽高为(maxX-minX,maxY-minY)，坐标为NDC；viewProjection保存当时投影。按顺序替换/增选/减选，不读取当前视口。 |
| indices | --indices | array |  {'type': 'uint'} | 可选 |  | 框选序列之前的初始选区，零起始点索引；未传时从空选区开始。 |

返回：object — 结果摘要；执行失败返回错误码。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
cloud.crop --indices "[0,1,2]"
```

```
rm.execute("cloud.crop", regions=[{'rectangle': {'viewProjection': [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], 'minX': -0.5, 'minY': -0.5, 'maxX': 0.5, 'maxY': 0.5, 'zeroToOneDepth': False}, 'mode': 'replace'}])
```

## cloud.delete

删除选中点云

```text
cloud.delete [--regions <array>] [--indices <array>]
cloud.delete --help
```

传入regions或indices即可独立重放选区并编辑，失败不改变当前选区或点云；均省略时兼容旧命令，使用当前选区。需使用相同输入点云和前序编辑结果。

| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| regions | --regions | array |  {'type': 'object', 'properties': {'rectangle': {'type': 'object', 'properties': {'viewProjection': {'type': 'array', 'items': {'type': 'float'}, 'min_size': 16, 'max_size': 16}, 'minX': {'type': 'float', 'ranges': [[-1, 1]]}, 'minY': {'type': 'float', 'ranges': [[-1, 1]]}, 'maxX': {'type': 'float', 'ranges': [[-1, 1]]}, 'maxY': {'type': 'float', 'ranges': [[-1, 1]]}, 'zeroToOneDepth': {'type': 'bool'}}}, 'mode': {'type': 'enum', 'values': ['replace', 'add', 'subtract']}}} | 可选 |  | 完整框选序列。矩形位置为(minX,minY)，宽高为(maxX-minX,maxY-minY)，坐标为NDC；viewProjection保存当时投影。按顺序替换/增选/减选，不读取当前视口。 |
| indices | --indices | array |  {'type': 'uint'} | 可选 |  | 框选序列之前的初始选区，零起始点索引；未传时从空选区开始。 |

返回：object — 结果摘要；执行失败返回错误码。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
cloud.delete --indices "[0,1,2]"
```

```
rm.execute("cloud.delete", regions=[{'rectangle': {'viewProjection': [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], 'minX': -0.5, 'minY': -0.5, 'maxX': 0.5, 'maxY': 0.5, 'zeroToOneDepth': False}, 'mode': 'replace'}])
```

## cloud.undo

撤销工程编辑（旧入口）

```text
cloud.undo
cloud.undo --help
```

project.undo 的旧入口；现在作用于统一工程历史，新录制写入 project.undo。

| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |

返回：object — 结果摘要；执行失败返回错误码。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

## cloud.redo

重做工程编辑（旧入口）

```text
cloud.redo
cloud.redo --help
```

project.redo 的旧入口；现在作用于统一工程历史，新录制写入 project.redo。

| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |

返回：object — 结果摘要；执行失败返回错误码。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

## cloud.reset

恢复导入状态（旧入口）

```text
cloud.reset
cloud.reset --help
```

project.reset 的旧入口；现在作用于统一工程历史，新录制写入 project.reset。

| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |

返回：object — 结果摘要；执行失败返回错误码。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

## cloud.export

导出当前点云为 PCD

```text
cloud.export <path>
cloud.export --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| path | 位置 1 | path |  | 必填 |  |  |

返回：object — 结果摘要；执行失败返回错误码。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
cloud.export "D:/data/result.pcd"
```

```
rm.execute("cloud.export", "D:/data/result.pcd")
```

