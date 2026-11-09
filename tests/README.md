# ReverseServiceTests

C++20 / Google Test 单元测试程序，包含点云、逐板识别与镜像、几何和异步服务检查。`tests` 与 `ReverseModeling` 是由仓库根 CMake 入口加载的两个子项目；测试直接编译 `ReverseModeling` 中的服务和几何源码，与桌面程序共享同一份实现。

停靠窗口专项为独立的 `WorkbenchDockTests` 目标，加载实际工作台 QSS，使用 Qt/ADS 验证右侧及底部初始布局、底部标签切换、自动隐藏/固定两种状态、统一图钉提示、点击标签固定展开、外部点击不收起、左右及底部各自只有一个可见窗口、编辑状态保留及布局恢复。还检查常驻 Qt 标签的数量、顺序、位置和高亮在展开/收起/恢复后保持一致，菜单与标签选择同步，以及同侧内容反复切换、侧边与底部同时固定时三维视图和命令面板让位、拖动尺寸与小窗口缩放、手动收起及恢复布局。构建后运行 `ctest --test-dir build/vc143-cpp20 -C Release -R ^WorkbenchDockTests$ --output-on-failure`；CTest 自动设置 offscreen 平台及 SDK 插件目录，不改动正式程序的布局配置。直接运行 EXE 时需自行设置 `QT_QPA_PLATFORM=offscreen` 和 `QT_QPA_PLATFORM_PLUGIN_PATH`。运行库沿用同目录已部署的主程序/测试依赖，或使用 SDK 的 DLL 搜索路径。

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

运行 CTest 前需准备好依赖 DLL 的 `PATH`。也可直接运行 `./output/vc143-cpp20/release/ReverseServiceTests.exe`，使用 `--gtest_filter=测试套件.用例名` 选择测试，或使用 `--gtest_list_tests` 查看当前用例。显示数据测试仅检查中立数据的部件归属、板厚、配色稳定性和坐标转换，不依赖 Qt 或 OCCT 视图。

根工程的构建目录为 `build/vc143-cpp20/`，测试子项目的中间文件位于其 `tests/` 下，可执行程序输出到 `output/<编译器>-cpp<标准>/<构建模式>/`。

全部第三方依赖（包括 Google Test）统一由仓库根的 `cmake/Dependencies.cmake` 查找。若找不到 `GTestConfig.cmake`，检查 `CMAKE_PREFIX_PATH` 是否包含 Google Test 的安装前缀，或将 `GTest_DIR` 设置为实际包含该文件的目录。

测试专用文件在 `tests.cmake` 中维护，通过 `RM_SOURCES` 和 `RM_HEARDERS` 追加；公共服务文件由 `../ReverseModeling/ReverseModeling.cmake` 汇总。测试程序的 `CMakeLists.txt` 分别包含这两份清单，并设置自己的头文件搜索目录、公共链接库和专用的 `GTest::gtest_main`。


T 排专项测试：`ReverseServiceTests.exe --gtest_filter=TBarRecognition.*`。用尺寸已知的合成点云验证直/斜板、异形槽口、地面/台架过滤、多工件、未定厚观测面、参数和取消、撤销重做、命令重放与 STEP 回读。用户样例生成器与尺寸说明见[主业务指南](../ReverseModeling/README.md#t-排点云重建)。

STEP/CWRM 板厚测试：`ReverseServiceTests.exe --gtest_filter=PlateThickness*:FileExchange.*:MirrorCompletion.*`，覆盖导入板厚、数字标签锚点、带孔与样条斜接板的厚度修改、单位换算、旋转、镜像、撤销/重做和双格式导出回读。设置 `RM_PLATE_SAMPLE_DIRECTORY=D:/projectWQZ/other-resource/cloud` 后，额外逐件验证 `test1.step`、`test2.cwrm`、`test3.cwrm`、`孔.step`。

全部镜像补全的工件选择测试：`ReverseServiceTests.exe --gtest_filter=MirrorCompletion.*`。覆盖传递连接、接触误差、孔内障碍物、独立工件、旋转平移、取消及版本过期；验证连通检查不写入历史，最终命令只录制所选工件，支持整体撤销/重做和脚本回放。

障碍物专项：`ReverseServiceTests.exe --gtest_filter=ObstacleReconstruction.*`，覆盖残余簇、平面/体扫描、大坐标、封闭包络包含性、孤立噪点与实体内点排除、镜像排除、双格式导出、撤销及录制。可设置 `RM_OBSTACLE_SAMPLE_FILE` 验证本地点云，设置 `RM_OBSTACLE_ACCEPTANCE_DIR` 输出摘要和含障碍物的 STEP/CWRM。

板边误判回归包含端部收齐前后的范围、断开的板边扫描条带、独立小面/实体和非板边线状物体、相接夹具、旋转板件与大坐标原点；T 排测试继续验证相接地面。真实样例 `reverse_modeling_result-20260922-08-17-37-1.pcd` 验证保留 7 块板及 2 个外部物体，排除两端共 4 个残差簇。模型树取景专项：`ReverseServiceTests.exe --gtest_filter=ModelFocus.*`，覆盖选择小目标时的整体取景、遮挡方向、侧立薄板、横竖视口均包含全部部件、显示原点及无效目标。

局部包围盒专项：`ReverseServiceTests.exe --gtest_filter=ObstacleReconstruction.LocalBox*:LocalReconstruction.*`，验证无板面点云关联的障碍物、移动/旋转后的占用范围、六面实体盒、其他对象不变、预览隔离、撤销重做、录制回放与双格式导出。真实样例验收设置 `RM_OBSTACLE_SAMPLE_FILE` 为点云路径，并设置 `RM_OBSTACLE_BOX_PART=obstacle-1`，运行 `ObstacleReconstruction.OptionalLocalScanRetainsObstacleGeometry`。

其他模型专项：`ReverseServiceTests.exe --gtest_filter=OtherModelRecognition.*`。仅用俯视可见点验证底板＋筋板、圆角围板＋外部筋板、可见长圆孔、不同高度、断带不跨接、围板内外障碍物、旋转/倾斜与大坐标、命令撤销重做、底板视图及 STEP 回读；不将合成验证当作实物验收。

板厚交互回归：`ReverseServiceTests.exe --gtest_filter=ThicknessLabel*:OtherModelRecognition.*`，包含圆角围板约 12 mm 测厚、保持中间轮廓和高度的改厚、底板默认 1 mm、未知观测面赋厚和保持焊接接触面。`ThicknessLabelEditor.*` 使用实际 Qt 输入事件验证回车提交、未知值默认 1 mm 确认、外部点击/失焦/Esc 取消；无桌面运行可设置 `QT_QPA_PLATFORM=offscreen`，并将 `QT_QPA_PLATFORM_PLUGIN_PATH` 指向 SDK 的 `Qt6/plugins/platforms`。
