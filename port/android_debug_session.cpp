// android_debug_session.cpp — Port Menu hooks for debug restart / SAF export.

#if defined(__ANDROID__)

#include <SDL2/SDL.h>
#include <android/log.h>
#include <jni.h>

#define LOG_TAG "ssb64.debug"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static void callDebugHelperStatic(const char *method, const char *sig)
{
    JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    jobject activity = SDL_AndroidGetActivity();
    if (env == nullptr || activity == nullptr) {
        LOGE("JNIEnv or activity unavailable");
        return;
    }

    jclass helper = env->FindClass("com/jrickey/battleship/DebugSessionHelper");
    if (helper == nullptr) {
        LOGE("DebugSessionHelper class not found");
        return;
    }

    jmethodID mid = env->GetStaticMethodID(helper, method, sig);
    if (mid == nullptr) {
        LOGE("method %s not found", method);
        return;
    }

    env->CallStaticVoidMethod(helper, mid, activity);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
}

extern "C" void ssb64_android_restart_in_debug_mode(void)
{
    callDebugHelperStatic("restartInDebugMode", "(Landroid/app/Activity;)V");
}

extern "C" void ssb64_android_restart_with_debug_env(void)
{
    callDebugHelperStatic("restartWithDebugEnv", "(Landroid/app/Activity;)V");
}

extern "C" void ssb64_android_export_debug_log(void)
{
    callDebugHelperStatic("exportDebugLog", "(Landroid/app/Activity;)V");
}

#endif /* __ANDROID__ */
