#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__ANDROID__)
void ssb64_android_restart_in_debug_mode(void);
void ssb64_android_restart_with_debug_env(void);
void ssb64_android_export_debug_log(void);
#endif

#ifdef __cplusplus
}
#endif
