# Resolve Windows SDK um/x64 import libs (e.g. ksguid.lib for WASAPI) to full paths at
# configure time so Ninja link lines do not depend on LIB being present in every build
# subprocess. GHA often sets LIB with ucrt paths only, or pwsh children drop um paths —
# link then fails LNK1181 even when a coarse "Windows Kits" check passes.

function(windows_sdk_um_find_lib lib_name out_var)
    set(_result "")
    if(NOT (WIN32 AND MSVC))
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    # Prefer a direct SDK path (stable on GHA); find_library can return a bare name.
    file(GLOB _wsu_versions "C:/Program Files (x86)/Windows Kits/10/Lib/*")
    if(_wsu_versions)
        list(SORT _wsu_versions)
        list(REVERSE _wsu_versions)
        foreach(_ver IN LISTS _wsu_versions)
            set(_candidate "${_ver}/um/x64/${lib_name}.lib")
            if(EXISTS "${_candidate}")
                get_filename_component(_result "${_candidate}" ABSOLUTE)
                break()
            endif()
        endforeach()
    endif()

    if(NOT _result)
        find_library(_wsu_found NAMES ${lib_name})
        if(_wsu_found)
            get_filename_component(_result "${_wsu_found}" ABSOLUTE)
        endif()
    endif()

    if(_result AND NOT EXISTS "${_result}")
        set(_result "")
    endif()

    set(${out_var} "${_result}" PARENT_SCOPE)
endfunction()

function(windows_sdk_um_link_lib target visibility lib_name)
    if(NOT (WIN32 AND MSVC))
        target_link_libraries(${target} ${visibility} ${lib_name})
        return()
    endif()

    windows_sdk_um_find_lib("${lib_name}" _wsu_path)
    if(_wsu_path)
        message(STATUS "Windows SDK: linking ${lib_name} from ${_wsu_path}")
        target_link_libraries(${target} ${visibility} "${_wsu_path}")
    else()
        message(FATAL_ERROR
            "Windows SDK: ${lib_name}.lib not found under "
            "Windows Kits/10/Lib/*/um/x64 (required for WASAPI). "
            "Configure with MSVC (cl) after vcvars64 / ilammy/msvc-dev-cmd.")
    endif()
endfunction()
