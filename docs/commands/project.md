# project — 工程与任务交付

由 `.rmdef` 生成，请修改声明后重新生成。

参数对可混排且顺序可调；位置参数必须保持相对顺序。`--` 之后均为位置值。
运行时输入 `help project` 可查询本模块，是否绑定业务以运行时帮助为准。

## project.undo

撤销上一步工程编辑

```text
project.undo
project.undo --help
```

统一撤销点云编辑、模型重建/删除和已确认基准面，最多 10 步；新导入建立历史边界。

| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |

返回：object — 恢复后的工程摘要；无历史返回错误。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
project.undo
```

## project.redo

重做已撤销的工程编辑

```text
project.redo
project.redo --help
```

恢复保存的几何结果；新工程编辑截断重做分支。

| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |

返回：object — 恢复后的工程摘要；无历史返回错误。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
project.redo
```

## project.reset

恢复最近一次导入状态

```text
project.reset
project.reset --help
```

恢复已加载的完整点云或 STEP/CWRM 模型，可撤销此次重置；不重读源文件，不停止录制，不回滚导出文件。

| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |

返回：object — 基线工程摘要；没有导入基线返回错误。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
project.reset
```

## project.snapshot

查询当前工程摘要

```text
project.snapshot
project.snapshot --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |

返回：object — 结果摘要；执行失败返回错误码。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
project.snapshot
```

```
snapshot = rm.execute("project.snapshot")
```

## project.set_output_dir

设置本次任务输出目录

```text
project.set_output_dir <path>
project.set_output_dir --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| path | 位置 1 | path |  | 必填 |  | 交付目录；同目录重复设置不重置结果，不允许切换到其他目录 |

返回：object — 输出目录；初始化 result.json 为失败状态。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
project.set_output_dir "D:/任务/001"
```

## project.export_and_exit

导出完整模型、提交结果并退出

```text
project.export_and_exit
project.export_and_exit --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |

返回：object — STEP 和结果 JSON；成功保存录制后退出并停止脚本后续内容。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
project.export_and_exit
```

## project.set_reference_plane

用三个工程坐标点设置基准面

```text
project.set_reference_plane --points <[[x,y,z],[x,y,z],[x,y,z]]>
project.set_reference_plane --help
```

三个非共线点，单位 mm；点序决定法线方向。用于正视和点云剖视，不变换原始坐标或重建几何。重新定面会关闭剖视。

| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| points | --points | array |  {'type': 'array', 'min_size': 3, 'max_size': 3, 'items': {'type': 'float'}} | 必填 |  | 三个工程坐标 XYZ（mm），不是显示局部坐标 |

返回：object — 包含基准面的工程摘要。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
project.set_reference_plane --points "[[0,0,0],[100,0,0],[0,100,0]]"
```

## project.clear_reference_plane

清除基准面并关闭点云剖视

```text
project.clear_reference_plane
project.clear_reference_plane --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |

返回：object — 工程摘要。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
project.clear_reference_plane
```

## project.section

设置基准面点云剖视

```text
project.section --enabled true|false [--offset <mm>] [--reverse true|false]
project.section --help
```

仅过滤点云显示，不删除点云，不影响框选、重建或导出。偏移沿原法线；默认保留正法线侧，reverse 仅反转保留侧。

| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| enabled | --enabled | bool |  | 必填 |  |  |
| offset | --offset | float |  | 默认 0.0 |  | 沿基准面法线的偏移，单位 mm |
| reverse | --reverse | bool |  | 默认 False |  | 反转点云保留侧 |

返回：object — 包含剖视状态的工程摘要。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
project.section --enabled true --offset 10 --reverse false
```

```
project.section --enabled false
```

