#pragma once

#if defined(__ANDROID__)

#ifdef __cplusplus
extern "C" {
#endif

/** Next clean SDL_main return should not _exit the process (in-process debug restart). */
void ssb64_android_begin_in_process_restart(void);

/** If a restart was requested, clear the flag and return 1. */
int ssb64_android_consume_in_process_restart(void);

/** JNI callback to launch BootActivity without blocking on Activity.onDestroy join. */
void ssb64_android_notify_main_returned_for_restart(void);

#ifdef __cplusplus
}
#endif

#endif /* __ANDROID__ */
