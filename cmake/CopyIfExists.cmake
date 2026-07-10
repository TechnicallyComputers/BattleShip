# Copy SRC to DST only when SRC exists (POST_BUILD sidecars for cross-builds).
if(NOT DEFINED SRC OR NOT DEFINED DST)
    message(FATAL_ERROR "CopyIfExists.cmake requires -DSRC= and -DDST=")
endif()
if(EXISTS "${SRC}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SRC}" "${DST}")
endif()
