# 文件描述：
# 统一语言标准、工具链约束、输出目录和公共编译选项。
# 在仓库根 CMakeLists.txt 的 project() 之后引用，两个子项目继承这些设置。
include_guard(DIRECTORY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_FIND_USE_PACKAGE_REGISTRY OFF)
set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY OFF)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    if(NOT MSVC_TOOLSET_VERSION EQUAL 143)
        message(FATAL_ERROR "The Windows build requires MSVC v143. Select Visual Studio 2022 / -T v143.")
    endif()
endif()
if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The Windows build requires x64. Select the x64 generator architecture.")
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
# 相对于公共文件定位仓库输出目录，避免引用者的源码目录层级影响路径。
get_filename_component(RM_OUTPUT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../output" ABSOLUTE)
set(RM_OUTPUT_DIRECTORY "${RM_OUTPUT_ROOT}/${RM_COMPILER_TAG}-cpp${CMAKE_CXX_STANDARD}/$<LOWER_CASE:$<CONFIG>>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${RM_OUTPUT_DIRECTORY}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${RM_OUTPUT_DIRECTORY}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${RM_OUTPUT_DIRECTORY}")

# 两个程序使用同一组编译设置。
add_compile_definitions(EIGEN_MPL2_ONLY)
if(MSVC)
    add_compile_options(/utf-8 /Zc:__cplusplus /bigobj /W4)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()
