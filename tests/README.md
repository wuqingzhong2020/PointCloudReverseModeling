# ReverseServiceTests

C++20 / Google Test 单元测试程序，包含点云、参数默认值、几何和异步服务检查。`tests` 与 `ReverseModeling` 是由仓库根 CMake 入口加载的两个子项目；测试直接编译 `ReverseModeling` 中的服务和几何源码，与桌面程序共享同一份实现。

## Qt Creator

1. 打开仓库根目录的 `CMakeLists.txt`。
2. 选择 `Windows / Qt desktop + Google Test / MSVC v143 / C++20` 预设；若依赖安装位置不同，在 CMake 配置中覆盖预设默认的 `CMAKE_PREFIX_PATH`。
3. 配置运行环境的依赖 DLL 搜索路径，例如将依赖的 `bin` 目录加入 `PATH`；Debug 和 Release 需要各自匹配的 DLL。
4. 选择 `ReverseServiceTests` 构建并运行，或通过测试面板运行 Google Test 用例。

仓库只保留根目录的一个 `CMakePresets.json`，主程序和测试程序共用 CMake 配置及依赖搜索路径。

## 命令行

在 VS 2022 Developer PowerShell 中，从仓库根目录执行：

```powershell
cmake --preset vc143-cpp20
cmake --build --preset windows-release --target ReverseServiceTests
ctest --preset windows-release
```

运行 CTest 前需准备好依赖 DLL 的 `PATH`。也可直接运行 `./output/vc143-cpp20/release/ReverseServiceTests.exe`，使用 `--gtest_filter=测试套件.用例名` 选择测试。当前有 5 个用例。

根工程的构建目录为 `build/vc143-cpp20/`，测试子项目的中间文件位于其 `tests/` 下，可执行程序输出到 `output/<编译器>-cpp<标准>/<构建模式>/`。

全部第三方依赖（包括 Google Test）统一由仓库根的 `cmake/Dependencies.cmake` 查找。若找不到 `GTestConfig.cmake`，检查 `CMAKE_PREFIX_PATH` 是否包含 Google Test 的安装前缀，或将 `GTest_DIR` 设置为实际包含该文件的目录。

测试专用文件在 `tests.cmake` 中维护，通过 `RM_SOURCES` 和 `RM_HEARDERS` 追加；公共服务文件由 `../ReverseModeling/ReverseModeling.cmake` 汇总。测试程序的 `CMakeLists.txt` 分别包含这两份清单，并设置自己的头文件搜索目录、公共链接库和专用的 `GTest::gtest_main`。
