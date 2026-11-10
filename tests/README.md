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

运行 CTest 前需完成测试运行库部署，或准备好依赖 DLL 的 `PATH`。SDK 提供 offscreen 插件时，CTest 为服务和停靠窗口测试统一设置该平台与 SDK 插件路径，不要求桌面焦点。也可直接运行 `./output/vc143-cpp20/release/ReverseServiceTests.exe`，使用 `--gtest_filter=测试套件.用例名` 选择测试，或使用 `--gtest_list_tests` 查看当前用例。显示数据测试仅检查中立数据的部件归属、板厚、配色稳定性和坐标转换，不依赖 Qt 或 OCCT 视图。

根工程的构建目录为 `build/vc143-cpp20/`，测试子项目的中间文件位于其 `tests/` 下，可执行程序输出到 `output/<编译器>-cpp<标准>/<构建模式>/`。

全部第三方依赖（包括 Google Test）统一由仓库根的 `cmake/Dependencies.cmake` 查找。若找不到 `GTestConfig.cmake`，检查 `CMAKE_PREFIX_PATH` 是否包含 Google Test 的安装前缀，或将 `GTest_DIR` 设置为实际包含该文件的目录。

测试专用文件在 `tests.cmake` 中维护，通过 `RM_SOURCES` 和 `RM_HEARDERS` 追加；公共服务文件由 `../ReverseModeling/ReverseModeling.cmake` 汇总。测试程序的 `CMakeLists.txt` 分别包含这两份清单，并设置自己的头文件搜索目录、公共链接库和专用的 `GTest::gtest_main`。


T 排专项测试：`ReverseServiceTests.exe --gtest_filter=TBarRecognition.*`。用尺寸已知的合成点云验证直/斜板、异形槽口、地面/台架过滤、多工件、未定厚观测面、参数和取消、撤销重做、命令重放与 STEP 回读。用户样例生成器与尺寸说明见[主业务指南](../ReverseModeling/README.md#t-排点云重建)。

STEP/CWRM 板厚测试：`ReverseServiceTests.exe --gtest_filter=PlateThickness*:FileExchange.*:MirrorCompletion.*`，覆盖导入板厚、数字标签锚点、带孔与样条斜接板的厚度修改、单位换算、旋转、镜像、撤销/重做和双格式导出回读。设置 `RM_PLATE_SAMPLE_DIRECTORY` 为本机样例目录（例如 `D:/work/samples`）后，额外逐件验证 `test1.step`、`test2.cwrm`、`test3.cwrm`、`孔.step`。

全部镜像补全的工件选择测试：`ReverseServiceTests.exe --gtest_filter=MirrorCompletion.*`。覆盖传递连接、接触误差、孔内障碍物、独立工件、旋转平移、取消及版本过期；验证连通检查不写入历史，最终命令只录制所选工件，支持整体撤销/重做和脚本回放。

障碍物专项：`ReverseServiceTests.exe --gtest_filter=ObstacleReconstruction.*`，覆盖残余簇、平面/体扫描、大坐标、封闭包络包含性、孤立噪点与实体内点排除、镜像排除、双格式导出、撤销及录制。可设置 `RM_OBSTACLE_SAMPLE_FILE` 验证本地点云，设置 `RM_OBSTACLE_ACCEPTANCE_DIR` 输出摘要和含障碍物的 STEP/CWRM。

板边误判回归包含端部收齐前后的范围、断开的板边扫描条带、独立小面/实体和非板边线状物体、相接夹具、旋转板件与大坐标原点；T 排测试继续验证相接地面。真实样例 `reverse_modeling_result-20260922-08-17-37-1.pcd` 验证保留 7 块板及 2 个外部物体，排除两端共 4 个残差簇。模型树取景专项：`ReverseServiceTests.exe --gtest_filter=ModelFocus.*`，覆盖选择小目标时的整体取景、遮挡方向、侧立薄板、横竖视口均包含全部部件、显示原点及无效目标。

局部包围盒专项：`ReverseServiceTests.exe --gtest_filter=ObstacleReconstruction.LocalBox*:LocalReconstruction.*`，验证无板面点云关联的障碍物、移动/旋转后的占用范围、六面实体盒、其他对象不变、预览隔离、撤销重做、录制回放与双格式导出。真实样例验收设置 `RM_OBSTACLE_SAMPLE_FILE` 为点云路径，并设置 `RM_OBSTACLE_BOX_PART=obstacle-1`，运行 `ObstacleReconstruction.OptionalLocalScanRetainsObstacleGeometry`。

其他模型专项：`ReverseServiceTests.exe --gtest_filter=OtherModelRecognition.*`。仅用俯视可见点验证底板＋筋板、圆角围板＋外部筋板、可见长圆孔、不同高度、断带不跨接、围板内外障碍物、旋转/倾斜与大坐标、命令撤销重做、底板视图及 STEP 回读；不将合成验证当作实物验收。

板厚交互回归：`ReverseServiceTests.exe --gtest_filter=ThicknessLabel*:OtherModelRecognition.*`，包含圆角围板约 12 mm 测厚、保持中间轮廓和高度的改厚、底板默认 1 mm、未知观测面赋厚和保持焊接接触面。`ThicknessLabelEditor.*` 使用实际 Qt 输入事件验证回车提交、未知值默认 1 mm 确认、外部点击/失焦/Esc 取消；无桌面运行可设置 `QT_QPA_PLATFORM=offscreen`，并将 `QT_QPA_PLATFORM_PLUGIN_PATH` 指向 SDK 的 `Qt6/plugins/platforms`。

## 编辑事务与文件诊断回归

`ReverseServiceTests.exe --gtest_filter=ProjectEditTransactions.*:ProjectCommitTransactions.*:ProjectDiagnostics.*:CloudTransactions.*` 验证预览隔离、计算结束时取消、计算期间版本变化、诊断回调读取快照与抛错，移动/删除/首次重建的提交版本复核，以及 `commands.log` 的任务进度、任务关联、失败原因和模型提交版本。日志测试使用临时目录，不覆盖正式运行记录。大型实现拆分后仍通过公开接口运行几何、命令、选点向导和停靠窗口测试；窗口私有 State 不作为测试 API；`ModelPresentation.*` 仅在几何内部验证拓扑隔离约束，不将 `ModelState.h` 暴露给业务调用方。

`CloudTransactions.*` 额外覆盖 PCD/STEP 导入、框选、裁剪和偏差计算中的过期提交，以及诊断回调读取快照/抛错、实际文件中的导入和点云提交记录。

`EditorActionPolicy.*` 不启动窗口，验证各工具的互斥动作、组合限制、点云选择条件和空闲状态恢复；`PointModelingUi.*` 使用真实 Qt 控件验证选面、框选、预览、应用、源数据替换后的退出，以及应用日志中剖视前后点数和预览结果。日志测试使用临时目录。`ModelPresentation.*` 验证重建网格不修改旧模型的 OCCT 拓扑、局部/世界边界语义及重复重建的体积和网格一致性。

命令生成器测试由 CTest 使用 `RM_CODEGEN_PYTHON` 运行；也可执行 `build/codegen-python/Scripts/python.exe -B tests/commandManager/test_generator.py`。构建环境须包含既有 Jinja2 依赖。测试包括只规划不写入、损坏所有权清单、未托管文件保护、只读检查与完整 CMake 源文件清单，以及部分模块更新和废弃文件清理。

## 功能覆盖索引

以下按现有功能组织行为回归，不以测试数量代替真实扫描精度验收。常规测试使用合成点云、内存模型和临时目录；本机样例验收仍由环境变量显式启用。

| 功能 | 测试套件 | 主要验证 |
|---|---|---|
| 点云导入、框选、裁剪 | `PointCloud`、`CloudEditing`、`CloudEditCommands`、`CloudTransactions`、`CloudCancellation` | 单位和属性、全分辨率选区、加减选、取消和过期结果不污染工程、保留重做分支 |
| H 型钢、T 排及其他焊件 | `PlateRecognition`、`TBarRecognition`、`OtherModelRecognition`、`ObstacleReconstruction` | 逐板厚度、槽口和遮挡边界、多个工件、真实障碍物与扫描残差分离 |
| 局部重建 | `LocalReconstruction`、`ObstacleReconstruction.LocalBox*` | 三类角部轮廓、下边镜像开关、盒状占用空间、未选部件不变 |
| 选点建模与障碍物 | `PointModeling`、`PointModelingUi` | 矩形/圆形/多边形、多方向拉伸、切除范围、明确选面、剖视筛选、预览和提交分离、失败后修正及取消后重新进入 |
| 板件调整与装配 | `PlateThickness*`、`PlateContour`、`PlateEditing`、`MirrorCompletion`、`StructuralSteel`、`PlateGeneration`、`CrossPlate`、`EndCapPlate` | 厚度、轮廓、延长/截取/合并/分割/端部裁剪、补孔、连通工件镜像、型钢和附加板 |
| 显示、选择、测量 | `ModelDisplay*`、`ModelFocus`、`ViewportPicking`、`PointMeasurement`、`MeasurementSnap`、`ThicknessLabel*` | 部件索引、取景和遮挡、拾取几何、标签编辑、参考面和坐标变换 |
| 命令、历史与日志 | `CommandService`、`ProjectHistoryTest`、`Project*Transactions`、`ProjectDiagnostics`、`PythonConsole` | 参数拒绝、原子提交、撤销重做、录制回放、脚本中断、任务与文件日志 |
| 文件交换 | `FileExchange`、`ExportTransactions`、`TaskOutputTest` | PCD/STEP/CWRM 回读、位置/体积/属性、取消、目标争用不覆盖、异常清理及重试 |
| 窗口布局与工具互斥 | `WorkbenchDockTests`、`EditorActionPolicy` | 固定/收起、常驻标签、同侧单窗口、视图让位、模式互斥与恢复 |
| 生成、构建、部署、打包 | `CommandGeneratorUnitTests`、`EnvBuildUnitTests` | 输出所有权、生成失败清理、命令帮助、构建失败停止、清理边界、运行库核验、包校验和及发布失败 |

### 本轮补齐的场景

- `AllFormats/ExportTransactions.*`：PCD、STEP、CWRM 各自验证最后提交检查点取消、生成期间目标被其他写入方创建、进度回调异常后的清理及重试、已有文件保护和输出父目录缺失。取消注入到确定的进度回调中，不依赖线程竞速。
- `AllOperations/CloudCancellation.*`：PCD/STEP 导入、框选、点云编辑和偏差计算运行中取消；断言模型、网格、选区、版本及历史不变，并实际重做验证原分支可用。另覆盖四种按索引选择模式的预先取消。
- `PointModelingUi.*`：补普通矩形向导的尺寸修改/应用/撤销重做、圆形直径推算及换参考方向、未完成预览退出后重新进入、无效选点纠正。新增异步流程按状态等待，有超时上限，不以固定睡眠时长判定完成。
- `PointModeling.*`：补默认切除保留底板、全部切除、倾斜参考面两侧向面拉伸、非有限参数和无效参考系、几何完成后的取消隔离。
- `EnvBuildUnitTests`：既有 26 项脚本单元测试纳入 CTest，使用临时目录和模拟命令，不清理正式产物或调用真实打包/部署。

```powershell
# 全部自动化回归；共享运行环境按串行执行。
ctest --test-dir build/vc143-cpp20 -C Release --output-on-failure --parallel 1
# 重点功能组。
ctest --test-dir build/vc143-cpp20 -C Release -R "ExportTransactions|CloudCancellation|PointModeling" --output-on-failure
# 构建工具与命令生成器，不启动桌面程序。
ctest --test-dir build/vc143-cpp20 -C Release -L build --output-on-failure
```

### Qt 平台插件错误排查

若提示 `Available platform plugins are: windows`，同时测试指定 `offscreen`，应先检查测试的实际环境变量和 SDK 的 `qoffscreen.dll`，无需重装产品。`gtest_discover_tests` 生成脚本时会再次展开列表，不能把两个环境变量直接合并成带分号的 `ENVIRONMENT` 值。当前分别使用 `ENVIRONMENT` 指定平台、`ENVIRONMENT_MODIFICATION` 指定插件目录，停靠窗口与服务测试使用同一配置。

可运行 `ctest --test-dir build/vc143-cpp20 -C Release -N --show-only=json-v1` 检查最终生成的属性，再用 `-R "ThicknessLabelEditor|PointModelingUi|WorkbenchDockTests"` 单独验证控件测试。直接运行测试 EXE 时仍需在当前终端设置这两个环境变量。
