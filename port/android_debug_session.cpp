// android_debug_session.cpp — Port Menu hooks for debug session arming / SAF export.

#if defined(__ANDROID__)

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

extern "C" void ssb64_android_restart_in_debug_mode(void)
{
    /* Arms .battleship_debug_session; user relaunches from launcher (no in-process restart). */
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
