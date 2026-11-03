# model — 模型重建和导出

由 `.rmdef` 生成，请修改声明后重新生成。

参数对可混排且顺序可调；位置参数必须保持相对顺序。`--` 之后均为位置值。
运行时输入 `help model` 可查询本模块，是否绑定业务以运行时帮助为准。

## model.reconstruct

按点云识别板厚重建

```text
model.reconstruct [--mirror false] [--end-tolerance 30]
model.reconstruct --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| mirror | --mirror | bool |  | 默认 False |  | 是否补建镜像侧 |
| end_tolerance | --end-tolerance | float | [[0, 100000]] | 默认 30.0 | mm | 端部突出容差 |

返回：object — 结果摘要；执行失败返回错误码。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
model.reconstruct --mirror false --end-tolerance 30
```

```
rm.execute("model.reconstruct", mirror=False)
```

## model.parametric

按显式参数构建梁体

```text
model.parametric --parameters <object> [--frame <object>]
model.parametric --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| parameters | --parameters | object |  {'length': {'type': 'float'}, 'width': {'type': 'float'}, 'height': {'type': 'float'}, 'webThickness': {'type': 'float'}, 'flangeThickness': {'type': 'float'}, 'ribThickness': {'type': 'float'}, 'ribStations': {'type': 'array', 'items': {'type': 'float'}}, 'swapCrossSection': {'type': 'bool'}, 'mirrorSymmetry': {'type': 'bool'}} | 必填 |  | 梁尺寸及板厚，单位 mm |
| frame | --frame | object |  {'originMm': {'type': 'array', 'items': {'type': 'float'}, 'min_size': 3, 'max_size': 3}, 'xAxis': {'type': 'array', 'items': {'type': 'float'}, 'min_size': 3, 'max_size': 3}, 'yAxis': {'type': 'array', 'items': {'type': 'float'}, 'min_size': 3, 'max_size': 3}, 'zAxis': {'type': 'array', 'items': {'type': 'float'}, 'min_size': 3, 'max_size': 3}} | 可选 |  | 未传入时采用点云识别框架 |

返回：object — 结果摘要；执行失败返回错误码。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

## model.export_step

导出模型为 STEP

```text
model.export_step <path>
model.export_step --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| path | 位置 1 | path |  | 必填 |  |  |

返回：object — 结果摘要；执行失败返回错误码。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
model.export_step "D:/data/result.step"
```

```
rm.execute("model.export_step", "D:/data/result.step")
```

## model.deviation

计算点云与当前模型的偏差

```text
model.deviation
model.deviation --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |

返回：object — 结果摘要；执行失败返回错误码。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
model.deviation
```

```
rm.execute("model.deviation")
```

