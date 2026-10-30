project(ReverseModeling VERSION 0.1.0 LANGUAGES C CXX)

# 公共服务源码和头文件。
set(RM_SOURCES)
set(RM_HEARDERS)
include("${CMAKE_CURRENT_LIST_DIR}/ReverseModeling.cmake")

# 主程序专用的入口、工作台源码和头文件。
list(APPEND RM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/main.cpp")
include("${CMAKE_CURRENT_LIST_DIR}/workbench/workbench.cmake")
if(WIN32)
    enable_language(RC)
    set(RM_APPLICATION_ICON "${CMAKE_CURRENT_LIST_DIR}/workbench/resources/app/ReverseModeler.ico")
    configure_file(workbench/resources/app/ReverseModeler.rc.in ReverseModeler.rc @ONLY)
    list(APPEND RM_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/ReverseModeler.rc")
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/ReverseModeler.rc"
        PROPERTIES OBJECT_DEPENDS "${RM_APPLICATION_ICON}")
endif()
add_executable(ReverseModeler WIN32 ${RM_SOURCES} ${RM_HEARDERS})
target_compile_definitions(ReverseModeler PRIVATE RM_APP_VERSION="${CMAKE_PROJECT_VERSION}")
target_include_directories(ReverseModeler PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(ReverseModeler PRIVATE ${RM_COMMON_LIBRARIES})

# 主程序专用的 Qt Widgets 和 OCCT 显示库。
target_link_libraries(ReverseModeler PRIVATE
    Qt6::Widgets TKService TKV3d TKOpenGl
    SARibbonBar::SARibbonBar ads::qtadvanceddocking-qt6)
set_target_properties(ReverseModeler PROPERTIES AUTOMOC ON AUTORCC ON)
set_target_properties(ReverseModeler PROPERTIES FOLDER "qtc_runnable")

install(TARGETS ReverseModeler RUNTIME DESTINATION bin)
if(WIN32)
    target_link_libraries(ReverseModeler PRIVATE shell32)
    add_custom_command(TARGET ReverseModeler POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -P
            "${CMAKE_BINARY_DIR}/envbuild/$<CONFIG>/deploy-ReverseModeler.cmake"
        COMMENT "Deploying ReverseModeler runtime" VERBATIM)
endif()

message(STATUS "Application: ReverseModeler (Qt desktop); C++20; dependencies provided by find_package")
