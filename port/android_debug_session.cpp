// android_debug_session.cpp — Port Menu hooks for debug restart / SAF export.

#if defined(__ANDROID__)

#include "android_debug_restart.h"
#include "port_log.h"

#include <libultraship/libultraship.h>
#include <SDL2/SDL.h>
#include <android/log.h>
#include <jni.h>

#define LOG_TAG "ssb64.debug"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static void jniLogPendingException(JNIEnv *env, const char *context)
{
    if (env == nullptr || !env->ExceptionCheck()) {
        return;
    }
    LOGE("JNI exception: %s", context);
    env->ExceptionDescribe();
    env->ExceptionClear();
}

static void callDebugHelperStatic(const char *method, const char *sig)
{
    JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (env == nullptr || activity == nullptr) {
        LOGE("JNIEnv or activity unavailable");
        return;
    }

    jclass helper = env->FindClass("com/jrickey/battleship/DebugSessionHelper");
    if (helper == nullptr) {
        jniLogPendingException(env, "FindClass DebugSessionHelper");
        LOGE("DebugSessionHelper class not found");
        return;
    }

    jmethodID mid = env->GetStaticMethodID(helper, method, sig);
    if (mid == nullptr) {
        jniLogPendingException(env, method);
        LOGE("method %s not found", method);
        env->DeleteLocalRef(helper);
        return;
    }

    env->CallStaticVoidMethod(helper, mid, activity);
    jniLogPendingException(env, method);
    env->DeleteLocalRef(helper);
    /* activity is owned by SDL — do not DeleteLocalRef */
}

static void requestCooperativeMainLoopExit(void)
{
    ssb64_android_begin_in_process_restart();
    auto ctx = Ship::Context::GetInstance();
    if (ctx != nullptr) {
        if (auto win = ctx->GetWindow()) {
            win->Close();
        }
    }
    port_log("SSB64: debug restart — Window::Close() requested (in-process)\n");
}

extern "C" void ssb64_android_restart_in_debug_mode(void)
{
    /* Port menu runs on the SDL_main thread — safe to close the LUS window here. */
    requestCooperativeMainLoopExit();
    callDebugHelperStatic("restartInDebugMode", "(Landroid/app/Activity;)V");
}

extern "C" JNIEXPORT void JNICALL
Java_com_jrickey_battleship_BattleShipActivity_nativeBeginInProcessRestart(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;
    ssb64_android_begin_in_process_restart();
}

extern "C" void ssb64_android_restart_with_debug_env(void)
{
    /* SAF picker runs on UI thread; no SDL_main yet if called before game — skip Close. */
    callDebugHelperStatic("restartWithDebugEnv", "(Landroid/app/Activity;)V");
}

extern "C" void ssb64_android_export_debug_log(void)
{
    callDebugHelperStatic("exportDebugLog", "(Landroid/app/Activity;)V");
}

extern "C" void ssb64_android_notify_main_returned_for_restart(void)
{
    JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    if (env == nullptr) {
        LOGE("notify_main_returned: JNIEnv unavailable");
        return;
    }

    jclass activity = env->FindClass("com/jrickey/battleship/BattleShipActivity");
    if (activity == nullptr) {
        jniLogPendingException(env, "FindClass BattleShipActivity");
        LOGE("BattleShipActivity class not found");
        return;
    }

    jmethodID mid = env->GetStaticMethodID(activity, "onNativeMainReturnedForDebugRestart", "()V");
    if (mid == nullptr) {
        jniLogPendingException(env, "onNativeMainReturnedForDebugRestart");
        env->DeleteLocalRef(activity);
        return;
    }

    env->CallStaticVoidMethod(activity, mid);
    jniLogPendingException(env, "onNativeMainReturnedForDebugRestart");
    env->DeleteLocalRef(activity);
}

#endif /* __ANDROID__ */
