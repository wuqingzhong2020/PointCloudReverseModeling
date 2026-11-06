# file — 文件格式转换

由 `.rmdef` 生成，请修改声明后重新生成。

参数对可混排且顺序可调；位置参数必须保持相对顺序。`--` 之后均为位置值。
运行时输入 `help file` 可查询本模块，是否绑定业务以运行时帮助为准。

## file.convert

转换 STEP/CWRM 模型格式或采样为 PCD

```text
file.convert <input.step|input.cwrm> <output.step|output.cwrm|output.pcd> [--spacing 2]
file.convert --help
```

输入仅支持 STEP/STP、CWRM。禁止以 PCD 为输入（包括 PCD 转 PCD），点云正常导入、导出不受影响。输出 PCD 为模型表面采样，并非原始扫描数据；曲面受三角网格离散精度限制。转换不修改当前工程，不覆盖已有文件。

| 参数 | 绑定 | 类型 | 约束 | 必填 / 默认 | 单位 | 说明 |
| --- | --- | --- | --- | --- | --- | --- |
| input | 位置 1 | path |  | 必填 |  |  |
| output | 位置 2 | path |  | 必填 |  |  |
| spacing | --spacing | float | [[0.01, 100000.0]] | 默认 2.0 | mm | 模型表面面积采样间距，越小点越密；最多五百万点。 |

返回：object — 当前工程摘要，转换结果写入日志。

失败时返回错误码、参数路径、期望值及实际输入，不执行业务。业务前提由处理函数检查；失败不代表前序命令回滚。

```
file.convert "D:/data/model.step" "D:/data/model.cwrm"
```

```
file.convert "D:/data/model.cwrm" "D:/data/surface.pcd" --spacing 2
```

