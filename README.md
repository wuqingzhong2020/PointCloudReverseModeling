# ReverseModeling

点云逆向建模工程与研究资料。当前工程代码位于 [ReverseModeling](ReverseModeling/README.md)，使用 CMake / C++20，包含 Qt 工作台源码和可独立运行的命令行服务。

第三方库统一由 [LibPackByVcpkgWindows](../../../LibPackByVcpkgWindows/README.md) 使用 vcpkg 编译维护，以该工程的 [vcpkg.json](../../../LibPackByVcpkgWindows/vcpkg.json) 为当前库清单。应用工程直接使用发布的预编译 SDK，开发者无需下载或配置 vcpkg。参考文档和方案的采用状态统一见下方资料入口。

## 文档入口

| 入口 | 内容 |
|---|---|
| [工程构建与使用](ReverseModeling/README.md) | 预编译 SDK 直接使用、CMake 配置、Qt/命令行使用、首版能力与验证边界 |
| [研究与实现参考资料](reference/README.md) | 参考文档目录、实施背景、维护约定与原始材料归档说明 |
| [Python 脚本编码规范](docs/Python脚本编码规范.md) | 产品嵌入式 Python 的依赖边界、编码规则、宿主接口和回归脚本约定 |

## 目录结构

```text
ReverseModeling/
├── README.md
├── .gitignore
├── ReverseModeling/       # CMake 工程、公共契约、几何/命令服务、Qt/CLI 与定向测试
├── docs/
│   └── Python脚本编码规范.md
└── reference/
    ├── README.md         # 研究与实现参考资料入口
    └── ...               # 研究与实现参考文档，含配套 Word 阅读版本
```
