include_guard(GLOBAL)

# VTK inspects Windows import libraries with dumpbin. A Visual Studio generator
# can discover cl.exe without adding its directory to the launching shell PATH.
if(MSVC)
    get_filename_component(_rm_msvc_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(DUMPBIN_EXECUTABLE NAMES dumpbin HINTS "${_rm_msvc_bin}")
    mark_as_advanced(DUMPBIN_EXECUTABLE)
endif()

# 由使用者通过 CMAKE_PREFIX_PATH 或各包的 <Package>_DIR 提供依赖位置。
find_package(Eigen3 CONFIG REQUIRED)
find_package(nlohmann_json 3.11 CONFIG REQUIRED)
find_package(PCL 1.15 CONFIG REQUIRED COMPONENTS common io)
# 部分 OCCT/VTK 配置分别使用 Module 和 Config 模式查找 FreeType。
# 若存在 Config 导出则提前加载，避免重复定义目标；否则由依赖包自行查找。
find_package(Freetype CONFIG QUIET)
# QUIET 通过 find_dependency 传递到 VTK，
# 避免 VTKCompileTools 对 MSVC 的无关提示，REQUIRED 仍保证缺失依赖时配置失败。
find_package(OpenCASCADE CONFIG QUIET REQUIRED
    COMPONENTS FoundationClasses ModelingData ModelingAlgorithms Visualization ApplicationFramework DataExchange)
if(OpenCASCADE_MAJOR_VERSION LESS 8)
    message(FATAL_ERROR "ReverseModeling requires OCCT 8 or newer.")
endif()

find_package(Threads REQUIRED)
find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui Widgets)
find_package(GTest CONFIG REQUIRED)

# 两个程序共用的第三方库；各程序专用库在自己的 CMakeLists.txt 中链接。
set(RM_COMMON_LIBRARIES
    Eigen3::Eigen nlohmann_json::nlohmann_json pcl_common pcl_io Threads::Threads
    TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKTopAlgo TKPrim TKMesh
    TKDESTEP TKXCAF TKLCAF TKCAF TKXSBase)
