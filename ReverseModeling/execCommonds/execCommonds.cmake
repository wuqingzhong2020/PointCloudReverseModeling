# 文件描述：从唯一模块清单生成并统一编译描述类，不收录业务实现。
set(RM_COMMAND_OUTPUT "${CMAKE_CURRENT_LIST_DIR}")
set(RM_COMMAND_DOCS "${CMAKE_SOURCE_DIR}/docs/commands")
set(RM_COMMAND_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/../commandDefinitions/modules.json")
set(RM_COMMAND_GENERATOR "${CMAKE_SOURCE_DIR}/commandManager/generator/rm_command_gen.py")
if(NOT RM_CODEGEN_PYTHON)
    if(EXISTS "${CMAKE_SOURCE_DIR}/build/codegen-python/Scripts/python.exe")
        set(RM_CODEGEN_PYTHON "${CMAKE_SOURCE_DIR}/build/codegen-python/Scripts/python.exe" CACHE FILEPATH "Python with Jinja2 (build tool only)")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/build/codegen-python/bin/python")
        set(RM_CODEGEN_PYTHON "${CMAKE_SOURCE_DIR}/build/codegen-python/bin/python" CACHE FILEPATH "Python with Jinja2 (build tool only)")
    else()
        find_program(RM_CODEGEN_PYTHON NAMES python3 python REQUIRED)
    endif()
endif()
set(_rm_codegen "${RM_CODEGEN_PYTHON}" -B "${RM_COMMAND_GENERATOR}"
    --manifest "${RM_COMMAND_MANIFEST}" --output "${RM_COMMAND_OUTPUT}"
    --docs "${RM_COMMAND_DOCS}" --build-dir "${CMAKE_BINARY_DIR}")
execute_process(COMMAND ${_rm_codegen} RESULT_VARIABLE _rm_generation_result)
if(NOT _rm_generation_result EQUAL 0)
    message(FATAL_ERROR "Command generation failed. Install commandManager/generator/requirements.txt into RM_CODEGEN_PYTHON.")
endif()
file(GLOB _rm_definitions CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../commandDefinitions/*.rmdef")
file(GLOB _rm_templates CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/commandManager/templates/*.j2")
file(GLOB _rm_scripts CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/commandManager/generator/*.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${RM_COMMAND_MANIFEST}" ${_rm_definitions} ${_rm_templates} ${_rm_scripts})
include("${CMAKE_BINARY_DIR}/commandManager/generator/sources.cmake")
set(_rm_codegen_stamp "${CMAKE_BINARY_DIR}/commandManager/generator/$<CONFIG>/complete.stamp")
add_custom_command(OUTPUT "${_rm_codegen_stamp}"
    COMMAND ${_rm_codegen}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/commandManager/generator/$<CONFIG>"
    COMMAND "${CMAKE_COMMAND}" -E touch "${_rm_codegen_stamp}"
    DEPENDS "${RM_COMMAND_MANIFEST}" ${_rm_definitions} ${_rm_templates} ${_rm_scripts}
    BYPRODUCTS ${RM_GENERATED_SOURCES} ${RM_GENERATED_HEADERS} ${RM_GENERATED_DOCS}
    COMMENT "Generating command descriptors and help" VERBATIM)
add_custom_target(rm_command_codegen DEPENDS "${_rm_codegen_stamp}")
add_library(RmCommandDefinitions STATIC ${RM_GENERATED_SOURCES} ${RM_GENERATED_HEADERS})
add_dependencies(RmCommandDefinitions rm_command_codegen)
target_include_directories(RmCommandDefinitions PUBLIC "${RM_COMMAND_OUTPUT}")
target_link_libraries(RmCommandDefinitions PUBLIC RmCommandManager)
