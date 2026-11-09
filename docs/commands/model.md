# model — 模型重建和导出

由 `.rmdef` 生成，请修改声明后重新生成。

参数对可混排且顺序可调；位置参数必须保持相对顺序。`--` 之后均为位置值。
运行时输入 `help model` 可查询本模块，是否绑定业务以运行时帮助为准。

## model.reconstruct_local

局部重建所选部件或生成占用空间包围盒，保留未选部件

```text
model.reconstruct_local --parts <["部件ID",...]> [--type plate|horizontal|box] [--contour rectangle|circle|chamfer_rectangle|quarter_hole_rectangle|rounded_rectangle|convex|polygon|smooth] [--up <[X,Y,Z]>] [--tolerance 2] [--corner 5] [--mirror-bottom true]
model.reconstruct_local --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| parts | --parts | array |  {'type': 'string', 'min_size': 1} | 必填 |  | 所选模型部件 ID；平板规则要求原始点云关联，包围盒不要求。整批成功才替换，未选部件保持不变；选中部件的手工修改会被替代。 |
| type | --type | enum | ['plate', 'horizontal', 'box'] | 默认 plate |  | plate 保持原板面方向及板厚；horizontal 采用指定水平法向；box 按当前模型占用范围生成盒状障碍物，沿当前部件坐标系，不要求点云关联；轮廓参数不生效。 |
| contour | --contour | enum | ['rectangle', 'circle', 'chamfer_rectangle', 'quarter_hole_rectangle', 'rounded_rectangle', 'convex', 'polygon', 'smooth'] | 默认 rectangle |  | 矩形、包络圆、倒角矩形、1/4孔角矩形、圆角矩形、凸包、凹多边形、连续曲线；包围盒忽略轮廓。仅拟合外轮廓，不重新识别内孔。 |
| up | --up | array |  {'type': 'float'} | 默认 [0.0, 0.0, 1.0] |  | 水平板法向，GUI 有基准面时明确记录其法向；其他类型忽略。 |
| tolerance | --tolerance | float | [[0.1, 1000.0]] | 默认 2.0 | mm | 任意多边形和连续曲线的轮廓采样/简化容差，过稀或分离的点云明确报错。 |
| corner | --corner | float | [[0.001, 100000.0]] | 默认 5.0 | mm | 倒角矩形的角部退距，或 1/4孔角矩形/圆角矩形的半径；必须小于较短边的一半。 |
| mirror_bottom | --mirror-bottom | bool |  | 默认 True |  | 仅对原板面方向的竖直板及三种角部矩形生效：按原板底边补齐下部并镜像上角。关闭时按可见点范围重建，仅处理上角；水平板及其他轮廓忽略。 |

返回：object — 局部重建后的工程摘要，支持撤销、重做和脚本录制。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
model.reconstruct_local --parts '["web"]' --type plate --contour rectangle
```

## model.delete

删除指定模型部件

```text
model.delete --parts <["部件ID",...]>
model.delete --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| parts | --parts | array |  {'type': 'string', 'min_size': 1} | 必填 |  | 当前模型中要删除的部件 ID；同步更新实际几何和 STEP 导出，不删除点云。未知 ID 整条拒绝。 |

返回：object — 删除后的工程摘要；全部删除后无模型。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
model.delete --parts '["imported-part-1","imported-part-3"]'
```

## model.reconstruct

按点云识别板厚重建

```text
model.reconstruct [--type hbeam|tbar|other] [--mirror false] [--end-tolerance 30] [--web-holes true] [--min-hole-radius 5]
model.reconstruct --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| type | --type | enum | ['hbeam', 'tbar', 'other'] | 默认 hbeam |  | 重建工件类型；other 为俯视底板焊件，按直线/圆弧顶边垂直投影恢复连接，保留独立障碍物 |
| on_ground | --on-ground | bool |  | 默认 True |  | T 排置于地面；关闭表示台架 |
| regular_upper_surface | --regular-upper-surface | bool |  | 默认 True |  | T 排规则顶边；异形直 T 关闭 |
| projection_onto_border | --projection-onto-border | bool |  | 默认 False |  | T 排端边投影到底板边界 |
| perpendicular | --perpendicular | bool |  | 默认 True |  | T 排立板与底板垂直约束；斜 T 关闭 |
| max_slot_width | --max-slot-width | float | [[0, 1000]] | 默认 50.0 | mm | T 排顶边无支持断带的最大连接宽度 |
| max_base_width | --max-base-width | float | [[20, 1000]] | 默认 250.0 | mm | T 排底板识别宽度上限 |
| mirror | --mirror | bool |  | 默认 False |  | 是否补建镜像侧 |
| end_tolerance | --end-tolerance | float | [[0, 100000]] | 默认 30.0 | mm | 端部突出容差 |
| web_holes | --web-holes | bool |  | 默认 True |  | 保留俯视可见的腹板圆孔和长圆孔；关闭时生成无内孔腹板，不影响筋板可见顶边缺口。 |
| min_hole_radius | --min-hole-radius | float | [[0, 1000]] | 默认 5.0 | mm | 忽略小于此半径的腹板孔。长圆孔按端圆半径（孔宽一半）过滤；0 仍受点云分辨率限制。 |

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

## model.move

移动模型部件

```text
model.move --parts <["部件ID",...]> --offset <[X,Y,Z]>
model.move --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| parts | --parts | array |  {'type': 'string', 'min_size': 1} | 必填 |  | 当前模型中部件 ID；多个部件一起变换，未知 ID 整条拒绝 |
| offset | --offset | array |  {'type': 'float'} | 必填 |  | 项目坐标 X/Y/Z 平移量，单位 mm |

返回：object — 更新后的工程摘要；可撤销和录制。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
model.move --parts '["imported-part-1"]' --offset '[-100,0,0]'
```

## model.rotate

按指定轴旋转模型部件

```text
model.rotate --parts <["部件ID",...]> --axis x|y|z --angle <度> --center <[X,Y,Z]>
model.rotate --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| parts | --parts | array |  {'type': 'string', 'min_size': 1} | 必填 |  | 当前模型中部件 ID；多个部件一起变换，未知 ID 整条拒绝 |
| axis | --axis | enum | ['x', 'y', 'z'] | 必填 |  | 项目坐标旋转轴；一次仅绕一个轴，遵循右手规则 |
| angle | --angle | float | [[-180, 180]] | 必填 |  | 有符号角度；从轴正向朝原点看，正值逆时针 |
| center | --center | array |  {'type': 'float'} | 必填 |  | 公共旋转中心，项目坐标 mm；必须明确填写以便重放 |

返回：object — 更新后的工程摘要；可撤销和录制。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
model.rotate --parts '["imported-part-1"]' --axis z --angle -60 --center '[0,0,0]'
```

## model.fillet

对板件轮廓应用圆角，保持板厚

```text
model.fillet (--part <部件ID> --edges <[边编号,...]> | --targets <分组数组>) --size <毫米>
model.fillet --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| part | --part | string |  | 可选 |  | 模型部件 ID，可通过 project.snapshot 查询 |
| edges | --edges | array |  {'type': 'int', 'ranges': [[1, 2147483647]]} | 可选 |  | 当前部件 BRep 选边编号（从 1 开始）；轮廓边处理两端板角，贯穿板厚短边处理一个角 |
| targets | --targets | array |  {'type': 'object', 'properties': {'part': {'type': 'string', 'min_size': 1}, 'edges': {'type': 'array', 'min_size': 1, 'items': {'type': 'int', 'ranges': [[1, 2147483647]]}}}} | 可选 |  | 多部件边线分组；每组含 part 和 edges，不可与 --part/--edges 同用；整批成功才提交 |
| size | --size | float |  | 必填 |  | 轮廓半径，mm，必须大于 0；受轮廓空间限制，不以板厚为上限；命令直接应用 |

返回：object — 更新后的工程摘要；可撤销、录制和回放。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
model.fillet --part web --edges '[1]' --size 1
```

```
model.fillet --targets '[{"part":"web","edges":[1]},{"part":"flange-bottom","edges":[1]}]' --size 1
```

## model.chamfer

对板件轮廓应用等距倒角，保持板厚

```text
model.chamfer (--part <部件ID> --edges <[边编号,...]> | --targets <分组数组>) --size <毫米>
model.chamfer --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| part | --part | string |  | 可选 |  | 模型部件 ID，可通过 project.snapshot 查询 |
| edges | --edges | array |  {'type': 'int', 'ranges': [[1, 2147483647]]} | 可选 |  | 当前部件 BRep 选边编号（从 1 开始）；轮廓边处理两端板角，贯穿板厚短边处理一个角 |
| targets | --targets | array |  {'type': 'object', 'properties': {'part': {'type': 'string', 'min_size': 1}, 'edges': {'type': 'array', 'min_size': 1, 'items': {'type': 'int', 'ranges': [[1, 2147483647]]}}}} | 可选 |  | 多部件边线分组；每组含 part 和 edges，不可与 --part/--edges 同用；整批成功才提交 |
| size | --size | float |  | 必填 |  | 轮廓两侧距离，mm，必须大于 0；保持板厚；命令直接应用 |

返回：object — 更新后的工程摘要；可撤销、录制和回放。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
model.chamfer --part web --edges '[1]' --size 1
```

```
model.chamfer --targets '[{"part":"web","edges":[1]},{"part":"flange-bottom","edges":[1]}]' --size 1
```

## model.add_hole

在等厚板件底边添加过焊孔或底边孔

```text
model.add_hole --part <部件ID> --edge <底边编号> --kind weld|bottom --shape circle|triangle|rectangle [--size 20] [--width 30 --height 15] [--reference start|middle|end --offset 0] [--first true --second true]
model.add_hole --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| part | --part | string |  | 必填 |  | 一个等厚实体板件的 ID；可通过 project.snapshot 查询 |
| edge | --edge | int | [[1, 2147483647]] | 必填 |  | 当前部件主面直线外轮廓边的 BRep 编号，作为底边；界面录制会填写，重放不依赖视角 |
| kind | --kind | enum | ['weld', 'bottom'] | 必填 |  | weld：底角过焊孔；bottom：沿底边定位开孔 |
| shape | --shape | enum | ['circle', 'triangle', 'rectangle'] | 默认 circle |  | 过焊孔支持 circle/triangle；底边孔支持 circle/rectangle |
| size | --size | float |  | 默认 20.0 |  | 圆孔半径或三角形两直角边长度，mm，必须大于 0 |
| width | --width | float |  | 默认 30.0 |  | 矩形总宽，mm，孔中心两侧各一半 |
| height | --height | float |  | 默认 15.0 |  | 矩形从底边切入板内的高度，mm |
| reference | --reference | enum | ['start', 'middle', 'end'] | 默认 middle |  | 底边端点 1 / 中点 / 端点 2；端点编号对应界面 1、2 标记 |
| offset | --offset | float |  | 默认 0.0 |  | 底边孔偏移 mm；从端点向内为正，从中点朝端点 2 为正 |
| first | --first | bool |  | 默认 True |  | 是否切除端点 1 的过焊孔 |
| second | --second | bool |  | 默认 True |  | 是否切除端点 2 的过焊孔 |

返回：object — 更新后的工程摘要；保持板厚，支持撤销/重做、录制及 STEP 导出。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
model.add_hole --part imported-part-4 --edge 4 --kind weld --shape circle --size 20
```

```
model.add_hole --part imported-part-4 --edge 4 --kind bottom --shape rectangle --width 30 --height 15 --reference middle --offset 0
```

## model.set_thickness

修改等厚板件的真实板厚，保持中面和轮廓

```text
model.set_thickness --parts <["部件ID",...]> --thickness <毫米>
model.set_thickness --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| parts | --parts | array |  {'type': 'string', 'min_size': 1} | 必填 |  | 选中的等厚实体板件 ID；支持多个部件，整批成功才提交 |
| thickness | --thickness | float | [[0.001, 1000000]] | 必填 | mm | 目标板厚；中面固定、两侧等量增减，保留轮廓及贯穿孔；不支持变厚板/曲面板 |

返回：object — 更新后的工程摘要；支持撤销、重做、录制、回放和 STEP 导出。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
model.set_thickness --parts '["imported-part-5"]' --thickness 20
```

## model.expand_contour

外扩等厚板件外轮廓，保持板厚及封闭内孔

```text
model.expand_contour --parts <["部件ID",...]> --distance <毫米>
model.expand_contour --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| parts | --parts | array |  {'type': 'string', 'min_size': 1} | 必填 |  | 等厚实体板件 ID；支持多选，整批成功才提交 |
| distance | --distance | float | [[0.001, 1000000]] | 必填 | mm | 各外边界在板面内向外偏移的距离；直线角延伸相交，封闭内孔不变，开放缺口随外轮廓偏移；再次应用为累加 |

返回：object — 更新后的工程摘要；支持撤销、重做、录制、回放和 STEP 导出。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
model.expand_contour --parts '["imported-part-1"]' --distance 10
```

## model.generate_cover_plate

生成外扩贴板，保留源板并新增独立部件

```text
model.generate_cover_plate --part <部件ID> --face <主面编号> [--reverse false] [--offset 30 --thickness 10]
model.generate_cover_plate --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| part | --part | string |  | 必填 |  | 一个有效的等厚实体板件；曲面、变厚板和观测面不适用 |
| offset | --offset | float | [[-100, 100]] | 默认 30.0 | mm | 平面外轮廓等距外扩；新板不继承源板封闭内孔 |
| thickness | --thickness | float | [[1, 100]] | 默认 10.0 | mm | 新增板件厚度，不修改源板厚度 |
| face | --face | int | [[1, 2147483647]] | 必填 |  | 等厚实体板件的主面 BRep 编号（1-based），不能选薄侧面或孔壁 |
| reverse | --reverse | bool |  | 默认 False |  | 沿所选面的内法线拉伸，可与原板重叠；默认向外 |

返回：object — 含新增部件 ID 的工程摘要；支持撤销、重做、录制和 STEP 导出。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
model.generate_cover_plate --part imported-part-1 --face 3 --offset 30 --thickness 10
```

## model.generate_base_plate

生成下底板，保留源板并新增独立部件

```text
model.generate_base_plate --mode horizontal --part <水平板ID> [--offset 20 --thickness 10]
model.generate_base_plate --mode vertical_pair --part <竖板ID1> --second-part <竖板ID2> [--offset 20 --thickness 10]
model.generate_base_plate --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| part | --part | string |  | 必填 |  | 第一块等厚实体板件 ID；方向偏差允许 ±5°，源板不移动 |
| mode | --mode | enum | ['horizontal', 'vertical_pair'] | 默认 horizontal |  | horizontal：一块水平板的底面轮廓；vertical_pair：两块相对平行的竖直板底部外包络，均只生成一块底板 |
| second_part | --second-part | string |  | 默认  |  | vertical_pair 必须指定不同的第二块板 ID；两板底部需横向分开、沿长度重叠且为连续直边；horizontal 不允许指定 |
| offset | --offset | float | [[4, 1000]] | 默认 20.0 | mm | 平面外轮廓等距外扩；新板不继承源板封闭内孔 |
| thickness | --thickness | float | [[4, 50]] | 默认 10.0 | mm | 新增板件厚度，不修改源板厚度 |
| up | --up | array |  {'type': 'float'} | 默认 [0.0, 0.0, 1.0] |  | 固定世界 +Z [0,0,1]；其他方向拒绝。新板平行于 XY 平面，从源板最低 Z 沿 -Z 生成，不受基准面或相机影响 |

返回：object — 含新增部件 ID 的工程摘要；支持撤销、重做、录制和 STEP 导出。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
model.generate_base_plate --mode horizontal --part imported-part-1 --offset 20 --thickness 10
```

```
model.generate_base_plate --mode vertical_pair --part imported-part-2 --second-part imported-part-3 --offset 20 --thickness 10
```

## model.mirror_complete

以底板（腹板）中面镜像补全所选板件

```text
model.mirror_complete --parts <["部件ID",...]> --plane-part <底板ID>
model.mirror_complete --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| parts | --parts | array |  {'type': 'string', 'min_size': 1} | 必填 |  | 待补全的等厚实体板件；原有几何保留，相接镜像合并为原部件，分离镜像新增独立部件 |
| plane_part | --plane-part | string |  | 必填 |  | 用作镜像参考的等厚底板（腹板）ID，使用实际中面，不随相机或世界 Z 改变；不得同时选入 parts |

返回：object — 整批成功提交；已完整对称时提示无需补全；部分重叠失败不修改模型。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
model.mirror_complete --parts '["flange-top","rib-1-positive"]' --plane-part web
```

## model.extend_to_plate

延长到目标板

```text
model.extend_to_plate --parts <array> --target <string> [--contacts []] [--tolerance 0.15] [--max-distance 50.0]
model.extend_to_plate --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| parts | --parts | array |  {'type': 'string', 'min_size': 1} | 必填 |  | 明确的源板 ID，整批成功才提交 |
| target | --target | string |  | 必填 |  | 参照目标板 ID；合并时保留此 ID |
| contacts | --contacts | array |  {'type': 'object', 'properties': {'part': {'type': 'string', 'min_size': 1}, 'face': {'type': 'int', 'ranges': [[0, 1000000]]}}} | 默认 [] |  | 逐源目标面编号，从1开始；0 自动判断，歧义时报错 |
| tolerance | --tolerance | float | [[0.0, 1000000.0]] | 默认 0.15 |  | 接触距离容差 mm |
| max_distance | --max-distance | float | [[0.0, 1000000.0]] | 默认 50.0 |  | 最长允许延长距离 mm |

返回：object — 原子提交；失败不改变任何部件，支持撤销和录制。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

## model.trim_to_plate

截取到目标板，保留指定一侧

```text
model.trim_to_plate --parts <array> --target <string> [--contacts []] --cuts <array> [--clearance 0.0]
model.trim_to_plate --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| parts | --parts | array |  {'type': 'string', 'min_size': 1} | 必填 |  | 明确的源板 ID，整批成功才提交 |
| target | --target | string |  | 必填 |  | 参照目标板 ID；合并时保留此 ID |
| contacts | --contacts | array |  {'type': 'object', 'properties': {'part': {'type': 'string', 'min_size': 1}, 'face': {'type': 'int', 'ranges': [[0, 1000000]]}}} | 默认 [] |  | 逐源目标面编号，从1开始；0 自动判断，歧义时报错 |
| cuts | --cuts | array |  {'type': 'object', 'properties': {'part': {'type': 'string', 'min_size': 1}, 'origin': {'type': 'array', 'items': {'type': 'float'}, 'min_size': 3, 'max_size': 3}, 'normal': {'type': 'array', 'items': {'type': 'float'}, 'min_size': 3, 'max_size': 3}, 'keep': {'type': 'enum', 'values': ['positive', 'negative']}}} | 必填 |  | 逐源工程坐标截断面和显式保留正/负侧；截取到板以所选目标面重新校验平面 |
| clearance | --clearance | float | [[0.0, 1000000.0]] | 默认 0.0 |  | 目标面外的装配间隙 mm |

返回：object — 原子提交；失败不改变任何部件，支持撤销和录制。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

## model.merge_to_plate

以目标厚度合并板件

```text
model.merge_to_plate --parts <array> --target <string> [--normal-angle 1.0] [--inplane-angle 1.0] [--max-gap 5.0]
model.merge_to_plate --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| parts | --parts | array |  {'type': 'string', 'min_size': 1} | 必填 |  | 明确的源板 ID，整批成功才提交 |
| target | --target | string |  | 必填 |  | 参照目标板 ID；合并时保留此 ID |
| normal_angle | --normal-angle | float | [[0.0, 15.0]] | 默认 1.0 |  | 法向校正最大角度 |
| inplane_angle | --inplane-angle | float | [[0.0, 15.0]] | 默认 1.0 |  | 面内校正最大角度 |
| max_gap | --max-gap | float | [[0.0, 1000000.0]] | 默认 5.0 |  | 最大桥接间隙 mm |

返回：object — 原子提交；失败不改变任何部件，支持撤销和录制。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

## model.split_to_plate

按目标板厚分割，保留两侧

```text
model.split_to_plate --parts <array> --target <string> [--clearance 0.0]
model.split_to_plate --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| parts | --parts | array |  {'type': 'string', 'min_size': 1} | 必填 |  | 明确的源板 ID，整批成功才提交 |
| target | --target | string |  | 必填 |  | 参照目标板 ID；合并时保留此 ID |
| clearance | --clearance | float | [[0.0, 1000000.0]] | 默认 0.0 |  | 目标面外的装配间隙 mm |

返回：object — 原子提交；失败不改变任何部件，支持撤销和录制。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

## model.crop_parts

按明确的工程坐标平面切除端部

```text
model.crop_parts --cuts <array>
model.crop_parts --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| cuts | --cuts | array |  {'type': 'object', 'properties': {'part': {'type': 'string', 'min_size': 1}, 'origin': {'type': 'array', 'items': {'type': 'float'}, 'min_size': 3, 'max_size': 3}, 'normal': {'type': 'array', 'items': {'type': 'float'}, 'min_size': 3, 'max_size': 3}, 'keep': {'type': 'enum', 'values': ['positive', 'negative']}}} | 必填 |  | 逐源工程坐标截断面和显式保留正/负侧；截取到板以所选目标面重新校验平面 |

返回：object — 原子提交；失败不改变任何部件，支持撤销和录制。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

## model.repair_holes

修复等厚板的贯穿孔，可选边缘小缺口

```text
model.repair_holes --parts <array> [--boundary False] [--max-hole-area 0.0] [--notch-width 35.0] [--notch-depth 20.0] [--simplify-tolerance 0.01]
model.repair_holes --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| parts | --parts | array |  {'type': 'string', 'min_size': 1} | 必填 |  | 明确的源板 ID，整批成功才提交 |
| boundary | --boundary | bool |  | 默认 False |  | 同时填补边缘小缺口，可能包括过焊孔 |
| max_hole_area | --max-hole-area | float | [[0.0, 1000000000000.0]] | 默认 0.0 |  | 内部孔面积上限 mm²，0 修复全部 |
| notch_width | --notch-width | float | [[0.0, 1000000.0]] | 默认 35.0 |  | 边缘缺口最大跨度 mm |
| notch_depth | --notch-depth | float | [[0.0, 1000000.0]] | 默认 20.0 |  | 边缘缺口最大深度 mm |
| simplify_tolerance | --simplify-tolerance | float | [[0.0, 1000000.0]] | 默认 0.01 |  | 缺口检测容差 mm |

返回：object — 原子提交；失败不改变任何部件，支持撤销和录制。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

## model.add_holes

逐板指定底边，批量添加过焊孔

```text
model.add_holes --targets <array>
model.add_holes --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| targets | --targets | array |  {'type': 'object', 'properties': {'part': {'type': 'string', 'min_size': 1}, 'edge': {'type': 'int', 'ranges': [[1, 1000000]]}, 'kind': {'type': 'enum', 'values': ['weld']}, 'shape': {'type': 'enum', 'values': ['circle', 'triangle']}, 'size': {'type': 'float', 'ranges': [[0.001, 1000000.0]]}, 'first': {'type': 'bool'}, 'second': {'type': 'bool'}}} | 必填 |  | 各板独立底边和端点开关；只修改明确部件 |

返回：object — 原子提交；失败不改变任何部件，支持撤销和录制。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

## model.export_cwrm

导出当前模型为 CWRM v2

```text
model.export_cwrm <path>
model.export_cwrm --help
```

保存当前几何，不包含原始点云、重建参数或编辑历史；不覆盖已有文件。

| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| path | 位置 1 | path |  | 必填 |  |  |

返回：object — 导出结果摘要。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

## model.export_pcd

将当前模型表面采样导出为 PCD

```text
model.export_pcd <path> [--spacing 2]
model.export_pcd --help
```

采样模型三角网格表面，曲面受网格离散精度限制；并非原始扫描点。已有点云请用 cloud.export，不覆盖已有文件。

| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| path | 位置 1 | path |  | 必填 |  |  |
| spacing | --spacing | float | [[0.01, 100000.0]] | 默认 2.0 | mm | 模型表面面积采样间距，越小点越密；最多五百万点。 |

返回：object — 导出结果摘要。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

## model.edit_plate

按右键菜单的固定规则直接编辑板件

```text
model.edit_plate --operation extend|trim|merge|split|crop|repair --parts <array> [--target <id>] [--cuts <array>] [--all false]
model.edit_plate --help
```



| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| operation | --operation | enum | ['extend', 'trim', 'merge', 'split', 'crop', 'repair'] | 必填 |  | 与菜单一致的板件操作 |
| parts | --parts | array |  {'type': 'string', 'min_size': 1} | 必填 |  | 单个源板；修复全部时记录实际范围 |
| target | --target | string |  | 默认  |  | 目标板 ID |
| cuts | --cuts | array |  {'type': 'object', 'properties': {'part': {'type': 'string', 'min_size': 1}, 'origin': {'type': 'array', 'items': {'type': 'float'}, 'min_size': 3, 'max_size': 3}, 'normal': {'type': 'array', 'items': {'type': 'float'}, 'min_size': 3, 'max_size': 3}, 'keep': {'type': 'enum', 'values': ['positive', 'negative']}}} | 默认 [] |  | 框选操作解析后的工程坐标截断面 |
| all | --all | bool |  | 默认 False |  | 修复全部时跳过不支持或无需修复的板件 |

返回：object — 实际几何、统一撤销重做与录制回放。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

