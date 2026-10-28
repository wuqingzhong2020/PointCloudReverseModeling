project(ReverseModeling VERSION 0.1.0 LANGUAGES C CXX)

# 公共服务源码和头文件。
set(RM_SOURCES)
set(RM_HEARDERS)
include("${CMAKE_CURRENT_LIST_DIR}/ReverseModeling.cmake")

# 主程序专用的入口、工作台源码和头文件。
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/main.cpp")
include("${CMAKE_CURRENT_LIST_DIR}/workbench/workbench.cmake")
add_executable(ReverseModeler WIN32 ${RM_SOURCES} ${RM_HEARDERS})
target_include_directories(ReverseModeler PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(ReverseModeler PRIVATE ${RM_COMMON_LIBRARIES})

# 主程序专用的 Qt Widgets 和 OCCT 显示库。
target_link_libraries(ReverseModeler PRIVATE
    Qt6::Widgets TKService TKV3d TKOpenGl)
set_target_properties(ReverseModeler PROPERTIES FOLDER "qtc_runnable")

install(TARGETS ReverseModeler RUNTIME DESTINATION bin)

message(STATUS "Application: ReverseModeler (Qt desktop); C++20; dependencies provided by find_package")
