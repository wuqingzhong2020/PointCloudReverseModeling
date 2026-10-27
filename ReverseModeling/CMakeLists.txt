cmake_minimum_required(VERSION 3.25)

# Consume the separately maintained SDK; this project never installs its ports.
include(cmake/PrebuiltSdk.cmake)
project(ReverseModeling VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_FIND_USE_PACKAGE_REGISTRY OFF)
set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
if(MSVC AND NOT MSVC_TOOLSET_VERSION EQUAL 143)
    message(FATAL_ERROR "The Windows SDK requires MSVC v143. Select Visual Studio 2022 / -T v143.")
endif()
if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The Windows SDK requires x64. Select the x64 generator architecture.")
endif()

# 产物按实际编译器和 C++ 标准归档，构建中间文件仍保留在 CMAKE_BINARY_DIR。
if(MSVC)
    set(RM_COMPILER_TAG "vc${MSVC_TOOLSET_VERSION}")
else()
    string(REGEX MATCH "^[0-9]+" RM_COMPILER_MAJOR "${CMAKE_CXX_COMPILER_VERSION}")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(RM_COMPILER_TAG "gcc${RM_COMPILER_MAJOR}")
    else()
        string(TOLOWER "${CMAKE_CXX_COMPILER_ID}" RM_COMPILER_NAME)
        set(RM_COMPILER_TAG "${RM_COMPILER_NAME}${RM_COMPILER_MAJOR}")
    endif()
endif()
get_filename_component(RM_OUTPUT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../output" ABSOLUTE)
set(RM_OUTPUT_DIRECTORY "${RM_OUTPUT_ROOT}/${RM_COMPILER_TAG}-cpp${CMAKE_CXX_STANDARD}/$<LOWER_CASE:$<CONFIG>>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${RM_OUTPUT_DIRECTORY}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${RM_OUTPUT_DIRECTORY}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${RM_OUTPUT_DIRECTORY}")

include(cmake/Dependencies.cmake)
find_package(Threads REQUIRED)

add_library(ReverseBuildOptions INTERFACE)
target_compile_features(ReverseBuildOptions INTERFACE cxx_std_20)
target_compile_definitions(ReverseBuildOptions INTERFACE EIGEN_MPL2_ONLY)
if(MSVC)
    target_compile_options(ReverseBuildOptions INTERFACE /utf-8 /Zc:__cplusplus /bigobj /W4)
    target_compile_definitions(ReverseBuildOptions INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(ReverseBuildOptions INTERFACE -Wall -Wextra -Wpedantic)
endif()

add_library(ReverseModelCore INTERFACE)
add_library(ReverseModeling::Core ALIAS ReverseModelCore)
target_include_directories(ReverseModelCore INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(ReverseModelCore INTERFACE ReverseBuildOptions)
target_sources(ReverseModelCore INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include/reverse/Types.h")

add_library(ReverseGeometry STATIC
    include/reverse/Geometry.h
    src/geometry/PointCloud.cpp
    src/geometry/BeamModel.cpp)
add_library(ReverseModeling::Geometry ALIAS ReverseGeometry)
target_link_libraries(ReverseGeometry PUBLIC ReverseModelCore PRIVATE
    Eigen3::Eigen pcl_common pcl_io
    TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKTopAlgo TKPrim TKMesh
    TKDESTEP TKXCAF TKLCAF TKCAF TKXSBase)

add_library(ReverseCommands STATIC
    include/reverse/CommandService.h
    include/reverse/Json.h
    include/reverse/ParameterDefaults.h
    src/commands/CommandService.cpp
    src/commands/ParameterDefaults.cpp
    src/commands/Json.cpp)
add_library(ReverseModeling::Commands ALIAS ReverseCommands)
target_link_libraries(ReverseCommands PUBLIC ReverseModelCore nlohmann_json::nlohmann_json
    PRIVATE ReverseGeometry Threads::Threads)

add_library(ReverseWorkbench STATIC
    include/reverse/workbench/OccViewport.h
    include/reverse/workbench/MainWindow.h
    src/workbench/OccViewport.cpp
    src/workbench/MainWindow.cpp)
target_link_libraries(ReverseWorkbench PUBLIC ReverseCommands Qt6::Widgets
    PRIVATE TKService TKV3d TKOpenGl)
add_executable(ReverseModeler WIN32 src/app/main.cpp)
target_link_libraries(ReverseModeler PRIVATE ReverseWorkbench)
# Qt Creator 与 Visual Studio 默认运行桌面程序。
set_target_properties(ReverseModeler PROPERTIES FOLDER "qtc_runnable")
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ReverseModeler)

include(cmake/RuntimeEnvironment.cmake)
rm_configure_runtime(ReverseModeler)

if(BUILD_TESTING)
    add_executable(ReverseServiceTests tests/service_tests.cpp)
    target_link_libraries(ReverseServiceTests PRIVATE ReverseCommands ReverseGeometry)
    add_test(NAME ReverseModeling.Service COMMAND ReverseServiceTests)
    set_tests_properties(ReverseModeling.Service PROPERTIES TIMEOUT 120)
    rm_configure_test_runtime(ReverseModeling.Service)
endif()

install(TARGETS ReverseModeler RUNTIME DESTINATION bin)

message(STATUS "ReverseModeling SDK prefix: ${RM_SDK_PREFIX}")
message(STATUS "Application: ReverseModeler (Qt desktop); C++20; SDK dependencies are read-only")
