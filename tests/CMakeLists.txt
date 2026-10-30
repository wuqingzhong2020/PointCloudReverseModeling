project(ReverseServiceTests VERSION 0.1.0 LANGUAGES C CXX)

# 测试复用的服务源码和头文件。
set(RM_SOURCES)
set(RM_HEARDERS)
include("${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling/ReverseModeling.cmake")

# 单元测试程序专用的源码和头文件。
include("${CMAKE_CURRENT_LIST_DIR}/tests.cmake")
add_executable(ReverseServiceTests ${RM_SOURCES} ${RM_HEARDERS})
target_include_directories(ReverseServiceTests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../ReverseModeling")
target_link_libraries(ReverseServiceTests PRIVATE ${RM_COMMON_LIBRARIES})

# 单元测试程序专用的 Google Test 入口库。
target_link_libraries(ReverseServiceTests PRIVATE GTest::gtest_main)
# 命中检测回归使用真实 AIS 对象；不创建 Qt 窗口或 OpenGL 上下文。
target_link_libraries(ReverseServiceTests PRIVATE TKService TKV3d)
set_target_properties(ReverseServiceTests PROPERTIES FOLDER "qtc_runnable")
include(GoogleTest)
gtest_discover_tests(ReverseServiceTests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 120)
