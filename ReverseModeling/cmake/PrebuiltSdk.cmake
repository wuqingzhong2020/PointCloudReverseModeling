include_guard(GLOBAL)

# Consume SDK package exports directly; reject an old toolchain before project().
if(CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg[.]cmake$"
        OR "$ENV{CMAKE_TOOLCHAIN_FILE}" MATCHES "vcpkg[.]cmake$")
    message(FATAL_ERROR "This project uses the prebuilt SDK without a vcpkg toolchain. Remove CMAKE_TOOLCHAIN_FILE from the IDE configuration/environment and select vc143-cpp20 with a fresh build directory. Only RM_SDK_ROOT is required on Windows.")
endif()

# 环境变量作为 SDK 路径的首选来源，避免 Qt Creator 的旧缓存遮蔽新设置。
if(NOT "$ENV{RM_SDK_ROOT}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{RM_SDK_ROOT}" _rm_sdk_root_from_environment)
    set(RM_SDK_ROOT "${_rm_sdk_root_from_environment}" CACHE PATH
        "Prebuilt SDK directory containing include/ and share/" FORCE)
else()
    set(RM_SDK_ROOT "" CACHE PATH "Prebuilt SDK directory containing include/ and share/")
endif()
if(NOT RM_SDK_ROOT)
    message(FATAL_ERROR "RM_SDK_ROOT is empty in both the CMake configuration and process environment. Set it in the IDE build environment or pass -DRM_SDK_ROOT=<sdk-directory> (the directory containing include/ and share/). No dependencies will be downloaded or built.")
endif()
file(TO_CMAKE_PATH "${RM_SDK_ROOT}" RM_SDK_ROOT)
if(NOT IS_DIRECTORY "${RM_SDK_ROOT}/share" OR NOT IS_DIRECTORY "${RM_SDK_ROOT}/include")
    message(FATAL_ERROR "RM_SDK_ROOT must point directly to the SDK directory containing include/ and share/, not its parent: ${RM_SDK_ROOT}. Check the IDE build environment first; when RM_SDK_ROOT is not set there, check the CMake configuration.")
endif()
file(REAL_PATH "${RM_SDK_ROOT}" RM_SDK_PREFIX)
list(PREPEND CMAKE_PREFIX_PATH "${RM_SDK_PREFIX}")
