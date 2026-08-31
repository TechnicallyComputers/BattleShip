#ifndef PORT_ANDROID_NETWORK_H
#define PORT_ANDROID_NETWORK_H

#ifdef __cplusplus
extern "C" {
#endif

void port_android_network_drain(void);
/* 1 while the app is backgrounded (Android lifecycle watch); 0 on other platforms.
 * Gates render suppression (gameloop) and unpause blocking (netreconnect) while the
 * loop keeps running under SDL_ANDROID_BLOCK_ON_PAUSE=0. Thread-safe (atomic load). */
int port_android_app_backgrounded(void);
/* Store Activity context for deferred ConnectivityManager registration (Android). */
#if defined(__ANDROID__)
struct _JNIEnv;
typedef struct _JNIEnv JNIEnv;
typedef struct _jobject *jobject;
void port_android_network_bind_context(JNIEnv *env, jobject activity);
#else
void port_android_network_bind_context(void *ctx);
#endif
/* Request ConnectivityManager registration (Android: runs on SDL_main in drain). */
void port_android_network_try_arm_monitoring(void);
void port_android_network_disarm_monitoring(void);
#if defined(__ANDROID__)
void port_android_network_install(JNIEnv *env, jobject activity);
#else
void port_android_network_install(void *ctx);
#endif

#ifdef __cplusplus
}
#endif

#endif
