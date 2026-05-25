# Populate discord-rpc thirdparty/rapidjson-1.1.0 before discord-rpc configures.
# Invoked from FetchContent PATCH_COMMAND; RJ_ROOT is the discord-rpc source dir.
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED RJ_ROOT OR RJ_ROOT STREQUAL "")
    message(FATAL_ERROR "PopulateDiscordRpcRapidjson: RJ_ROOT not set")
endif()

set(_rj_inc "${RJ_ROOT}/thirdparty/rapidjson-1.1.0/include/rapidjson/document.h")
if(EXISTS "${_rj_inc}")
    return()
endif()

set(_thirdparty "${RJ_ROOT}/thirdparty")
set(_tar "${_thirdparty}/v1.1.0.tar.gz")
file(MAKE_DIRECTORY "${_thirdparty}")

message(STATUS "PopulateDiscordRpcRapidjson: downloading rapidjson v1.1.0")
file(DOWNLOAD
    https://github.com/Tencent/rapidjson/archive/refs/tags/v1.1.0.tar.gz
    "${_tar}"
    TLS_VERIFY ON
    STATUS _dl_status
    SHOW_PROGRESS
)
list(GET _dl_status 0 _dl_code)
list(GET _dl_status 1 _dl_msg)
if(NOT _dl_code EQUAL 0)
    message(FATAL_ERROR "PopulateDiscordRpcRapidjson: download failed (${_dl_code}): ${_dl_msg}")
endif()

message(STATUS "PopulateDiscordRpcRapidjson: extracting rapidjson")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar xzf "${_tar}"
    WORKING_DIRECTORY "${_thirdparty}"
    RESULT_VARIABLE _tar_res
    ERROR_VARIABLE _tar_err
)
if(_tar_res)
    message(FATAL_ERROR "PopulateDiscordRpcRapidjson: extract failed (${_tar_res}): ${_tar_err}")
endif()
file(REMOVE "${_tar}")

if(NOT EXISTS "${_rj_inc}")
    message(FATAL_ERROR "PopulateDiscordRpcRapidjson: expected ${_rj_inc} after extract")
endif()

message(STATUS "PopulateDiscordRpcRapidjson: rapidjson ready at ${_thirdparty}/rapidjson-1.1.0")
