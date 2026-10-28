# ReverseModeling

点云逆向建模工程与研究资料。当前工程代码位于 [ReverseModeling](ReverseModeling/README.md)，使用 CMake / C++20，唯一产品入口为 Qt 桌面程序 `ReverseModeler`，始终包含 GUI。

第三方库由使用者预先准备，通过 `CMAKE_PREFIX_PATH` 和标准 `find_package()` 接入，可来自 vcpkg 或其他编译方式，也可分布在不同安装目录。工程只声明所需库、版本和组件，不管理第三方库的编译、安装或发布。参考文档和方案的采用状态统一见下方资料入口。

## 文档入口

| 入口 | 内容 |
|---|---|
| [工程构建与使用](ReverseModeling/README.md) | 第三方依赖查找、CMake 配置、Qt 桌面程序使用、首版能力与验证边界 |
| [单元测试程序](tests/README.md) | Google Test 构建、运行与用例选择 |
| [研究与实现参考资料](reference/README.md) | 参考文档目录、实施背景、维护约定与原始材料归档说明 |
| [Python 脚本编码规范](docs/Python脚本编码规范.md) | 产品嵌入式 Python 的依赖边界、编码规则、宿主接口和回归脚本约定 |

## 目录结构

```text
PointCloudReverseModeling/
├── CMakeLists.txt         # 总入口，加载主业务和单元测试两个子项目
├── CMakePresets.json      # 全仓库唯一的 CMake 预设
├── README.md
├── .gitignore
├── cmake/                # 公共构建设置与全部第三方依赖查找
├── ReverseModeling/      # 主业务子项目，生成 ReverseModeler
│   ├── CMakeLists.txt
│   ├── ReverseModeling.cmake  # 公共服务文件清单入口
│   ├── main.cpp
│   ├── commands/
│   ├── core/
│   ├── geometry/
│   ├── workbench/
│   └── examples/
├── tests/                # 单元测试子项目，生成 ReverseServiceTests
│   ├── CMakeLists.txt
│   ├── README.md
│   ├── tests.cmake
│   └── service_tests.cpp
├── docs/
│   └── Python脚本编码规范.md
└── reference/
    ├── README.md         # 研究与实现参考资料入口
    └── ...               # 研究与实现参考文档，含配套 Word 阅读版本
```

每个包含 C++ 文件的目录维护一个同名 `.cmake`，通过 `RM_SOURCES` 和 `RM_HEARDERS` 分别追加源文件与头文件，每个文件单独一条 `list(APPEND ...)`。主程序入口 `ReverseModeling/main.cpp` 在主程序的 `CMakeLists.txt` 中单独追加。各程序的 `CMakeLists.txt` 组合公共及专用文件清单，设置头文件搜索目录和链接库；所有第三方库的 `find_package()` 集中在 `cmake/Dependencies.cmake`。

## 构建入口

在 Qt Creator 中打开仓库根目录的 `CMakeLists.txt`，选择 `Windows / Qt desktop + Google Test / MSVC v143 / C++20` 预设。主程序和测试程序共享一次配置，可分别选择 `ReverseModeler` 或 `ReverseServiceTests` 构建、运行。

`CMakePresets.json` 已将 `CMAKE_PREFIX_PATH` 默认设为 `D:/projectWQZ/LibPackByVcpkgWindows/install_vc143_cpp20/x64-windows-v143-cpp20`。其他使用者可直接修改该值，或在配置命令中添加 `-DCMAKE_PREFIX_PATH="<依赖安装前缀>"` 覆盖；Qt Creator 中也可通过项目的 CMake 配置覆盖。

命令行在仓库根目录执行：

```powershell
cmake --preset vc143-cpp20
cmake --build --preset windows-release
ctest --preset windows-release
```

运行前需准备好依赖 DLL 的搜索路径。构建中间文件位于 `build/vc143-cpp20/`，两个程序输出到 `output/vc143-cpp20/release/`。从原先的子目录构建入口迁移时，先执行 `cmake --fresh --preset vc143-cpp20 -DCMAKE_PREFIX_PATH="<依赖安装前缀>"`，让缓存使用新的根目录入口。
