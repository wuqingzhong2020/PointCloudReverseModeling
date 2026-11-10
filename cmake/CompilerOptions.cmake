# 文件描述：工程 C++ 编译约束与第三方头文件告警隔离。
include_guard(GLOBAL)

option(RM_WARNINGS_AS_ERRORS "Treat warnings in project code as errors" ON)

# 仅应用于本仓库构建的目标，PRIVATE 选项不传播给库的使用者。
# find_package() 导入目标的头文件保持 SYSTEM：MSVC 使用 /external:I，
# GCC/Clang 使用 -isystem。不要把工程 include 目录标成 SYSTEM。
function(rm_apply_compiler_options target)
    set_target_properties(${target} PROPERTIES
        COMPILE_WARNING_AS_ERROR "${RM_WARNINGS_AS_ERRORS}"
        NO_SYSTEM_FROM_IMPORTED OFF)

    if(MSVC)
        target_compile_options(${target} PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive-;/Zc:preprocessor;/Zc:__cplusplus;/utf-8;/bigobj;/FS;/external:W0>")
        # 不启用 /external:templates-，避免重新报告第三方模板内部的告警。
        # 不全局关闭 C4996 等告警：工程调用已弃用 API 时应改用受支持的 API。
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>")
    endif()
endfunction()
