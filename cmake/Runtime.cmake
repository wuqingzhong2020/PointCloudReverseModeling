# 文件描述：生成按配置隔离的运行部署脚本、构建信息及自动/手动部署入口。
include_guard(GLOBAL)
if(NOT WIN32)
    return()
endif()

set(RM_RUNTIME_RELEASE_DIRS "" CACHE STRING "Additional Release runtime DLL directories")
set(RM_RUNTIME_DEBUG_DIRS "" CACHE STRING "Additional Debug runtime DLL directories")
set(RM_OCCT_RESOURCE_DIR "${OpenCASCADE_RESOURCE_DIR}" CACHE PATH "OCCT runtime resources")
set(RM_BUILD_TAG "${RM_COMPILER_TAG}-cpp${CMAKE_CXX_STANDARD}")
set(RM_VERSION "${CMAKE_PROJECT_VERSION}")
set(RM_INFO_DIR "${CMAKE_BINARY_DIR}/envbuild")
file(MAKE_DIRECTORY "${RM_INFO_DIR}")

# 使用实际导入目标的目录及用户指定前缀，不从全局 PATH 选择其他 SDK。
set(RM_DLL_DIRS "$<TARGET_FILE_DIR:Qt6::Core>")
# find_package 在根目录 include 中执行，其导入目标也属于根目录。
get_property(_rm_imported DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY IMPORTED_TARGETS)
foreach(_rm_target IN LISTS _rm_imported)
    get_target_property(_rm_type "${_rm_target}" TYPE)
    if(_rm_type STREQUAL "SHARED_LIBRARY" OR _rm_type STREQUAL "MODULE_LIBRARY")
        list(APPEND RM_DLL_DIRS "$<TARGET_FILE_DIR:${_rm_target}>")
    endif()
endforeach()
foreach(_rm_prefix IN LISTS CMAKE_PREFIX_PATH)
    file(TO_CMAKE_PATH "${_rm_prefix}" _rm_prefix)
    list(APPEND RM_DLL_DIRS
        "$<IF:$<CONFIG:Debug>,${_rm_prefix}/debug/bin,${_rm_prefix}/bin>")
endforeach()
list(APPEND RM_DLL_DIRS
    "$<IF:$<CONFIG:Debug>,${RM_RUNTIME_DEBUG_DIRS},${RM_RUNTIME_RELEASE_DIRS}>")
list(REMOVE_DUPLICATES RM_DLL_DIRS)

set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
set(CMAKE_INSTALL_DEBUG_LIBRARIES TRUE)
include(InstallRequiredSystemLibraries)
set(RM_CRT_DLLS "")
foreach(_rm_crt IN LISTS CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
    get_filename_component(_rm_crt_name "${_rm_crt}" NAME)
    if(_rm_crt MATCHES "/Debug_NonRedist/" OR _rm_crt_name MATCHES "d\\.dll$")
        list(APPEND RM_CRT_DLLS "$<$<CONFIG:Debug>:${_rm_crt}>")
    else()
        list(APPEND RM_CRT_DLLS "$<$<NOT:$<CONFIG:Debug>>:${_rm_crt}>")
    endif()
endforeach()

# 显式选择配置感知的 qtpaths，避免分离安装的 SDK 在 Debug 部署时查询 Release 路径。
qt6_generate_deploy_script(TARGET ReverseModeler OUTPUT_SCRIPT RM_QT_DEPLOY_SCRIPT
    CONTENT "
set(QT_DEPLOY_PREFIX \"\${RM_STAGE}\")
set(QT_DEPLOY_BIN_DIR .)
qt_deploy_runtime_dependencies(
    EXECUTABLE \"\${RM_SCAN_EXE}\"
    BIN_DIR . LIB_DIR . PLUGINS_DIR .
    GENERATE_QT_CONF NO_TRANSLATIONS NO_COMPILER_RUNTIME
    DEPLOY_TOOL_OPTIONS --$<LOWER_CASE:$<CONFIG>>
        --qtpaths \"$<TARGET_FILE:Qt6::qtpaths>\")
")

find_package(Git QUIET)
set(RM_GIT_COMMIT unknown)
set(RM_GIT_DIRTY false)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE RM_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE _rm_status ERROR_QUIET)
    if(_rm_status)
        set(RM_GIT_DIRTY true)
    endif()
endif()
if(NOT RM_GIT_COMMIT)
    set(RM_GIT_COMMIT unknown)
endif()

foreach(RM_DEPLOY_TARGET IN ITEMS ReverseModeler ReverseServiceTests)
    set(RM_EXECUTABLE "$<TARGET_FILE:${RM_DEPLOY_TARGET}>")
    set(RM_DEPLOY_QT FALSE)
    if(RM_DEPLOY_TARGET STREQUAL "ReverseModeler")
        set(RM_DEPLOY_QT TRUE)
    endif()
    configure_file("${CMAKE_CURRENT_LIST_DIR}/DeployRuntime.cmake.in"
        "${RM_INFO_DIR}/${RM_DEPLOY_TARGET}.in" @ONLY)
    file(GENERATE OUTPUT "${RM_INFO_DIR}/$<CONFIG>/deploy-${RM_DEPLOY_TARGET}.cmake"
        INPUT "${RM_INFO_DIR}/${RM_DEPLOY_TARGET}.in")
endforeach()

# 默认构建在不重新链接时也检查运行文件。手动入口不依赖主程序，绝不触发编译。
add_custom_target(runtime_ready ALL
    COMMAND "${CMAKE_COMMAND}" -P "${RM_INFO_DIR}/$<CONFIG>/deploy-ReverseModeler.cmake"
    DEPENDS ReverseModeler VERBATIM)
add_custom_target(prepare_runtime
    COMMAND "${CMAKE_COMMAND}" -DRM_FORCE=TRUE -P "${RM_INFO_DIR}/$<CONFIG>/deploy-ReverseModeler.cmake"
    VERBATIM)
set_target_properties(runtime_ready prepare_runtime PROPERTIES FOLDER "deployment")

set(_rm_prefix_json "")
foreach(_rm_prefix IN LISTS CMAKE_PREFIX_PATH)
    file(TO_CMAKE_PATH "${_rm_prefix}" _rm_prefix)
    if(_rm_prefix_json)
        string(APPEND _rm_prefix_json ", ")
    endif()
    string(APPEND _rm_prefix_json "\"${_rm_prefix}\"")
endforeach()
file(GENERATE OUTPUT "${RM_INFO_DIR}/$<CONFIG>/build-info.json" CONTENT
"{
  \"schema\": 1,
  \"source\": \"${CMAKE_SOURCE_DIR}\",
  \"build\": \"${CMAKE_BINARY_DIR}\",
  \"configuration\": \"$<CONFIG>\",
  \"executable\": \"$<TARGET_FILE:ReverseModeler>\",
  \"tests\": \"$<TARGET_FILE:ReverseServiceTests>\",
  \"version\": \"${RM_VERSION}\",
  \"tag\": \"${RM_BUILD_TAG}\",
  \"commit\": \"${RM_GIT_COMMIT}\",
  \"dirty\": ${RM_GIT_DIRTY},
  \"prefixes\": [${_rm_prefix_json}],
  \"deploy_script\": \"${RM_INFO_DIR}/$<CONFIG>/deploy-ReverseModeler.cmake\"
}
")
