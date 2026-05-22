# Resolve Windows SDK um/x64 import libs (e.g. ksguid.lib for WASAPI) to full paths at
# configure time so Ninja link lines do not depend on LIB being present in every build
# subprocess. Newest SDK version dirs (e.g. 10.0.26100.0) may omit ksguid.lib; scan all.

function(_windows_sdk_um_glob_versions out_list)
    set(_roots "")
    if(EXISTS "C:/Program Files (x86)/Windows Kits/10/Lib")
        list(APPEND _roots "C:/Program Files (x86)/Windows Kits/10/Lib")
    endif()
    if(EXISTS "C:/Program Files/Windows Kits/10/Lib")
        list(APPEND _roots "C:/Program Files/Windows Kits/10/Lib")
    endif()
    set(_all_versions "")
    foreach(_root IN LISTS _roots)
        file(GLOB _versions "${_root}/*")
        if(_versions)
            list(APPEND _all_versions ${_versions})
        endif()
    endforeach()
    if(_all_versions)
        list(SORT _all_versions)
        list(REVERSE _all_versions)
    endif()
    set(${out_list} "${_all_versions}" PARENT_SCOPE)
endfunction()

function(windows_sdk_um_find_lib lib_name out_var)
    set(_result "")
    if(NOT WIN32)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    _windows_sdk_um_glob_versions(_wsu_versions)
    foreach(_ver IN LISTS _wsu_versions)
        set(_candidate "${_ver}/um/x64/${lib_name}.lib")
        if(EXISTS "${_candidate}")
            get_filename_component(_result "${_candidate}" ABSOLUTE)
            break()
        endif()
    endforeach()

    if(NOT _result)
        foreach(_root IN ITEMS
            "C:/Program Files (x86)/Windows Kits/10/Lib"
            "C:/Program Files/Windows Kits/10/Lib"
        )
            if(NOT EXISTS "${_root}")
                continue()
            endif()
            file(GLOB_RECURSE _wsu_recurse "${_root}/*/${lib_name}.lib")
            if(_wsu_recurse)
                list(GET _wsu_recurse 0 _result)
                get_filename_component(_result "${_result}" ABSOLUTE)
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
    if(NOT WIN32)
        target_link_libraries(${target} ${visibility} ${lib_name})
        return()
    endif()

    if(DEFINED SSB64_KSGUID_LIB AND EXISTS "${SSB64_KSGUID_LIB}")
        message(STATUS "Windows SDK: linking ${lib_name} from ${SSB64_KSGUID_LIB}")
        target_link_libraries(${target} ${visibility} "${SSB64_KSGUID_LIB}")
        return()
    endif()

    windows_sdk_um_find_lib("${lib_name}" _wsu_path)
    if(_wsu_path)
        message(STATUS "Windows SDK: linking ${lib_name} from ${_wsu_path}")
        target_link_libraries(${target} ${visibility} "${_wsu_path}")
    elseif(MINGW)
        target_link_libraries(${target} ${visibility} ${lib_name})
    else()
        message(FATAL_ERROR
            "Windows SDK: ${lib_name}.lib not found under "
            "Windows Kits/10/Lib (tried all SDK versions under um/x64 and recursive search). "
            "Install the Windows 10 SDK desktop/um libraries on the build host.")
    endif()
endfunction()
