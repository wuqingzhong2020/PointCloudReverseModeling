# project — 工程与任务交付

由 `.rmdef` 生成，请修改声明后重新生成。

参数对可混排且顺序可调；位置参数必须保持相对顺序。`--` 之后均为位置值。
运行时输入 `help project` 可查询本模块，是否绑定业务以运行时帮助为准。

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

