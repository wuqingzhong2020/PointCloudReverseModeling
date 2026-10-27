include_guard(GLOBAL)

function(rm_assert_sdk_package_locations)
    # NO_DEFAULT_PATH does not override an already cached <Package>_DIR. Fail
    # explicitly rather than mixing a previous build's SDK into this consumer.
    set(_variables Eigen3_DIR nlohmann_json_DIR PCL_DIR OpenCASCADE_DIR
        Freetype_DIR VTK_DIR Boost_DIR TBB_DIR RapidJSON_DIR
        Qt6_DIR Qt6Core_DIR Qt6Gui_DIR Qt6Widgets_DIR
        Qt6CoreTools_DIR Qt6GuiTools_DIR Qt6WidgetsTools_DIR)
    foreach(_variable IN LISTS _variables)
        if(DEFINED ${_variable} AND IS_DIRECTORY "${${_variable}}")
            file(REAL_PATH "${${_variable}}" _package_path)
            set(_prefix_path "${RM_SDK_PREFIX}")
            if(WIN32)
                string(TOLOWER "${_package_path}" _package_path)
                string(TOLOWER "${_prefix_path}" _prefix_path)
            endif()
            cmake_path(IS_PREFIX _prefix_path "${_package_path}" NORMALIZE _in_sdk)
            if(NOT _in_sdk)
                message(FATAL_ERROR "${_variable} points outside the selected SDK: ${${_variable}}. Use a fresh build directory for this SDK.")
            endif()
        endif()
    endforeach()
endfunction()

rm_assert_sdk_package_locations()

# VTK inspects Windows import libraries with dumpbin. A Visual Studio generator
# can discover cl.exe without adding its directory to the launching shell PATH.
if(MSVC)
    get_filename_component(_rm_msvc_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(DUMPBIN_EXECUTABLE NAMES dumpbin HINTS "${_rm_msvc_bin}")
    mark_as_advanced(DUMPBIN_EXECUTABLE)
endif()

# Bind the principal packages to the selected SDK instead of a system/old SDK.
find_package(Eigen3 CONFIG REQUIRED PATHS "${RM_SDK_PREFIX}/share/eigen3" NO_DEFAULT_PATH)
find_package(nlohmann_json 3.11 CONFIG REQUIRED PATHS "${RM_SDK_PREFIX}/share/nlohmann_json" NO_DEFAULT_PATH)
find_package(PCL 1.15 CONFIG REQUIRED COMPONENTS common io
    PATHS "${RM_SDK_PREFIX}/share/pcl" NO_DEFAULT_PATH)
# OCCT asks for FindFreetype.cmake while VTK loads the CONFIG package. Import
# the complete SDK export first so both calls reuse the same pair of targets.
find_package(Freetype CONFIG REQUIRED PATHS "${RM_SDK_PREFIX}/share/freetype" NO_DEFAULT_PATH)
# 本工程使用当前系统的预编译 SDK；QUIET 通过 find_dependency 传递到 VTK，
# 避免 VTKCompileTools 对 MSVC 的无关提示，REQUIRED 仍保证缺失依赖时配置失败。
find_package(OpenCASCADE CONFIG QUIET REQUIRED
    COMPONENTS FoundationClasses ModelingData ModelingAlgorithms Visualization ApplicationFramework DataExchange
    PATHS "${RM_SDK_PREFIX}/share/opencascade" NO_DEFAULT_PATH)
if(OpenCASCADE_MAJOR_VERSION LESS 8)
    message(FATAL_ERROR "ReverseModeling requires the OCCT 8 SDK exports and APIs.")
endif()

find_package(Qt6 CONFIG QUIET COMPONENTS Core Gui Widgets
    PATHS "${RM_SDK_PREFIX}/share/Qt6" "${RM_SDK_PREFIX}/Qt6/lib/cmake/Qt6"
          "${RM_SDK_PREFIX}/lib/cmake/Qt6" NO_DEFAULT_PATH)
if(NOT Qt6_FOUND)
    message(FATAL_ERROR
        "Qt6 Core/Gui/Widgets development files are missing or incomplete in ${RM_SDK_PREFIX}. "
        "ReverseModeler requires a complete prebuilt SDK containing Qt6 Core/Gui/Widgets. "
        "This project will not build Qt or fall back to another SDK. ${Qt6_NOT_FOUND_MESSAGE}")
endif()

rm_assert_sdk_package_locations()
