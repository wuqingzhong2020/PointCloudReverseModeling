include_guard(GLOBAL)

function(rm_configure_runtime target)
    if(WIN32)
        set(_bin "${RM_SDK_PREFIX}/$<IF:$<CONFIG:Debug>,debug/bin,bin>")
        set(_environment "PATH=${_bin};%PATH%\nCSF_SHMessage=${RM_SDK_PREFIX}/share/opencascade/resources/SHMessage\nCSF_XSMessage=${RM_SDK_PREFIX}/share/opencascade/resources/XSMessage\nCSF_ShadersDirectory=${RM_SDK_PREFIX}/share/opencascade/resources/Shaders")
        string(APPEND _environment "\nQT_PLUGIN_PATH=${RM_SDK_PREFIX}/$<IF:$<CONFIG:Debug>,debug/Qt6/plugins,Qt6/plugins>")
        set_property(TARGET ${target} PROPERTY VS_DEBUGGER_ENVIRONMENT
            "${_environment}")
    endif()
endfunction()

function(rm_configure_test_runtime name)
    if(WIN32)
        set_property(TEST ${name} PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${RM_SDK_PREFIX}/$<IF:$<CONFIG:Debug>,debug/bin,bin>")
    elseif(UNIX)
        set_property(TEST ${name} PROPERTY ENVIRONMENT_MODIFICATION
            "LD_LIBRARY_PATH=path_list_prepend:${RM_SDK_PREFIX}/$<IF:$<CONFIG:Debug>,debug/lib,lib>")
    endif()
    set_property(TEST ${name} PROPERTY ENVIRONMENT
        "CSF_SHMessage=${RM_SDK_PREFIX}/share/opencascade/resources/SHMessage"
        "CSF_XSMessage=${RM_SDK_PREFIX}/share/opencascade/resources/XSMessage")
endfunction()
