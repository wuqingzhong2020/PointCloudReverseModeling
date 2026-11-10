project(ReverseServiceTests VERSION 0.1.0 LANGUAGES C CXX)

# 测试复用的服务源码和头文件。
set(RM_SOURCES)
set(RM_HEARDERS)
include("${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/ReverseModeling.cmake")

# 单元测试程序专用的源码和头文件。
include("${CMAKE_CURRENT_LIST_DIR}/tests.cmake")
add_executable(ReverseServiceTests ${RM_SOURCES} ${RM_HEARDERS})
rm_apply_compiler_options(ReverseServiceTests)
add_executable(ApplicationLogProbe application_log_probe.cpp
    ../ReverseModeling/commands/ApplicationLog.cpp)
rm_apply_compiler_options(ApplicationLogProbe)
target_include_directories(ApplicationLogProbe PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling")
target_compile_definitions(ApplicationLogProbe PRIVATE QT_MESSAGELOGCONTEXT)
target_link_libraries(ApplicationLogProbe PRIVATE Qt6::Core)
if(WIN32)
    target_link_libraries(ApplicationLogProbe PRIVATE dbghelp)
endif()
add_dependencies(ReverseServiceTests ApplicationLogProbe)
target_include_directories(ReverseServiceTests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling")
target_link_libraries(ReverseServiceTests PRIVATE ${RM_COMMON_LIBRARIES})

# 单元测试程序专用的 Google Test 入口库。
target_link_libraries(ReverseServiceTests PRIVATE GTest::gtest_main)
# 标签编辑回归使用实际 QWidget 的焦点和键盘事件；可用 offscreen 平台执行。
target_link_libraries(ReverseServiceTests PRIVATE Qt6::Widgets)
target_link_libraries(ReverseServiceTests PRIVATE ads::qtadvanceddocking-qt6)
# 命中检测回归使用真实 AIS 对象；不创建 Qt 窗口或 OpenGL 上下文。
target_link_libraries(ReverseServiceTests PRIVATE TKService TKV3d TKOpenGl)
set_target_properties(ReverseServiceTests PROPERTIES FOLDER "qtc_runnable")
if(WIN32)
    add_custom_command(TARGET ReverseServiceTests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -P
            "${CMAKE_BINARY_DIR}/envbuild/$<CONFIG>/deploy-ReverseServiceTests.cmake"
        COMMENT "Deploying unit test runtime" VERBATIM)
endif()
include(GoogleTest)
# 控件测试使用统一的离屏平台，避免完整回归依赖桌面焦点或显示会话。
set(_rm_qt_test_properties TIMEOUT 120)
if(TARGET Qt6::QOffscreenIntegrationPlugin)
    # GTest 的发现脚本会再次展开列表；两个环境变量不能合并成带分号的属性值。
    list(APPEND _rm_qt_test_properties
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION
            "QT_QPA_PLATFORM_PLUGIN_PATH=set:$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>")
endif()
gtest_discover_tests(ReverseServiceTests DISCOVERY_MODE PRE_TEST
    PROPERTIES ${_rm_qt_test_properties})

# 停靠窗口需要独立 QApplication，使用真实 ADS 验证收起、关闭与编辑状态的区别。
add_executable(WorkbenchDockTests dock_workspace_tests.cpp
    ../ReverseModeling/workbench/DockWorkspace.cpp
    ../ReverseModeling/workbench/WorkbenchTheme.cpp
    ../ReverseModeling/workbench/WorkbenchActions.cpp
    ../ReverseModeling/workbench/WorkbenchActions.h
    ../ReverseModeling/workbench/WorkbenchActionHelp.cpp
    ../ReverseModeling/workbench/resources/workbench.qrc)
rm_apply_compiler_options(WorkbenchDockTests)
target_include_directories(WorkbenchDockTests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling")
target_link_libraries(WorkbenchDockTests PRIVATE Qt6::Widgets ads::qtadvanceddocking-qt6 GTest::gtest)
set_target_properties(WorkbenchDockTests PROPERTIES FOLDER "qtc_runnable" AUTORCC ON)
add_test(NAME WorkbenchDockTests COMMAND WorkbenchDockTests)
set_tests_properties(WorkbenchDockTests PROPERTIES
    ${_rm_qt_test_properties} TIMEOUT 60)
add_test(NAME CommandGeneratorUnitTests
    COMMAND "${RM_CODEGEN_PYTHON}" -B "${CMAKE_CURRENT_LIST_DIR}/commandManager/test_generator.py")

# 构建、部署及打包脚本使用临时目录和模拟命令，不操作正式 SDK 或运行目录。
add_test(NAME EnvBuildUnitTests
    COMMAND "${RM_CODEGEN_PYTHON}" -B -m unittest discover
        -s "${CMAKE_CURRENT_SOURCE_DIR}/python" -p test_envbuild.py)
set_tests_properties(EnvBuildUnitTests PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" LABELS "build" TIMEOUT 60)
set_tests_properties(CommandGeneratorUnitTests PROPERTIES LABELS "build" TIMEOUT 60)
